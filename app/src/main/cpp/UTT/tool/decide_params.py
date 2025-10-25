#!/usr/bin/env python3
# 调参代码
import subprocess
import optuna
import numpy as np

# ---------- 2. 14 盘双循环 ----------
def win_rate(param1, param2):
    """
    返回 [参数1胜率, 参数2胜率]，维度 2
    """
    n1, c1 = param1
    n2, c2 = param2
    score1 = 0  # 参数1赢的盘数
    total = 16

    cmd = [
        "../build/tool/self_play",
        str(int(n1)), str(float(c1)),
        str(int(n2)), str(float(c2)),
    ]
    out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True)
    
    # 读出 win_count_param_1
    for line in out.splitlines()[::-1]:
        if line.startswith("win_count_param_1"):
            score1 = float(line.split("=")[1])
            return np.array([score1/total, 1-score1/total])
    raise RuntimeError("parse ailed, output:\n" + out)

# ---------- 3. Optuna 目标函数 ----------
# 我们让「参数1」当变量，「参数2」固定成 baseline
BASELINE = (6994, 0.62042)   # 随便给一组中等强度
# 实际给的是高强度

def objective(trial: optuna.Trial):
    n = trial.suggest_int("n_playout", 200, 8000, log=True)
    c = trial.suggest_float("c_puct", 0.5, 5.0, log=True)
    param1 = (n, c)
    rates = win_rate(param1, BASELINE)   # rates[0] 就是参数1对 baseline 的胜率
    return rates[0]   # Optuna 默认最大化

# ---------- 4. 启动调参 ----------
if __name__ == "__main__":
    study = optuna.create_study(
        direction="maximize",
        pruner=optuna.pruners.MedianPruner(n_startup_trials=5, n_warmup_steps=3)
    )
    study.optimize(objective, n_trials=60, show_progress_bar=True)

    print("Best trial:")
    t = study.best_trial
    print(" value =", t.value)
    print(" params =", t.params)