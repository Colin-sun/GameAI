#!/usr/bin/env python3
"""Evaluate a CUDA Torch AlphaZero model against native tactical MCTS."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import torch

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.alphazero import NeuralSearchConfig
from scripts.alphazero.torch_impl import TorchPolicyValueNetwork, evaluate_torch_model
from scripts.common import load_native_module, repo_root


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


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model",
        type=Path,
        default=root / "models" / "alphazero" / "utt_majority_v1_torch.pt",
    )
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--games-per-side", type=int, default=20)
    parser.add_argument("--seeds", type=parse_seeds, default=[20260871, 20260872])
    parser.add_argument("--neural-simulations", type=int, default=512)
    parser.add_argument("--neural-c-puct", type=float, default=0.3)
    parser.add_argument(
        "--candidate-mode",
        choices=("neural", "native-prior"),
        default="neural",
        help="Use Python neural MCTS or native tactical MCTS guided by model priors.",
    )
    parser.add_argument("--tactical-prior-weight", type=float, default=0.25)
    parser.add_argument("--rollout-value-weight", type=float, default=0.1)
    parser.add_argument("--rollout-limit", type=int, default=32)
    parser.add_argument("--inference-batch-size", type=int, default=32)
    parser.add_argument("--policy-exponent", type=float, default=1.0)
    parser.add_argument(
        "--native-root-selection",
        choices=("visits", "q"),
        default="visits",
        help="Native-prior candidate root action criterion.",
    )
    parser.add_argument(
        "--force-tactical",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Hard-filter immediate tactical root moves (default: enabled).",
    )
    parser.add_argument("--baseline-n-playout", type=int, default=3000)
    parser.add_argument("--baseline-c-puct", type=float, default=0.3)
    parser.add_argument("--output-json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.games_per_side <= 0 or args.neural_simulations <= 0:
        raise SystemExit("games-per-side and neural-simulations must be positive")
    if args.device.startswith("cuda") and not torch.cuda.is_available():
        raise SystemExit("CUDA was requested but torch.cuda.is_available() is false")
    model = TorchPolicyValueNetwork.load(args.model, device=args.device)
    result = evaluate_torch_model(
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
            force_tactical=args.force_tactical,
            inference_batch_size=args.inference_batch_size,
            policy_exponent=args.policy_exponent,
            root_selection=args.native_root_selection,
        ),
        baseline_n_playout=args.baseline_n_playout,
        baseline_c_puct=args.baseline_c_puct,
        candidate_mode=args.candidate_mode,
    )
    total = int(result["total_games"])
    result["win_rate_95ci"] = wilson_interval(int(result["candidate_wins"]), total)
    result["cuda_device"] = (
        torch.cuda.get_device_name(args.device) if str(args.device).startswith("cuda") else None
    )
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(encoded + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
