#!/usr/bin/env python3
"""Train the optional NumPy AlphaZero-style UTT policy/value model."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.alphazero import NeuralSearchConfig, PolicyValueNetwork, train_model
from scripts.common import load_native_module, repo_root


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "models" / "alphazero" / "utt_majority_v1.npz",
    )
    parser.add_argument("--seed", type=int, default=20260809)
    parser.add_argument("--hidden-size", type=int, default=128)
    parser.add_argument("--learning-rate", type=float, default=0.002)
    parser.add_argument("--weight-decay", type=float, default=1.0e-4)
    parser.add_argument("--teacher-games", type=int, default=40)
    parser.add_argument("--teacher-playout", type=int, default=128)
    parser.add_argument("--teacher-c-puct", type=float, default=0.3)
    parser.add_argument("--iterations", type=int, default=2)
    parser.add_argument("--self-play-games", type=int, default=8)
    parser.add_argument("--self-play-simulations", type=int, default=96)
    parser.add_argument("--self-play-c-puct", type=float, default=0.3)
    parser.add_argument("--self-play-tactical-prior-weight", type=float, default=0.5)
    parser.add_argument("--self-play-rollout-value-weight", type=float, default=0.5)
    parser.add_argument("--self-play-rollout-limit", type=int, default=32)
    parser.add_argument("--temperature-moves", type=int, default=12)
    parser.add_argument("--epochs", type=int, default=6)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--replay-capacity", type=int, default=20000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if min(args.teacher_games, args.iterations, args.self_play_games, args.epochs) < 0:
        raise SystemExit("training counts cannot be negative")
    module = load_native_module()
    model = PolicyValueNetwork(
        hidden_size=args.hidden_size,
        learning_rate=args.learning_rate,
        weight_decay=args.weight_decay,
        seed=args.seed,
    )
    metrics = train_model(
        module,
        model,
        seed=args.seed,
        teacher_games=args.teacher_games,
        teacher_playout=args.teacher_playout,
        teacher_c_puct=args.teacher_c_puct,
        iterations=args.iterations,
        self_play_games=args.self_play_games,
        self_play_config=NeuralSearchConfig(
            simulations=args.self_play_simulations,
            c_puct=args.self_play_c_puct,
            tactical_prior_weight=args.self_play_tactical_prior_weight,
            rollout_value_weight=args.self_play_rollout_value_weight,
            rollout_limit=args.self_play_rollout_limit,
        ),
        temperature_moves=args.temperature_moves,
        epochs=args.epochs,
        batch_size=args.batch_size,
        replay_capacity=args.replay_capacity,
    )
    model.save(args.output)
    metrics["checkpoint"] = str(args.output)
    print(json.dumps(metrics, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
