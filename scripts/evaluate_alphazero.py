#!/usr/bin/env python3
"""Evaluate a saved AlphaZero-style model against the current tactical MCTS."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from alphazero import NeuralSearchConfig, PolicyValueNetwork, evaluate_model_against_mcts
from common import load_native_module, repo_root


def parse_seeds(value: str) -> list[int]:
    try:
        result = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("seeds must be comma-separated integers") from exc
    if not result:
        raise argparse.ArgumentTypeError("at least one seed is required")
    return result


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model",
        type=Path,
        default=root / "models" / "alphazero" / "utt_majority_v1.npz",
    )
    parser.add_argument("--games-per-side", type=int, default=8)
    parser.add_argument("--seeds", type=parse_seeds, default=[20260841, 20260842])
    parser.add_argument("--neural-simulations", type=int, default=64)
    parser.add_argument("--neural-c-puct", type=float, default=0.3)
    parser.add_argument("--tactical-prior-weight", type=float, default=0.25)
    parser.add_argument("--rollout-value-weight", type=float, default=0.25)
    parser.add_argument("--rollout-limit", type=int, default=32)
    parser.add_argument("--baseline-n-playout", type=int, default=3000)
    parser.add_argument("--baseline-c-puct", type=float, default=0.3)
    parser.add_argument("--output-json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.games_per_side <= 0:
        raise SystemExit("games-per-side must be positive")
    model = PolicyValueNetwork.load(args.model)
    result = evaluate_model_against_mcts(
        load_native_module(),
        model,
        games_per_side=args.games_per_side,
        seeds=args.seeds,
        neural_config=NeuralSearchConfig(
            simulations=args.neural_simulations,
            c_puct=args.neural_c_puct,
            dirichlet_epsilon=0.0,
            tactical_prior_weight=args.tactical_prior_weight,
            rollout_value_weight=args.rollout_value_weight,
            rollout_limit=args.rollout_limit,
        ),
        baseline_n_playout=args.baseline_n_playout,
        baseline_c_puct=args.baseline_c_puct,
    )
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(encoded + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
