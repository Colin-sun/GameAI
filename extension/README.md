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

默认输出是 `extension/models/utt_majority_v1_torch_teacher6000_512_hard.bin`。Chrome 端不需要 Python 或 Torch，native-prior 首次搜索时按需加载这个转换后的 Float32 文件。

打包发布版本执行：

```bash
npm run package:extension
```

命令只生成标准的 `dist/gameai-extension.crx`，由纯 Python CRX3 打包器完成，不依赖 Chrome/Chromium。Chrome 没有 `ctx` 扩展包格式，因此这里按标准 `.crx` 处理 `ctx` 的需求。需要调试用 ZIP 时执行 `npm run package:extension:zip`。

WASM 搜索资源已经包含在扩展目录中。若重新编译，执行：

```bash
npm run build:extension:wasm
```

编译器需要 Emscripten；产物是 `extension/wasm/engine.wasm`。MV3 manifest 已为扩展页声明 `wasm-unsafe-eval`，否则 Chrome 的默认 CSP 会拒绝实例化 WASM。

然后在 Chrome 中：

1. 打开 `chrome://extensions` 并开启“开发者模式”。
2. 点击“加载已解压的扩展程序”，选择仓库中的 `extension/` 目录。
3. 打开 `https://game.hullqin.cn/jzq?p=`，右下角浮动面板即可使用。

页面使用 `r=1` 时，扩展会提示切换到 `r=0`，因为 `r=1` 的网页规则与本项目的 majority 规则不一致。

## 三种模式

| 难度 | 内部模式 | 默认参数 | 行为 |
| --- | --- | --- | --- |
| 简单（均匀搜索） | `uniform` | `simulations=6200`, `c_puct=0.8`, `rollout_limit=300` | 均匀先验、均匀随机 rollout。 |
| 中等（战术搜索） | `tactical` | `simulations=3000`, `c_puct=0.4`, `rollout_limit=32` | 优先处理立即多数胜、阻止对手多数胜和局部成棋；这是面板初始默认难度。 |
| 困难（AI 模型） | `native-prior` | `simulations=12000`, `c_puct=0.2`, `policy_exponent=0.5`, `rollout_limit=32`, `root_selection=q` | 使用 Torch 策略先验，在 WASM 内完成 CNN 推理和 MCTS。 |

面板还支持 AI 先手/后手、自动人机、手动搜索建议、建议着 overlay、采用建议、停止搜索和搜索参数持久化。

### WASM 与 fallback

搜索和 native-prior 推理默认走 `extension/wasm/engine.wasm`。如果浏览器不支持 WASM、CSP/资源加载失败、模型长度不匹配或 WASM 运行出错，扩展会切换到 JavaScript fallback；面板会明确显示 `JS fallback：<原因>`，搜索结果也会带 `backend=js-fallback`，不会把 fallback 伪装成 WASM 性能结果。fallback 主要用于兼容和诊断，1 秒性能门禁只对 `backend=wasm` 计入。

## 性能 profile

profile 使用单线程 Node 24、空棋盘和新默认参数。原 JS tactical profile 的主要热点是 `completesLocalBoard`、`localBoardState` 和 `winsGameWithAction`，因为每个 tactical 候选都会重复扫描完整 3x3 小盘；WASM 后端改为固定状态、紧凑节点和只检查受影响线。

| 后端/参数 | 耗时 |
| --- | ---: |
| 原 JS `6200 / 0.8 / uniform / 300` | 约 746 ms |
| 原 JS `3000 / 0.4 / tactical / 32` | 约 2091 ms |
| WASM `6200 / 0.8 / uniform / 300` | 约 61 ms |
| WASM `3000 / 0.4 / tactical / 32` | 约 75 ms |
| WASM native-prior `12000 / 0.2 / 0.5 / 32 / q` | 约 506 ms（含推理） |

这些是当前测试机的基线，不替代目标机器上的测量。最近一次真实 Chromium fixture 测量的 WASM 搜索耗时为 uniform `84 ms`、tactical `95 ms`、native-prior `622 ms`；页面端墙钟分别约为 `132 ms`、`155 ms`、`673 ms`。E2E 会读取面板显示的搜索耗时，并对三组 WASM 预设执行 `<1000 ms` 门禁。

## 测试

先安装 Node 测试依赖：

```bash
npm install
```

推荐按以下顺序执行：

```bash
npm run build:extension:wasm
npm run test:extension
npm run test:extension:e2e
python -m unittest discover -s tests -v
```

测试覆盖范围：

- JavaScript 引擎：网页/native 坐标转换、`p` 序列、合法动作、局部棋盘和 strict-majority 终局、三种搜索模式和旧模式别名。
- WASM 运行时：Float32 CNN 输出与 JS 模型误差、三组预设合法落子、tactical 默认单核 `<1s`，以及节点/搜索状态导出。
- fallback 合同：没有 WASM 地址的 worker 请求必须发送 fallback 事件，并在结果中标记 `js-fallback` 和原因。
- 真实 Chromium：MV3 CSP/WASM 加载、service worker 搜索链路、三种模式默认参数和 `<1s` 实测、搜索期间点击锁、建议 overlay、自动 AI 落子、SPA 回退/重放和联机房间隔离。
- Python/native 回归：Android/native 共享引擎、AlphaZero 工具和 `majority-utt-v1` 测试。

`test:extension:e2e` 使用 Playwright 的真实 Chromium，并在测试中拦截网页为确定性 fixture，因此不依赖线上棋盘状态；若环境尚未安装浏览器，可执行 `npx playwright install chromium`。

发布前建议在目标设备重复至少 30 次，分别覆盖空盘、早期、中局和残局局面，记录 WASM/fallback、冷启动/热启动、p50/p95、模型加载时间和动作合法性；任何 fallback 或 p95 超过 1 秒都应单独标记，不能混入 WASM 通过率。
