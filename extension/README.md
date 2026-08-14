# GameAI Chrome Extension

这个 MV3 扩展把 Android/native 方案移植到 `https://game.hullqin.cn/jzq` 的本地对战页面，使用 `majority-utt-v1` 规则：局部棋盘由三连或局部和棋决定，meta 棋盘只统计已赢局部棋盘的严格多数，不使用 meta 三连作为胜负条件。

扩展只接管 `/jzq` 和 `/jzq/`，不会接管 `/jzq/<room>` 联机房间。

## 安装

在仓库根目录执行一次模型转换。转换器会校验指定 Torch checkpoint 的规则、网络形状和 Float32 权重，并生成浏览器可直接读取的资源：

```bash
python scripts/extension/export_prior_model.py --force --json
```

默认输入是：

```text
models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt
```

默认输出是 `extension/models/utt_majority_v1_torch_teacher6000_512_hard.bin`。Chrome 端不需要 Python 或 Torch，Prior model 首次搜索时按需加载这个转换后的 Float32 文件。

然后在 Chrome 中：

1. 打开 `chrome://extensions` 并开启“开发者模式”。
2. 点击“加载已解压的扩展程序”，选择仓库中的 `extension/` 目录。
3. 打开 `https://game.hullqin.cn/jzq?p=`，右下角浮动面板即可使用。

页面使用 `r=1` 时，扩展会提示切换到 `r=0`，因为 `r=1` 的网页规则与本项目的 majority 规则不一致。

## 三种模式

| 模式 | 行为 |
| --- | --- |
| Pure MCTS | 均匀先验，均匀随机 rollout，根节点按访问次数选择。 |
| Tactical MCTS | 均匀先验，rollout 优先处理立即多数胜、阻止对手多数胜和局部成棋。 |
| Prior model | 使用 `utt_majority_v1_torch_teacher6000_512_hard.pt` 导出的策略先验，并按根节点 Q 值选择。模型 metadata 必须匹配 `majority-utt-v1`、10 个输入平面和 81 个动作。 |

面板还支持 AI 先手/后手、自动人机、手动搜索建议、建议着 overlay、采用建议、停止搜索和搜索参数持久化。

## 测试

先安装 Node 测试依赖：

```bash
npm install
```

推荐按以下顺序执行：

```bash
npm run test:extension
npm run test:extension:e2e
python -m unittest discover -s tests -v
```

测试覆盖范围：

- JavaScript 引擎：网页/native 坐标转换、`p` 序列、合法动作、局部棋盘和 strict-majority 终局、Pure/Tactical/Prior 三种搜索。
- 模型运行时：GAI1 binary metadata、网络输出形状、模型规则校验和浏览器 CPU 推理。
- 真实 Chromium：MV3 加载、service worker 搜索链路、三种模式建议、搜索期间点击锁、建议 overlay、自动 AI 落子、SPA 回退/重放和联机房间隔离。
- Python/native 回归：Android/native 共享引擎、AlphaZero 工具和 `majority-utt-v1` 测试。

`test:extension:e2e` 使用 Playwright 的真实 Chromium，并在测试中拦截网页为确定性 fixture，因此不依赖线上棋盘状态；若环境尚未安装浏览器，可执行 `npx playwright install chromium`。
