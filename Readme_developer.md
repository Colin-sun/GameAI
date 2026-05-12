# GameAI Developer Notes

## 当前结构
- `native/`: 纯原生入口，`native/CMakeLists.txt` 负责 Linux 构建。
- `app/src/main/cpp/CMakeLists.txt`: Android JNI 入口，内部把 `native/` 作为子目录引入。
- `native/core/`: 共用棋类逻辑、MCTS 和 JSON5 解析。
- `native/python/`: `pybind11` 绑定，导出 `gameai_native` 模块给 Python 脚本调用。
- `native/benchmark/`、`native/tools/`、`scripts/`: 当前主要验证路径。
- 根目录 `pyproject.toml`: Python 直接依赖清单。

## 推荐环境
先进入 `gameai` conda 环境。Python tools 只需要安装项目本身；benchmark 和 self-play 可执行文件才需要额外 CMake 构建：

```shell
conda activate gameai
conda install -n gameai -c conda-forge -y python=3.13 pip cmake jsoncpp ninja pybind11
python -m pip install .
```

如果需要 native 可执行文件，再执行：

```shell
cmake -S native -B build/native -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_TOOL=ON -DENABLE_BENCHMARK=ON -DENABLE_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j
```

不要使用 `pip install -e .`。当前仓库明确禁止把 `gameai_native` 直接落到仓库根目录，Python 安装构建统一走根目录 `build/`。

## 验证顺序
优先验证这三条路径：
- `python ./native/tools/decide_params.py --trials 1 --games-per-side 1`
- `python ./scripts/humanplay.py --config ./configs/humanplay/humanplay_config.json5`
- `./build/native/benchmark/benchmark -config ./configs/benchmark/benchmark_config.json5`
- `./build/native/tools/self_play 20 0.8 20 0.8`

## 代码约定
- 新增 JSON5 解析代码时，继续使用 `native/core/json5cpp.h`，不要写死系统绝对路径。
- MCTS 相关代码放在 `native/core/MCTS/`，复用树时要避免父子 `shared_ptr` 引用环。
- Python humanplay 只放在 `scripts/`，核心博弈和搜索逻辑仍保持在 `native/core/`。
- Android 只维护 JNI 包装层，不在这里放 Linux 调试逻辑。

## 备注
当前仓库不再保留 `native/train/` 和 `native/infer/` 的源码入口；`native/humanplay/` 也已被 Python 脚本替代。
