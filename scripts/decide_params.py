#!/usr/bin/env python3
# 调参代码
from __future__ import annotations

import argparse

import optuna

from common import load_native_module


GAMEAI_NATIVE = load_native_module()
DEFAULT_BASELINE = (3000, 0.3, 1, 32)


def normalize_config(config, default_policy=1, default_limit=32):
    if len(config) == 2:
        return (int(config[0]), float(config[1]), default_policy, default_limit)
    return (int(config[0]), float(config[1]), int(config[2]), int(config[3]))


def win_rate(param1, param2, games_per_side=8, seed=20260512):
    """
    返回 [参数1胜率, 参数2胜率]，维度 2
    """
    n1, c1, policy1, limit1 = normalize_config(param1)
    n2, c2, policy2, limit2 = normalize_config(param2, default_policy=1, default_limit=32)
    score1, score2 = GAMEAI_NATIVE.compare_mcts(
        int(n1),
        float(c1),
        int(n2),
        float(c2),
        games_per_side=games_per_side,
        seed=seed,
        rollout_policy1=policy1,
        rollout_limit1=limit1,
        rollout_policy2=policy2,
        rollout_limit2=limit2,
    )
    total = score1 + score2
    return (score1 / total, score2 / total)


def build_objective(baseline, games_per_side, seed, rollout_policy, rollout_limit):
    def objective(trial: optuna.Trial):
        n = trial.suggest_int("n_playout", 200, 8000, log=True)
        c = trial.suggest_float("c_puct", 0.1, 2.0, log=True)
        param1 = (n, c, rollout_policy, rollout_limit)
        rates = win_rate(
            param1,
            baseline,
            games_per_side=games_per_side,
            seed=seed + trial.number,
        )
        return rates[0]

    return objective


def parse_args():
    parser = argparse.ArgumentParser(description="Tune pure MCTS parameters with Optuna.")
    parser.add_argument("--trials", type=int, default=60, help="Number of Optuna trials.")
    parser.add_argument("--games-per-side", type=int, default=8, help="Matches played as each side.")
    parser.add_argument("--baseline-n-playout", type=int, default=DEFAULT_BASELINE[0], help="Baseline n_playout.")
    parser.add_argument("--baseline-c-puct", type=float, default=DEFAULT_BASELINE[1], help="Baseline c_puct.")
    parser.add_argument("--baseline-rollout-policy", type=int, choices=(0, 1), default=DEFAULT_BASELINE[2])
    parser.add_argument("--baseline-rollout-limit", type=int, default=DEFAULT_BASELINE[3])
    parser.add_argument("--rollout-policy", type=int, choices=(0, 1), default=1)
    parser.add_argument("--rollout-limit", type=int, default=32)
    parser.add_argument("--seed", type=int, default=20260512, help="Base seed for self-play evaluation.")
    return parser.parse_args()


def main():
    args = parse_args()
    baseline = (
        args.baseline_n_playout,
        args.baseline_c_puct,
        args.baseline_rollout_policy,
        args.baseline_rollout_limit,
    )
    study = optuna.create_study(
        direction="maximize",
        pruner=optuna.pruners.MedianPruner(n_startup_trials=5, n_warmup_steps=3),
    )
    study.optimize(
        build_objective(baseline, args.games_per_side, args.seed, args.rollout_policy, args.rollout_limit),
        n_trials=args.trials,
        show_progress_bar=True,
    )

    print("Best trial:")
    best_trial = study.best_trial
    print(" value =", best_trial.value)
    print(" params =", best_trial.params)


if __name__ == "__main__":
    main()
