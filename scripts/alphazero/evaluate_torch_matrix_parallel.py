#!/usr/bin/env python3
"""Run the complete AlphaZero search-step comparison matrix on CPU workers."""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from multiprocessing import get_context
from pathlib import Path
from typing import Any

# Native MCTS is single-threaded. Keep Torch and BLAS from multiplying threads
# inside each process so the worker count maps to the available CPU cores.
os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import torch

torch.set_num_threads(1)
torch.set_num_interop_threads(1)

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.alphazero import NeuralSearchConfig
from scripts.alphazero.torch_impl import RULE_VERSION, TorchPolicyValueNetwork, _model_prior
from scripts.common import load_native_module, repo_root


_WORKER_MODULE: Any | None = None
_WORKER_MODEL: TorchPolicyValueNetwork | None = None


def parse_seeds(value: str) -> list[int]:
    try:
        seeds = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("seeds must be comma-separated integers") from exc
    if not seeds:
        raise argparse.ArgumentTypeError("at least one seed is required")
    return seeds


def parse_positive_int_list(value: str) -> list[int]:
    try:
        values = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("values must be comma-separated integers") from exc
    if not values or any(item <= 0 for item in values):
        raise argparse.ArgumentTypeError("values must be positive")
    return values


def wilson_interval(successes: int, total: int, z: float = 1.96) -> tuple[float, float]:
    if total == 0:
        return (0.0, 0.0)
    proportion = successes / total
    denominator = 1.0 + z * z / total
    center = (proportion + z * z / (2.0 * total)) / denominator
    radius = z * math.sqrt(
        proportion * (1.0 - proportion) / total + z * z / (4.0 * total * total)
    ) / denominator
    return (max(0.0, center - radius), min(1.0, center + radius))


def native_prior_spec(
    label: str,
    simulations: int,
    c_puct: float,
    policy_exponent: float,
    rollout_limit: int,
) -> dict[str, Any]:
    return {
        "label": label,
        "mode": "native-prior",
        "simulations": int(simulations),
        "c_puct": float(c_puct),
        "rollout_limit": int(rollout_limit),
        "policy_exponent": float(policy_exponent),
        "tactical_prior_weight": 0.0,
        "rollout_value_weight": 0.0,
        "force_tactical": False,
        "root_selection": "q",
    }


def tactical_spec(n_playout: int, c_puct: float, rollout_limit: int) -> dict[str, Any]:
    return {
        "label": f"tactical-{int(n_playout)}",
        "mode": "tactical",
        "simulations": int(n_playout),
        "c_puct": float(c_puct),
        "rollout_limit": int(rollout_limit),
    }


def build_matchups(args: argparse.Namespace) -> list[dict[str, Any]]:
    tactical = tactical_spec(
        args.tactical_n_playout,
        args.tactical_c_puct,
        args.tactical_rollout_limit,
    )
    low_specs = {
        simulations: native_prior_spec(
            f"native-prior-{simulations}",
            simulations,
            args.c_puct,
            args.policy_exponent,
            args.rollout_limit,
        )
        for simulations in args.low_simulations
    }
    high_spec = native_prior_spec(
        f"native-prior-{args.high_simulations}",
        args.high_simulations,
        args.c_puct,
        args.policy_exponent,
        args.rollout_limit,
    )

    matchups: list[dict[str, Any]] = []
    for simulations in args.low_simulations:
        matchups.append(
            {
                "id": f"low_{simulations}_vs_tactical",
                "candidate": low_specs[simulations],
                "opponent": tactical,
            }
        )
    for simulations in args.low_simulations:
        matchups.append(
            {
                "id": f"high_{args.high_simulations}_vs_low_{simulations}",
                "candidate": high_spec,
                "opponent": low_specs[simulations],
            }
        )
    return matchups


def _worker_init(model_path: str) -> None:
    global _WORKER_MODULE, _WORKER_MODEL
    os.environ["OMP_NUM_THREADS"] = "1"
    os.environ["MKL_NUM_THREADS"] = "1"
    os.environ["OPENBLAS_NUM_THREADS"] = "1"
    torch.set_num_threads(1)
    _WORKER_MODULE = load_native_module()
    _WORKER_MODEL = TorchPolicyValueNetwork.load(Path(model_path), device="cpu")


def _make_engine(spec: dict[str, Any], seed: int) -> Any:
    if _WORKER_MODULE is None:
        raise RuntimeError("parallel evaluator worker was not initialized")
    return _WORKER_MODULE.MCTSPure(
        n_playout=int(spec["simulations"]),
        c_puct=float(spec["c_puct"]),
        seed=int(seed) & 0xFFFFFFFF,
        rollout_policy=1,
        rollout_limit=int(spec["rollout_limit"]),
    )


def _choose_action(
    engine: Any,
    spec: dict[str, Any],
    config: NeuralSearchConfig | None,
    game: Any,
) -> int:
    if spec["mode"] == "tactical":
        return int(engine.get_move(game))
    if spec["mode"] != "native-prior" or config is None or _WORKER_MODEL is None:
        raise RuntimeError(f"unsupported search mode: {spec['mode']}")
    prior, allowed_actions = _model_prior(_WORKER_MODEL, game, config)
    return int(
        engine.get_move_with_priors(
            game,
            prior.tolist(),
            allowed_actions or [],
            config.root_selection == "q",
        )
    )


def _play_match_game(task: tuple[int, dict[str, Any], int, bool]) -> dict[str, Any]:
    index, matchup, seed, candidate_first = task
    if _WORKER_MODULE is None or _WORKER_MODEL is None:
        raise RuntimeError("parallel evaluator worker was not initialized")

    candidate_spec = matchup["candidate"]
    opponent_spec = matchup["opponent"]
    candidate_config = (
        NeuralSearchConfig(
            simulations=int(candidate_spec["simulations"]),
            c_puct=float(candidate_spec["c_puct"]),
            dirichlet_alpha=0.30,
            dirichlet_epsilon=0.0,
            tactical_prior_weight=float(candidate_spec.get("tactical_prior_weight", 0.0)),
            rollout_value_weight=float(candidate_spec.get("rollout_value_weight", 0.0)),
            rollout_limit=int(candidate_spec["rollout_limit"]),
            force_tactical=bool(candidate_spec.get("force_tactical", False)),
            inference_batch_size=32,
            policy_exponent=float(candidate_spec.get("policy_exponent", 1.0)),
            root_selection=str(candidate_spec.get("root_selection", "q")),
        )
        if candidate_spec["mode"] == "native-prior"
        else None
    )
    opponent_config = (
        NeuralSearchConfig(
            simulations=int(opponent_spec["simulations"]),
            c_puct=float(opponent_spec["c_puct"]),
            dirichlet_alpha=0.30,
            dirichlet_epsilon=0.0,
            tactical_prior_weight=float(opponent_spec.get("tactical_prior_weight", 0.0)),
            rollout_value_weight=float(opponent_spec.get("rollout_value_weight", 0.0)),
            rollout_limit=int(opponent_spec["rollout_limit"]),
            force_tactical=bool(opponent_spec.get("force_tactical", False)),
            inference_batch_size=32,
            policy_exponent=float(opponent_spec.get("policy_exponent", 1.0)),
            root_selection=str(opponent_spec.get("root_selection", "q")),
        )
        if opponent_spec["mode"] == "native-prior"
        else None
    )
    candidate = _make_engine(candidate_spec, int(seed) + 7)
    opponent = _make_engine(opponent_spec, int(seed) + 13)
    candidate_player = 1 if candidate_first else 2
    game = _WORKER_MODULE.UltimateTicTacToe()
    moves = 0
    while True:
        done, winner = game.get_done_winner()
        if done:
            winner = int(winner)
            if winner == -1:
                candidate_winner = 0
            elif (winner == 1) == bool(candidate_first):
                candidate_winner = 1
            else:
                candidate_winner = 2
            return {
                "index": int(index),
                "matchup": matchup["id"],
                "seed": int(seed),
                "candidate_first": bool(candidate_first),
                "candidate_winner": candidate_winner,
                "winner": winner,
                "moves": int(moves),
            }
        if moves >= 81:
            raise RuntimeError("comparison game exceeded the maximum move count")

        current_player = int(game.get_current_player())
        if current_player == candidate_player:
            action = _choose_action(candidate, candidate_spec, candidate_config, game)
            opponent.update_with_move(action)
        else:
            action = _choose_action(opponent, opponent_spec, opponent_config, game)
            candidate.update_with_move(action)
        if action < 0 or not game.is_action_valid(action):
            raise RuntimeError(f"comparison player produced invalid action {action}")
        if not game.make_move(action):
            raise RuntimeError(f"comparison player produced invalid action {action}")
        moves += 1


def summarize(records: list[dict[str, Any]], matchup: dict[str, Any]) -> dict[str, Any]:
    ordered = sorted(records, key=lambda item: int(item["index"]))
    wins = sum(item["candidate_winner"] == 1 for item in ordered)
    losses = sum(item["candidate_winner"] == 2 for item in ordered)
    draws = sum(item["candidate_winner"] == 0 for item in ordered)
    total = len(ordered)
    first = [item for item in ordered if item["candidate_first"]]
    second = [item for item in ordered if not item["candidate_first"]]

    def wdl(items: list[dict[str, Any]]) -> dict[str, int]:
        return {
            "wins": sum(item["candidate_winner"] == 1 for item in items),
            "draws": sum(item["candidate_winner"] == 0 for item in items),
            "losses": sum(item["candidate_winner"] == 2 for item in items),
        }

    return {
        "id": matchup["id"],
        "candidate": matchup["candidate"],
        "opponent": matchup["opponent"],
        "candidate_wins": wins,
        "candidate_losses": losses,
        "draws": draws,
        "total_games": total,
        "score_rate": (wins + 0.5 * draws) / total if total else 0.0,
        "win_rate": wins / total if total else 0.0,
        "win_rate_95ci": wilson_interval(wins, total),
        "mean_moves": (
            sum(int(item["moves"]) for item in ordered) / total if total else 0.0
        ),
        "candidate_first_wdl": wdl(first),
        "candidate_second_wdl": wdl(second),
        "records": ordered,
    }


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model",
        type=Path,
        default=root / "models" / "alphazero" / "utt_majority_v1_torch_teacher6000_512_hard.pt",
    )
    parser.add_argument("--workers", type=int, default=min(32, os.cpu_count() or 1))
    parser.add_argument("--games-per-side", type=int, default=2)
    parser.add_argument(
        "--seeds",
        type=parse_seeds,
        default=list(range(20260900, 20260910)),
    )
    parser.add_argument("--low-simulations", type=parse_positive_int_list, default=[24000, 12000, 6000, 3000])
    parser.add_argument("--high-simulations", type=int, default=48000)
    parser.add_argument("--c-puct", type=float, default=0.2)
    parser.add_argument("--policy-exponent", type=float, default=0.5)
    parser.add_argument("--rollout-limit", type=int, default=32)
    parser.add_argument("--tactical-n-playout", type=int, default=3000)
    parser.add_argument("--tactical-c-puct", type=float, default=0.3)
    parser.add_argument("--tactical-rollout-limit", type=int, default=32)
    parser.add_argument("--output-json", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.workers <= 0 or args.games_per_side <= 0:
        raise SystemExit("workers and games-per-side must be positive")
    if args.high_simulations <= 0 or any(item <= 0 for item in args.low_simulations):
        raise SystemExit("simulation counts must be positive")
    if args.c_puct <= 0.0 or args.tactical_c_puct <= 0.0:
        raise SystemExit("c-puct values must be positive")
    if args.rollout_limit <= 0 or args.tactical_rollout_limit <= 0:
        raise SystemExit("rollout limits must be positive")
    if not args.model.is_file():
        raise SystemExit(f"model does not exist: {args.model}")

    matchups = build_matchups(args)
    tasks: list[tuple[int, dict[str, Any], int, bool]] = []
    index = 0
    for matchup in matchups:
        for seed in args.seeds:
            for offset in range(args.games_per_side):
                tasks.append((index, matchup, int(seed) + offset * 101, True))
                index += 1
            for offset in range(args.games_per_side):
                tasks.append(
                    (
                        index,
                        matchup,
                        int(seed) + args.games_per_side * 101 + offset * 101,
                        False,
                    )
                )
                index += 1

    worker_count = min(int(args.workers), len(tasks))
    print(
        json.dumps(
            {
                "rule_version": RULE_VERSION,
                "model": str(args.model.resolve()),
                "workers": worker_count,
                "matchups": [item["id"] for item in matchups],
                "total_games": len(tasks),
            },
            ensure_ascii=False,
        ),
        flush=True,
    )

    started = time.perf_counter()
    records: list[dict[str, Any]] = []
    context = get_context("spawn")
    with ProcessPoolExecutor(
        max_workers=worker_count,
        mp_context=context,
        initializer=_worker_init,
        initargs=(str(args.model.resolve()),),
    ) as executor:
        futures = [executor.submit(_play_match_game, task) for task in tasks]
        for completed, future in enumerate(as_completed(futures), start=1):
            record = future.result()
            records.append(record)
            print(
                json.dumps(
                    {
                        "completed": completed,
                        "total": len(tasks),
                        "matchup": record["matchup"],
                        "candidate_first": record["candidate_first"],
                        "candidate_winner": record["candidate_winner"],
                        "moves": record["moves"],
                        "elapsed_s": round(time.perf_counter() - started, 2),
                    },
                    ensure_ascii=False,
                ),
                flush=True,
            )

    records_by_matchup = {
        matchup["id"]: summarize(
            [record for record in records if record["matchup"] == matchup["id"]],
            matchup,
        )
        for matchup in matchups
    }
    ordered_summaries = [records_by_matchup[matchup["id"]] for matchup in matchups]
    elapsed = time.perf_counter() - started
    result = {
        "rule_version": RULE_VERSION,
        "model": str(args.model.resolve()),
        "device": "cpu-workers",
        "workers": worker_count,
        "games_per_side": int(args.games_per_side),
        "seeds": [int(seed) for seed in args.seeds],
        "total_games": len(records),
        "wall_time_s": elapsed,
        "matrix": {
            "low_simulations": [int(item) for item in args.low_simulations],
            "high_simulations": int(args.high_simulations),
            "native_prior_c_puct": float(args.c_puct),
            "native_prior_policy_exponent": float(args.policy_exponent),
            "native_prior_rollout_limit": int(args.rollout_limit),
            "tactical_n_playout": int(args.tactical_n_playout),
            "tactical_c_puct": float(args.tactical_c_puct),
            "tactical_rollout_limit": int(args.tactical_rollout_limit),
        },
        "matchups": ordered_summaries,
    }
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(encoded + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
