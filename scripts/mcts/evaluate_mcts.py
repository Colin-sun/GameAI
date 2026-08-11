#!/usr/bin/env python3
"""Run reproducible, side-balanced MCTS matches and report W/D/L statistics."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.common import load_native_module


POLICY_IDS = {"uniform": 0, "tactical": 1}


@dataclass(frozen=True)
class MCTSConfig:
    n_playout: int
    c_puct: float
    rollout_policy: int
    rollout_limit: int


def parse_policy(value: str) -> int:
    if value in POLICY_IDS:
        return POLICY_IDS[value]
    policy_id = int(value)
    if policy_id not in (0, 1):
        raise argparse.ArgumentTypeError("rollout policy must be uniform/tactical or 0/1")
    return policy_id


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


def score_interval(scores: list[float], z: float = 1.96) -> tuple[float, float]:
    if not scores:
        return (0.0, 0.0)
    mean = sum(scores) / len(scores)
    if len(scores) == 1:
        return (mean, mean)
    variance = sum((score - mean) ** 2 for score in scores) / (len(scores) - 1)
    radius = z * math.sqrt(variance / len(scores))
    return (max(0.0, mean - radius), min(1.0, mean + radius))


def evaluate(module, candidate: MCTSConfig, baseline: MCTSConfig, games_per_side: int, seeds: list[int]) -> dict:
    records = []
    score_values: list[float] = []
    candidate_wins = candidate_losses = draws = 0
    for seed in seeds:
        result = module.compare_mcts_detailed(
            candidate.n_playout,
            candidate.c_puct,
            baseline.n_playout,
            baseline.c_puct,
            games_per_side=games_per_side,
            seed=seed,
            rollout_policy1=candidate.rollout_policy,
            rollout_limit1=candidate.rollout_limit,
            rollout_policy2=baseline.rollout_policy,
            rollout_limit2=baseline.rollout_limit,
        )
        wins = int(result["candidate1_wins"])
        losses = int(result["candidate2_wins"])
        seed_draws = int(result["draws"])
        candidate_wins += wins
        candidate_losses += losses
        draws += seed_draws
        score_values.extend([1.0] * wins + [0.5] * seed_draws + [0.0] * losses)
        records.append(
            {
                "seed": seed,
                "candidate1_wins": wins,
                "candidate2_wins": losses,
                "draws": seed_draws,
                "score1": float(result["score1"]),
                "score2": float(result["score2"]),
                "games": result["games"],
            }
        )

    total_games = candidate_wins + candidate_losses + draws
    score = candidate_wins + 0.5 * draws
    return {
        "candidate": candidate.__dict__,
        "baseline": baseline.__dict__,
        "games_per_side": games_per_side,
        "seeds": seeds,
        "candidate1_wins": candidate_wins,
        "candidate2_wins": candidate_losses,
        "draws": draws,
        "total_games": total_games,
        "score1": score,
        "score2": total_games - score,
        "score_rate": score / total_games if total_games else 0.0,
        "score_rate_95ci": score_interval(score_values),
        "win_rate": candidate_wins / total_games if total_games else 0.0,
        "win_rate_95ci": wilson_interval(candidate_wins, total_games),
        "records": records,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-n-playout", type=int, required=True)
    parser.add_argument("--candidate-c-puct", type=float, required=True)
    parser.add_argument("--candidate-rollout-policy", type=parse_policy, default=1)
    parser.add_argument("--candidate-rollout-limit", type=int, default=32)
    parser.add_argument("--baseline-n-playout", type=int, default=3000)
    parser.add_argument("--baseline-c-puct", type=float, default=0.3)
    parser.add_argument("--baseline-rollout-policy", type=parse_policy, default=1)
    parser.add_argument("--baseline-rollout-limit", type=int, default=32)
    parser.add_argument("--games-per-side", type=int, default=8)
    parser.add_argument("--seeds", type=parse_seeds, default=[20260809, 20260810])
    parser.add_argument("--json", action="store_true", help="Print one machine-readable JSON object.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.games_per_side <= 0:
        raise SystemExit("games-per-side must be positive")
    if args.candidate_n_playout <= 0 or args.baseline_n_playout <= 0:
        raise SystemExit("playout counts must be positive")
    if args.candidate_rollout_limit <= 0 or args.baseline_rollout_limit <= 0:
        raise SystemExit("rollout limits must be positive")

    candidate = MCTSConfig(
        args.candidate_n_playout,
        args.candidate_c_puct,
        args.candidate_rollout_policy,
        args.candidate_rollout_limit,
    )
    baseline = MCTSConfig(
        args.baseline_n_playout,
        args.baseline_c_puct,
        args.baseline_rollout_policy,
        args.baseline_rollout_limit,
    )
    result = evaluate(load_native_module(), candidate, baseline, args.games_per_side, args.seeds)
    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
