# Torch AlphaZero 实验记录

本文只记录 `majority-utt-v1` 规则下、使用 CUDA Torch 网络和 native-prior 搜索的最终实验。meta 棋盘三连线不参与胜负。

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

双方各执先手，W-D-L 从 candidate 视角统计。以下两批 seed 在参数确定后独立运行，逐局记录保存在 JSON 中：

| 评测批次 | 对局 | W-D-L | 严格胜率 | 得分率 |
| --- | ---: | ---: | ---: | ---: |
| `...hard_rootq_48000_independent_20.json` | 20 | 18-2-0 | 90.00% | 95.00% |
| `...hard_rootq_48000_independent2_20.json` | 20 | 15-5-0 | 75.00% | 87.50% |
| 合计 | 40 | 33-7-0 | **82.50%** | **91.25%** |

合计严格胜率的 Wilson 95% 区间为 `68.05%–91.25%`。因此当前数据支持“point estimate 超过 80%”，但不支持把 90% 宣称为稳定总体水平；单批最高为 90%。最终 aggregate 数据见 [`alphazero_torch_final_eval.json`](alphazero_torch_final_eval.json)。

## 复现

训练 hard checkpoint：

```shell
python scripts/train_alphazero_torch.py \
  --output models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt \
  --resume models/alphazero/utt_majority_v1_torch_teacher6000_512.pt \
  --device cuda --channels 128 --blocks 8 --workers 32 \
  --teacher-games 512 --teacher-playout 6000 \
  --teacher-policy-exponent 1 --teacher-hard-policy \
  --self-play-games 0 --warmup-epochs 8 --epochs 12 --batch-size 1024
```

最终评测：

```shell
python scripts/evaluate_alphazero_torch.py \
  --model models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt \
  --device cuda --games-per-side 2 \
  --seeds 20260900,20260901,20260902,20260903,20260904 \
  --neural-simulations 48000 --neural-c-puct 0.2 \
  --candidate-mode native-prior --tactical-prior-weight 0 \
  --rollout-value-weight 0 --rollout-limit 32 --policy-exponent 0.5 \
  --no-force-tactical --native-root-selection q \
  --baseline-n-playout 3000 --baseline-c-puct 0.3 \
  --output-json docs/dev/analysis/alphazero_torch_native_prior_teacher6000_512_hard_rootq_48000_independent_20.json
```

该模型仍是实验旁路；Android 默认入口继续使用 native tactical MCTS。若要把网络作为生产默认，还需要更大规模、覆盖不同阶段局面的独立评测。
