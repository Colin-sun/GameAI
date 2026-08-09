#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time
from pathlib import Path

from common import load_json5_config, load_native_module, repo_root


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Benchmark pure MCTS on an empty UTT board.")
    parser.add_argument(
        "--config",
        type=Path,
        default=root / "configs" / "benchmark" / "benchmark_config.json5",
        help="Path to the JSON5 config file.",
    )
    parser.add_argument("--runs", type=int, default=10, help="Number of repeated benchmark runs.")
    parser.add_argument("--seed", type=int, default=20260512, help="Seed for the MCTS engine.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    config = load_json5_config(args.config)
    gameai_native = load_native_module()

    n_playout = int(config["mcts"]["n_playout"])
    c_puct = float(config["mcts"]["c_puct"])
    rollout_policy = config["mcts"].get("rollout_policy", "tactical")
    policy_id = {"uniform": 0, "tactical": 1}.get(rollout_policy, rollout_policy)
    policy_id = int(policy_id)
    rollout_limit = int(config["mcts"].get("rollout_limit", 32 if policy_id else 300))

    print(f"读取配置文件: {args.config}")
    print("初始化完成，开始测试")

    board = gameai_native.UltimateTicTacToe()
    aiplayer = gameai_native.MCTSPure(
        n_playout=n_playout,
        c_puct=c_puct,
        seed=args.seed,
        rollout_policy=policy_id,
        rollout_limit=rollout_limit,
    )

    for run_index in range(args.runs):
        start = time.perf_counter()
        best_index = aiplayer.suggest_move(board)
        elapsed_ms = int((time.perf_counter() - start) * 1000)
        print(f"第{run_index}次测试，耗时: {elapsed_ms}ms")
        print(f"最佳动作: {best_index}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
