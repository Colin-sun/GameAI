# Torch AlphaZero 实验记录

本文只记录 `majority-utt-v1` 规则下、使用 Torch 网络和 native-prior 搜索的最终实验。meta 棋盘三连线不参与胜负。

## 规则

- 小棋盘仍由普通横、竖、斜三连线决定胜者；填满且没有三连线时为平局。
- meta 棋盘只统计双方已经赢得的小棋盘数量，不检查 meta 横、竖、斜三连线。
- 平局小棋盘不计入多数；达到不可逆严格多数时立即结束。
- 所有小棋盘结束后，获胜小棋盘更多者获胜，数量相同为和棋。

规则版本由 native、启发式、训练 target 和评测统一使用 `majority-utt-v1`。

## 训练

最终 checkpoint：`models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt`

| 项目 | 参数/结果 |
| --- | --- |
| 网络 | 128 channels × 8 residual blocks |
| GPU | RTX A4000, Torch `2.13.0+cu130` |
| teacher | 512 局 × 6000 playout, `c_puct=0.3`, 32 CPU spawn workers |
| teacher target | hard one-hot root policy，最终胜负 value |
| teacher 局面 | 25,119 raw，200,952 个 8 倍增强局面 |
| 最终训练集 | 50,238 raw，401,904 个增强位置 |
| 训练 | warmup 8 epochs + final 12 epochs，CUDA batch 1024 |
| final loss | policy `0.3808`，value `0.0621` |

训练使用多进程 CPU 生成 native tactical teacher 数据，GPU 只负责 Torch batch 训练。checkpoint 的 rule metadata 为 `majority-utt-v1`。
最终 40 局 validation 使用同一 checkpoint，在 32 个 CPU worker 中并行运行；每个 worker 的 Torch 推理和 native MCTS 均限制为单线程。

## 最终搜索参数

candidate 使用网络只提供根节点 prior，搜索和 rollout 仍由 native tactical MCTS 执行：

```text
native-prior
simulations=48000
c_puct=0.2
tactical_prior_weight=0
rollout_value_weight=0
rollout_limit=32
policy_exponent=0.5
force_tactical=false
root_selection=q
```

`root_selection=q` 先按根子节点 Q 选择，Q 相同再按访问次数选择；它在和棋较多的 majority 对局中优于单纯 visits 选择。baseline 保持原 tactical 配置：`3000 / c_puct=0.3 / rollout=32`。

## 棋力评测

双方各执先手，W-D-L 从 candidate 视角统计。最终 validation 使用 10 个 seed、每个 seed 双方各一局，共 40 局；逐局记录保存在 [`alphazero_torch_native_prior_teacher6000_512_hard_rootq_48000_parallel_40_cpu.json`](alphazero_torch_native_prior_teacher6000_512_hard_rootq_48000_parallel_40_cpu.json) 中：

| 评测批次 | 对局 | W-D-L | 严格胜率 | 得分率 |
| --- | ---: | ---: | ---: | ---: |
| CPU worker validation | 40 | 32-7-1 | **80.00%** | **88.75%** |

严格胜率的 Wilson 95% 区间为 `65.24%–89.50%`。因此当前 40 局数据的点估计达到 80%，但仍不能把 80% 视为稳定总体水平；majority 规则下和棋也应单独报告。

### 降低 simulations 的完整验证

为评估降低搜索预算的实际收益，使用同一个 hard checkpoint、同一组 10 个 seed、每组双方各 20 局，运行了完整的 8 组矩阵，共 320 局。所有 native-prior 配置均固定为 `c_puct=0.2`、`policy_exponent=0.5`、`rollout_limit=32`、`root_selection=q`；tactical 对手固定为 `3000 / c_puct=0.3 / tactical / 32`。高低参数对战中，表格的 candidate 始终是 `48000`。

| 对战 | 对局 | W-D-L | 严格胜率 | 得分率 |
| --- | ---: | ---: | ---: | ---: |
| `24000` vs tactical `3000` | 40 | 30-9-1 | 75.00% | **86.25%** |
| `12000` vs tactical `3000` | 40 | 32-8-0 | **80.00%** | **90.00%** |
| `6000` vs tactical `3000` | 40 | 23-13-4 | 57.50% | 73.75% |
| `3000` vs tactical `3000` | 40 | 20-14-6 | 50.00% | 67.50% |
| `48000` vs `24000` | 40 | 13-26-1 | 32.50% | 65.00% |
| `48000` vs `12000` | 40 | 14-23-3 | 35.00% | 63.75% |
| `48000` vs `6000` | 40 | 24-16-0 | 60.00% | 80.00% |
| `48000` vs `3000` | 40 | 29-10-1 | 72.50% | 85.00% |

W-D-L 和严格胜率均从 candidate 视角统计；高低参数组的 candidate 是 `48000`。各组严格胜率的 Wilson 95% 区间、每局 seed 和完整走子记录见 [`alphazero_torch_step_reduction_parallel_8x40_cpu.json`](alphazero_torch_step_reduction_parallel_8x40_cpu.json)。本次 32 worker CPU 批次墙钟时间为约 `537.6 s`。

结论是：`12000` 在对 tactical 的完整验证中达到了与 `48000` 当前基准相同的 80% 严格胜率，同时走子时间约降至四分之一；`6000` 延迟更低但棋力明显下降。`48000` 对 `6000/3000` 的得分优势较清楚，但对 `24000/12000` 主要表现为更多和棋，不能据此宣称 simulations 越高棋力必然越强。

## 复现

训练 hard checkpoint：

```shell
python -m scripts.alphazero.train_torch \
  --output models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt \
  --resume models/alphazero/utt_majority_v1_torch_teacher6000_512.pt \
  --device cuda --channels 128 --blocks 8 --workers 32 \
  --teacher-games 512 --teacher-playout 6000 \
  --teacher-policy-exponent 1 --teacher-hard-policy \
  --self-play-games 0 --warmup-epochs 8 --epochs 12 --batch-size 1024
```

最终 40 局并行评测：

```shell
python -m scripts.alphazero.evaluate_torch_parallel \
  --model models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt \
  --workers 32 --games-per-side 2 \
  --seeds 20260900,20260901,20260902,20260903,20260904,20260905,20260906,20260907,20260908,20260909 \
  --simulations 48000 --c-puct 0.2 \
  --rollout-limit 32 --policy-exponent 0.5 \
  --baseline-n-playout 3000 --baseline-c-puct 0.3 \
  --output-json docs/dev/analysis/alphazero_torch_native_prior_teacher6000_512_hard_rootq_48000_parallel_40_cpu.json
```

完整降档矩阵：

```shell
python -m scripts.alphazero.evaluate_torch_matrix_parallel \
  --model models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt \
  --workers 32 --games-per-side 2 \
  --seeds 20260900,20260901,20260902,20260903,20260904,20260905,20260906,20260907,20260908,20260909 \
  --low-simulations 24000,12000,6000,3000 --high-simulations 48000 \
  --c-puct 0.2 --policy-exponent 0.5 --rollout-limit 32 \
  --tactical-n-playout 3000 --tactical-c-puct 0.3 --tactical-rollout-limit 32 \
  --output-json docs/dev/analysis/alphazero_torch_step_reduction_parallel_8x40_cpu.json
```

该模型仍是实验旁路；Android 默认入口继续使用 native tactical MCTS。若要把网络作为生产默认，还需要更大规模、覆盖不同阶段局面的独立评测。
