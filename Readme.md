# GameAI
基于 C++ 和 Android 的游戏 AI 项目。当前主线是终极井字棋；Linux 侧保留纯 MCTS 核心、benchmark、自对弈工具和 Python humanplay 脚本，Android 侧提供 JNI 包装和界面。

## 依赖与构建
建议先激活 `gameai` conda 环境：

```shell
conda activate gameai
```

如果环境是空的，先安装依赖：

```shell
conda install -n gameai -c conda-forge -y python=3.13 pip cmake jsoncpp ninja pybind11
python -m pip install .
```

安装完成后，Python 工具可直接运行；只有在需要 native 可执行文件时，才额外构建：

```shell
cmake -S native -B build/native -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_TOOL=ON -DENABLE_BENCHMARK=ON -DENABLE_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j
```

可运行的主命令：

```shell
python ./native/tools/decide_params.py --trials 10 --games-per-side 2
python ./scripts/humanplay.py --config ./configs/humanplay/humanplay_config.json5
./build/native/benchmark/benchmark -config ./configs/benchmark/benchmark_config.json5
./build/native/tools/self_play 1000 0.8 2000 0.8
```

Python 脚本依赖和 `gameai_native` 绑定统一由根目录 `pyproject.toml` + `setup.py` 管理，`python -m pip install .` 后即可直接运行 `native/tools/decide_params.py` 和 `scripts/humanplay.py`，不需要再手动执行 CMake。
不要使用 `pip install -e .` 或 inplace 扩展构建，否则会偏离当前统一的 `build/` 目录约定。

## 目录
- `native/`: Linux 侧 C++ 核心、benchmark、tools、Python bindings。
- `app/src/main/cpp/`: Android JNI 入口，`android/mcts_pure_bridge.cpp` 连接原生 MCTS。
- `configs/`: 各模块 JSON5 配置。
- `img/`: README 图片资源。
- `scripts/`: Python 脚本入口，当前包含终端版 humanplay。

## 说明
当前仓库仅保留 pure MCTS、benchmark、self-play、Python humanplay 和 Android JNI 主线。Android 构建仍可在 Android Studio 中导入 `app/` 后运行；本次默认优先验证 native 与 Python 路径，不额外跑 Android。
