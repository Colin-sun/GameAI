# GameAI
基于 C++、libtorch 和 Android 的游戏 AI 项目。当前主线是终极井字棋，Linux 侧的纯 MCTS、benchmark 和自对弈工具已可运行；Android 侧提供 JNI 包装和界面。

## 依赖与构建
建议先激活 `gameai` conda 环境：

```shell
conda activate gameai
```

如果环境是空的，先安装依赖：

```shell
conda install -n gameai -c conda-forge -y python=3.13 cmake jsoncpp ninja
```

随后在仓库根目录构建 native 侧：

```shell
cmake -S native -B native/build -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_TOOL=ON -DENABLE_BENCHMARK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build native/build -j
```

可运行的主命令：

```shell
./native/build/humanplay/gameplay -config ./configs/humanplay/humanplay_config.json5
./native/build/benchmark/benchmark -config ./configs/benchmark/benchmark_config.json5
./native/build/tools/self_play 1000 0.8 2000 0.8
```

`native/tools/decide_params.py` 需要额外的 Python 依赖，按 `native/tools/requirements.txt` 安装即可。

## 目录
- `native/`: Linux 侧 C++ 核心、humanplay、benchmark、tools。
- `app/src/main/cpp/`: Android JNI 入口，`android/mcts_pure_bridge.cpp` 连接原生 MCTS。
- `configs/`: 各模块 JSON5 配置。
- `img/`: README 图片资源。
- `scripts/`: 预留的脚本目录。

## 说明
当前仓库仅保留 pure MCTS、benchmark、self-play 和 Android JNI 主线。Android 构建仍可在 Android Studio 中导入 `app/` 后运行。
