#!/usr/bin/env python3
"""Run the native-prior AlphaZero validation games in CPU worker processes."""

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

# Keep each native search worker on one host thread.
os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import torch

torch.set_num_threads(1)
torch.set_num_interop_threads(1)

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.alphazero import NeuralSearchConfig
from scripts.alphazero.torch_impl import (
    RULE_VERSION,
    TorchPolicyValueNetwork,
    _play_native_prior_game,
)
from scripts.common import load_native_module, repo_root


_WORKER_MODULE: Any | None = None
_WORKER_MODEL: TorchPolicyValueNetwork | None = None
_WORKER_CONFIG: NeuralSearchConfig | None = None
_WORKER_BASELINE_N_PLAYOUT = 3000
_WORKER_BASELINE_C_PUCT = 0.3


def parse_seeds(value: str) -> list[int]:
    try:
        seeds = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("seeds must be comma-separated integers") from exc
    if not seeds:
        raise argparse.ArgumentTypeError("at least one seed is required")
    return seeds


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


def _worker_init(
    model_path: str,
    config_data: dict[str, Any],
    baseline_n_playout: int,
    baseline_c_puct: float,
) -> None:
    global _WORKER_MODULE, _WORKER_MODEL, _WORKER_CONFIG
    global _WORKER_BASELINE_N_PLAYOUT, _WORKER_BASELINE_C_PUCT

    os.environ["OMP_NUM_THREADS"] = "1"
    os.environ["MKL_NUM_THREADS"] = "1"
    os.environ["OPENBLAS_NUM_THREADS"] = "1"
    torch.set_num_threads(1)
    _WORKER_MODULE = load_native_module()
    _WORKER_MODEL = TorchPolicyValueNetwork.load(Path(model_path), device="cpu")
    _WORKER_CONFIG = NeuralSearchConfig(**config_data)
    _WORKER_BASELINE_N_PLAYOUT = int(baseline_n_playout)
    _WORKER_BASELINE_C_PUCT = float(baseline_c_puct)


def _run_game(task: tuple[int, int, bool]) -> dict[str, Any]:
    index, seed, candidate_first = task
    if _WORKER_MODULE is None or _WORKER_MODEL is None or _WORKER_CONFIG is None:
        raise RuntimeError("parallel evaluator worker was not initialized")
    winner, moves = _play_native_prior_game(
        _WORKER_MODULE,
        _WORKER_MODEL,
        _WORKER_CONFIG,
        _WORKER_BASELINE_N_PLAYOUT,
        _WORKER_BASELINE_C_PUCT,
        bool(candidate_first),
        int(seed),
    )
    if int(winner) == -1:
        candidate_winner = 0
    elif (int(winner) == 1) == bool(candidate_first):
        candidate_winner = 1
    else:
        candidate_winner = 2
    return {
        "index": int(index),
        "seed": int(seed),
        "candidate_first": bool(candidate_first),
        "candidate_winner": int(candidate_winner),
        "winner": int(winner),
        "moves": int(moves),
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
    parser.add_argument("--simulations", type=int, default=48000)
    parser.add_argument("--c-puct", type=float, default=0.2)
    parser.add_argument("--policy-exponent", type=float, default=0.5)
    parser.add_argument("--rollout-limit", type=int, default=32)
    parser.add_argument("--baseline-n-playout", type=int, default=3000)
    parser.add_argument("--baseline-c-puct", type=float, default=0.3)
    parser.add_argument("--output-json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.workers <= 0 or args.games_per_side <= 0 or args.simulations <= 0:
        raise SystemExit("workers/games-per-side/simulations must be positive")
    if not args.model.is_file():
        raise SystemExit(f"model does not exist: {args.model}")

    tasks: list[tuple[int, int, bool]] = []
    index = 0
    for seed in args.seeds:
        for offset in range(args.games_per_side):
            tasks.append((index, int(seed) + offset * 101, True))
            index += 1
        for offset in range(args.games_per_side):
            tasks.append(
                (
                    index,
                    int(seed) + args.games_per_side * 101 + offset * 101,
                    False,
                )
            )
            index += 1

    config_data: dict[str, Any] = {
        "simulations": int(args.simulations),
        "c_puct": float(args.c_puct),
        "dirichlet_alpha": 0.30,
        "dirichlet_epsilon": 0.0,
        "tactical_prior_weight": 0.0,
        "rollout_value_weight": 0.0,
        "rollout_limit": int(args.rollout_limit),
        "force_tactical": False,
        "inference_batch_size": 32,
        "policy_exponent": float(args.policy_exponent),
        "root_selection": "q",
    }

    print(
        json.dumps(
            {
                "rule_version": RULE_VERSION,
                "model": str(args.model.resolve()),
                "workers": min(int(args.workers), len(tasks)),
                "total_games": len(tasks),
                "config": config_data,
                "baseline_n_playout": int(args.baseline_n_playout),
                "baseline_c_puct": float(args.baseline_c_puct),
            },
            ensure_ascii=False,
        ),
        flush=True,
    )

    started = time.perf_counter()
    records: list[dict[str, Any]] = []
    context = get_context("spawn")
    with ProcessPoolExecutor(
        max_workers=min(int(args.workers), len(tasks)),
        mp_context=context,
        initializer=_worker_init,
        initargs=(
            str(args.model.resolve()),
            config_data,
            int(args.baseline_n_playout),
            float(args.baseline_c_puct),
        ),
    ) as executor:
        futures = [executor.submit(_run_game, task) for task in tasks]
        for completed, future in enumerate(as_completed(futures), start=1):
            record = future.result()
            records.append(record)
            print(
                json.dumps(
                    {
                        "completed": completed,
                        "total": len(tasks),
                        "index": record["index"],
                        "candidate_first": record["candidate_first"],
                        "candidate_winner": record["candidate_winner"],
                        "moves": record["moves"],
                        "elapsed_s": round(time.perf_counter() - started, 2),
                    },
                    ensure_ascii=False,
                ),
                flush=True,
            )

    records.sort(key=lambda item: int(item["index"]))
    wins = sum(item["candidate_winner"] == 1 for item in records)
    losses = sum(item["candidate_winner"] == 2 for item in records)
    draws = sum(item["candidate_winner"] == 0 for item in records)
    total = len(records)
    elapsed = time.perf_counter() - started
    result = {
        "rule_version": RULE_VERSION,
        "model": str(args.model.resolve()),
        "device": "cpu-workers",
        "workers": min(int(args.workers), len(tasks)),
        "candidate_mode": "native-prior",
        "simulations": int(args.simulations),
        "c_puct": float(args.c_puct),
        "policy_exponent": float(args.policy_exponent),
        "rollout_limit": int(args.rollout_limit),
        "root_selection": "q",
        "baseline_n_playout": int(args.baseline_n_playout),
        "baseline_c_puct": float(args.baseline_c_puct),
        "games_per_side": int(args.games_per_side),
        "seeds": [int(seed) for seed in args.seeds],
        "candidate_wins": wins,
        "candidate_losses": losses,
        "draws": draws,
        "total_games": total,
        "score_rate": (wins + 0.5 * draws) / total if total else 0.0,
        "win_rate": wins / total if total else 0.0,
        "win_rate_95ci": wilson_interval(wins, total),
        "wall_time_s": elapsed,
        "records": records,
    }
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(encoded + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
