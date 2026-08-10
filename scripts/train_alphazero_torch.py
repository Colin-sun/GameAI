#!/usr/bin/env python3
"""Train the CUDA AlphaZero-style UTT network.

CPU workers generate native tactical-teacher games and optional neural
self-play.  Only the parent process touches CUDA, so the command scales CPU
data generation without duplicating GPU memory.
"""

from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

import torch

try:
    from alphazero import NeuralSearchConfig
    from alphazero_torch import (
        TorchPolicyValueNetwork,
        augment_symmetries,
        combine_game_data,
        generate_neural_self_play_data,
        generate_teacher_data,
        sharpen_policy_targets,
        train_network,
    )
    from common import repo_root
except ImportError:  # pragma: no cover - supports package-style invocation
    from scripts.alphazero import NeuralSearchConfig
    from scripts.alphazero_torch import (
        TorchPolicyValueNetwork,
        augment_symmetries,
        combine_game_data,
        generate_neural_self_play_data,
        generate_teacher_data,
        sharpen_policy_targets,
        train_network,
    )
    from scripts.common import repo_root


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "models" / "alphazero" / "utt_majority_v1_torch.pt",
    )
    parser.add_argument(
        "--resume",
        type=Path,
        help="Load an existing Torch checkpoint before generating new data.",
    )
    parser.add_argument("--seed", type=int, default=20260810)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--channels", type=int, default=128)
    parser.add_argument("--blocks", type=int, default=8)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--teacher-games", type=int, default=256)
    parser.add_argument("--teacher-playout", type=int, default=256)
    parser.add_argument("--teacher-c-puct", type=float, default=0.3)
    parser.add_argument(
        "--teacher-tree-value-weight",
        type=float,
        default=0.35,
        help="Blend native root Q into final-outcome value targets.",
    )
    parser.add_argument(
        "--teacher-policy-exponent",
        type=float,
        default=1.0,
        help="Raise teacher visit probabilities to this power before training.",
    )
    parser.add_argument(
        "--teacher-hard-policy",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Use the teacher root action as a one-hot policy target.",
    )
    parser.add_argument(
        "--teacher-repeat",
        type=int,
        default=1,
        help="Repeat teacher positions when mixing them with self-play data.",
    )
    parser.add_argument("--max-raw-positions", type=int, default=0)
    parser.add_argument("--warmup-epochs", type=int, default=8)
    parser.add_argument("--self-play-games", type=int, default=16)
    parser.add_argument("--self-play-simulations", type=int, default=64)
    parser.add_argument("--self-play-c-puct", type=float, default=0.3)
    parser.add_argument("--self-play-tactical-prior-weight", type=float, default=0.2)
    parser.add_argument("--self-play-rollout-value-weight", type=float, default=0.1)
    parser.add_argument("--self-play-rollout-limit", type=int, default=32)
    parser.add_argument("--self-play-inference-batch-size", type=int, default=16)
    parser.add_argument("--self-play-policy-exponent", type=float, default=1.0)
    parser.add_argument(
        "--self-play-force-tactical",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Hard-filter immediate tactical moves during self-play.",
    )
    parser.add_argument("--temperature-moves", type=int, default=12)
    parser.add_argument("--epochs", type=int, default=12)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--weight-decay", type=float, default=1.0e-4)
    parser.add_argument("--policy-weight", type=float, default=1.0)
    parser.add_argument("--value-weight", type=float, default=1.0)
    return parser.parse_args()


def _wdl(winners: list[int]) -> dict[str, int]:
    return {
        "player_one_wins": sum(int(winner) == 1 for winner in winners),
        "player_two_wins": sum(int(winner) == 2 for winner in winners),
        "draws": sum(int(winner) == -1 for winner in winners),
    }


def main() -> int:
    args = parse_args()
    if min(
        args.channels,
        args.blocks,
        args.workers,
        args.teacher_games,
        args.teacher_playout,
        args.teacher_repeat,
        args.warmup_epochs,
        args.self_play_games,
        args.self_play_simulations,
        args.epochs,
        args.batch_size,
    ) < 0:
        raise SystemExit("training counts and dimensions cannot be negative")
    if args.workers == 0 or args.teacher_playout == 0 or args.batch_size == 0:
        raise SystemExit("workers, teacher playout, and batch size must be positive")
    if args.teacher_policy_exponent <= 0.0 or not torch.isfinite(
        torch.tensor(args.teacher_policy_exponent)
    ):
        raise SystemExit("teacher-policy-exponent must be finite and positive")
    if args.device.startswith("cuda") and not torch.cuda.is_available():
        raise SystemExit("CUDA was requested but torch.cuda.is_available() is false")

    device = torch.device(args.device)
    if args.resume:
        model = TorchPolicyValueNetwork.load(args.resume, device="cpu")
        args.channels = model.channels
        args.blocks = model.blocks
    else:
        model = TorchPolicyValueNetwork(channels=args.channels, blocks=args.blocks)

    teacher = generate_teacher_data(
        games=args.teacher_games,
        n_playout=args.teacher_playout,
        c_puct=args.teacher_c_puct,
        seed=args.seed + 1000,
        workers=args.workers,
        tree_value_weight=args.teacher_tree_value_weight,
    )
    teacher = sharpen_policy_targets(
        teacher,
        exponent=args.teacher_policy_exponent,
        hard=args.teacher_hard_policy,
    )
    teacher_augmented = augment_symmetries(
        teacher,
        max_positions=args.max_raw_positions,
        seed=args.seed + 2000,
    )
    warmup_metrics = []
    if teacher_augmented.positions and args.warmup_epochs > 0:
        warmup_metrics = train_network(
            model,
            teacher_augmented,
            device=device,
            epochs=args.warmup_epochs,
            batch_size=args.batch_size,
            learning_rate=args.learning_rate,
            weight_decay=args.weight_decay,
            seed=args.seed + 3000,
            policy_weight=args.policy_weight,
            value_weight=args.value_weight,
        )

    self_play = teacher
    self_play_metrics: dict[str, object] = {
        "games": 0,
        "positions": 0,
        "wdl": {"player_one_wins": 0, "player_two_wins": 0, "draws": 0},
    }
    if args.self_play_games > 0:
        with tempfile.TemporaryDirectory(prefix="gameai-alphazero-") as temporary:
            warmup_path = Path(temporary) / "warmup.pt"
            model.save(warmup_path, {"stage": "warmup"})
            self_play = generate_neural_self_play_data(
                checkpoint=warmup_path,
                games=args.self_play_games,
                config=NeuralSearchConfig(
                    simulations=args.self_play_simulations,
                    c_puct=args.self_play_c_puct,
                    dirichlet_epsilon=0.25,
                    tactical_prior_weight=args.self_play_tactical_prior_weight,
                    rollout_value_weight=args.self_play_rollout_value_weight,
                    rollout_limit=args.self_play_rollout_limit,
                    force_tactical=args.self_play_force_tactical,
                    inference_batch_size=args.self_play_inference_batch_size,
                    policy_exponent=args.self_play_policy_exponent,
                ),
                seed=args.seed + 4000,
                workers=args.workers,
                temperature_moves=args.temperature_moves,
            )
        self_play_metrics = {
            "games": args.self_play_games,
            "positions": self_play.positions,
            "wdl": _wdl(self_play.winners),
        }

    combined = combine_game_data([teacher] * args.teacher_repeat + [self_play])
    training_data = augment_symmetries(
        combined,
        max_positions=args.max_raw_positions,
        seed=args.seed + 5000,
    )
    if not training_data.positions:
        raise SystemExit("no training positions were generated")
    final_metrics = train_network(
        model,
        training_data,
        device=device,
        epochs=args.epochs,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        weight_decay=args.weight_decay,
        seed=args.seed + 6000,
        policy_weight=args.policy_weight,
        value_weight=args.value_weight,
    )
    model.save(
        args.output,
        {
            "stage": "final",
            "device": str(device),
            "resume": str(args.resume) if args.resume else None,
            "teacher_games": args.teacher_games,
            "teacher_playout": args.teacher_playout,
            "teacher_policy_exponent": args.teacher_policy_exponent,
            "teacher_hard_policy": args.teacher_hard_policy,
            "self_play_games": args.self_play_games,
            "raw_positions": combined.positions,
            "training_positions": training_data.positions,
        },
    )

    result = {
        "rule_version": "majority-utt-v1",
        "checkpoint": str(args.output),
        "device": str(device),
        "cuda_device": torch.cuda.get_device_name(device) if device.type == "cuda" else None,
        "network": {"channels": args.channels, "blocks": args.blocks},
        "workers": args.workers,
        "teacher": {
            "games": args.teacher_games,
            "playout": args.teacher_playout,
            "tree_value_weight": args.teacher_tree_value_weight,
            "policy_exponent": args.teacher_policy_exponent,
            "hard_policy": args.teacher_hard_policy,
            "positions": teacher.positions,
            "training_positions": teacher_augmented.positions,
            "wdl": _wdl(teacher.winners),
        },
        "self_play": self_play_metrics,
        "combined_raw_positions": combined.positions,
        "combined_training_positions": training_data.positions,
        "warmup_metrics": warmup_metrics,
        "final_metrics": final_metrics,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
