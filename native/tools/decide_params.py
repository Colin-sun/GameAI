#!/usr/bin/env python3
# 调参代码
import argparse
import sys
from pathlib import Path

import optuna


def load_native_module():
    try:
        import gameai_native  # pylint: disable=import-error

        return gameai_native
    except ModuleNotFoundError:
        pass

    repo_root = Path(__file__).resolve().parents[2]
    module_dir = repo_root / "build" / "native" / "python"
    if module_dir.exists():
        sys.path.insert(0, str(module_dir))
        import gameai_native  # pylint: disable=import-error

        return gameai_native

    raise ModuleNotFoundError(
        "gameai_native is unavailable. Run `python -m pip install .` "
        "or build the Python bindings with CMake."
    )


GAMEAI_NATIVE = load_native_module()
DEFAULT_BASELINE = (6994, 0.62042)


# ---------- 2. 14 盘双循环 ----------
def win_rate(param1, param2, games_per_side=8, seed=20260512):
    """
    返回 [参数1胜率, 参数2胜率]，维度 2
    """
    n1, c1 = param1
    n2, c2 = param2
    score1, score2 = GAMEAI_NATIVE.compare_mcts(
        int(n1),
        float(c1),
        int(n2),
        float(c2),
        games_per_side=games_per_side,
        seed=seed,
    )
    total = score1 + score2
    return (score1 / total, score2 / total)

# ---------- 3. Optuna 目标函数 ----------
def build_objective(baseline, games_per_side, seed):
    def objective(trial: optuna.Trial):
        n = trial.suggest_int("n_playout", 200, 8000, log=True)
        c = trial.suggest_float("c_puct", 0.5, 5.0, log=True)
        param1 = (n, c)
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
    parser.add_argument("--seed", type=int, default=20260512, help="Base seed for self-play evaluation.")
    return parser.parse_args()

# ---------- 4. 启动调参 ----------
def main():
    args = parse_args()
    baseline = (args.baseline_n_playout, args.baseline_c_puct)
    study = optuna.create_study(
        direction="maximize",
        pruner=optuna.pruners.MedianPruner(n_startup_trials=5, n_warmup_steps=3),
    )
    study.optimize(
        build_objective(baseline, args.games_per_side, args.seed),
        n_trials=args.trials,
        show_progress_bar=True,
    )

    print("Best trial:")
    t = study.best_trial
    print(" value =", t.value)
    print(" params =", t.params)


if __name__ == "__main__":
    main()
