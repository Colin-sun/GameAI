# 多数 Meta 规则下的 MCTS 优化记录

本文只记录当前 `majority-utt-v1` 规则、当前 native 实现和本轮新生成的实验数据。所有参数和对局结果均以多数 meta 规则为前提。

## 当前结论

当前默认配置为：

```text
rollout_policy = tactical
rollout_limit  = 32
c_puct         = 0.3
n_playout      = 3000（benchmark）或 2000（humanplay）
```

在新规则下，`3000 / 0.4 / tactical / 32` 对 `3000 / 0.8 / uniform / 300` 的 8 局对照为 `7 胜 0 负 1 和`，得分率 `93.75%`。围绕当前 tactical baseline 的新筛选支持把 `c_puct` 从 `0.4` 调到 `0.3`，但样本仍不足以给出绝对 Elo 或稳定通用胜率。

## 默认规则

规则版本为 `majority-utt-v1`：

- 每个 3x3 小棋盘由横、竖、斜三连线决定胜者；填满且没有三连线时，该小棋盘记为平局。
- meta 棋盘只统计已经获胜的小棋盘数量；meta 棋盘的横、竖、斜排列不参与胜负判定。
- 平局小棋盘不计入多数门槛。门槛为 `floor((9 - 平局小棋盘数) / 2) + 1`，未结束的小棋盘仍计入可争夺数量。
- 任一方达到该门槛后即可结束整局；这是不可逆的多数领先。
- 所有小棋盘结束但双方都没有达到严格多数时，获胜小棋盘更多的一方获胜；数量相同为和棋。
- 目标小棋盘已经结束时，下一位可以在任意未结束小棋盘走子；终局后没有合法动作。

## 启发式策略

实现位于 `native/games/utt.cpp`、`native/games/base_game.h` 和 `native/mcts/mcts_pure.h`。

### Tactical rollout

`rollout_policy=1` 按以下顺序选择合法动作：

1. 当前玩家可以通过完成一个小棋盘达到 meta 多数的动作；
2. 阻断对手下一步达到 meta 多数的动作；
3. 当前玩家可以完成小棋盘的动作；
4. 阻断对手完成小棋盘的动作；
5. 没有战术动作时均匀随机选择。

`rollout_policy=0` 仍保留为均匀随机 baseline，但它与 tactical rollout 使用同一个 MCTS 核心，不是第二套旧 MCTS 实现。

### 叶子评估

截断 rollout 后，`UltimateTicTacToe::evaluate(player)` 使用：

- 双方已经赢得的 meta 小棋盘数量差作为主要信号；
- 未结束小棋盘中的单方一连、两连和对方对应威胁作为局部潜力；
- 终局仍返回 `+1/-1/0`，非终局值经过 `tanh` 压到 `[-1, 1]`。

评估函数不再使用 meta 棋盘排列、meta 两连或 meta 三连线奖励。

## 新规则下的实验

所有对局都让双方各执先手，并使用 native 混合 seed。表中的 W-D-L 均从 candidate 视角统计，得分为 `W + 0.5D`。

### Tactical 对 uniform

命令：

```shell
python scripts/evaluate_mcts.py \
  --candidate-n-playout 3000 --candidate-c-puct 0.4 \
  --candidate-rollout-policy tactical --candidate-rollout-limit 32 \
  --baseline-n-playout 3000 --baseline-c-puct 0.8 \
  --baseline-rollout-policy uniform --baseline-rollout-limit 300 \
  --games-per-side 2 --seeds 20260901,20260902 --json
```

结果：

| candidate W | baseline W | 和棋 | candidate 得分 | 得分率 | 严格胜率 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 7 | 0 | 1 | 7.5/8 | 93.75% | 87.5% |

严格胜率的 Wilson 95% 区间约为 `[52.9%, 97.8%]`。该结果说明多数规则下 tactical rollout 有明显相对收益，但局数仍然很小。

### `c_puct` 筛选

固定 `3000 / tactical / 32`，与 `c_puct=0.4` 对照；seed `20260907` 下双方各一局：

| candidate c_puct | candidate W | baseline W | 和棋 |
| ---: | ---: | ---: | ---: |
| 0.2 | 0 | 0 | 2 |
| 0.3 | 2 | 0 | 0 |
| 0.5 | 1 | 1 | 0 |
| 0.6 | 1 | 1 | 0 |
| 0.8 | 0 | 2 | 0 |

对 `c_puct=0.3` 的确认使用 seed `20260908,20260909`、共 8 局，结果为 `4-0-4`，得分率 `75%`。与筛选阶段合并为 `6-0-4`、10 局，得分率 `80%`。这是选择 `0.3` 的工程依据，不是最终统计结论。

在 humanplay 预算下，`2000 / 0.3 / tactical / 32` 对 `2000 / 0.4 / tactical / 32` 使用 seed `20260910,20260911` 共 8 局，结果为 `5-1-2`，得分率 `75%`。

### playout 与 rollout 长度

下列对照也在多数规则下重新运行：

| candidate | baseline | W-D-L | 得分率 |
| --- | --- | ---: | ---: |
| `6000 / 0.4 / tactical / 16` | `3000 / 0.4 / tactical / 32` | `2-1-5` / 8 | 56.25% |
| `4000 / 0.4 / tactical / 16` | `2000 / 0.4 / tactical / 32` | `4-3-1` / 8 | 56.25% |

新规则下这些加倍 playout、缩短 rollout 的组合没有显示稳定收益，因此不再作为默认配置。

## 参数入口

| 入口 | n_playout | c_puct | rollout | limit |
| --- | ---: | ---: | --- | ---: |
| benchmark | 3000 | 0.3 | tactical | 32 |
| humanplay | 2000 | 0.3 | tactical | 32 |
| Python `MCTSPure` API 默认 | 调用方指定 | 调用方指定 | tactical | 32 |
| 比较接口兼容 baseline 默认 | 调用方指定 | 调用方指定 | uniform | 300 |

`compare_mcts()` 和 `compare_mcts_detailed()` 的 uniform/300 默认值只用于兼容旧调用和 baseline 对照；仓库默认配置及调参脚本默认 baseline 已使用多数规则下的 tactical 参数。

## 验证

已执行：

```shell
cmake --build build/native -j2
python -m unittest discover -s tests -v
python -m py_compile scripts/common.py scripts/benchmark.py scripts/decide_params.py scripts/evaluate_mcts.py scripts/humanplay.py
python scripts/benchmark.py --config configs/benchmark/benchmark_config.json5 --runs 3 --seed 20260809
python scripts/decide_params.py --trials 1 --games-per-side 1 --seed 20260830
git diff --check
```

native Release build、6 项规则和 MCTS 测试、Python 编译检查、benchmark 和调参入口均通过。未运行 Android emulator/device；本轮修改集中在 native/Python 和规则文档。

最终配置空盘 `suggest_move()` 的三次平均耗时约为：benchmark `3000/0.3/tactical/32` 为 `243 ms`，humanplay `2000/0.3/tactical/32` 为 `161 ms`。该数据只用于性能量级回归，不代表完整对局棋力。

## 后续统计门槛

本轮数据只用于确认规则切换后的方向。发布前仍需使用未参与筛选的 seed 扩大到数百局，并加入可达的早、中、残局面；同时记录规则版本、完整参数、每局 seed、W/D/L、步数和耗时。没有这些数据，不应把本文百分比当作通用棋力等级。
