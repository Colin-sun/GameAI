# GameAI Developer Notes

## 当前结构
- `native/`: 纯原生入口，`native/CMakeLists.txt` 负责 Linux 构建。
- `app/src/main/cpp/CMakeLists.txt`: Android JNI 入口，内部把 `native/` 作为子目录引入。
- `native/core/`: 共用棋类逻辑和 MCTS。
- `native/python/`: `pybind11` 绑定，导出 `gameai_native` 模块给 Python 脚本调用。
- `scripts/`: benchmark、调参和 humanplay 的统一 Python 入口。
- 根目录 `pyproject.toml`: Python 直接依赖清单。

## 推荐环境
先进入 `gameai` conda 环境。Python tools 只需要安装项目本身；只有验证 binding 构建时才需要额外 CMake：

```shell
conda activate gameai
conda install -n gameai -c conda-forge -y python=3.13 pip cmake ninja pybind11
python -m pip install .
```

如果需要单独验证 native binding，再执行：

```shell
cmake -S native -B build/native -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j
```

不要使用 `pip install -e .`。当前仓库明确禁止把 `gameai_native` 直接落到仓库根目录，Python 安装构建统一走根目录 `build/`。

## 验证顺序
优先验证这三条路径：
- `python ./scripts/decide_params.py --trials 1 --games-per-side 1`
- `python ./scripts/benchmark.py --config ./configs/benchmark/benchmark_config.json5`
- `python ./scripts/humanplay.py --config ./configs/humanplay/humanplay_config.json5`
- `cmake -S native -B build/native -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_PYTHON_BINDINGS=ON`

## 代码约定
- MCTS 相关代码放在 `native/core/MCTS/`，复用树时要避免父子 `shared_ptr` 引用环。
- Python humanplay 只放在 `scripts/`，核心博弈和搜索逻辑仍保持在 `native/core/`。
- benchmark 和调参也统一放在 `scripts/`，不要再新增 `native/tools/` 或 `native/benchmark/` 入口。
- Android 只维护 JNI 包装层，不在这里放 Linux 调试逻辑。

## 备注
当前仓库不再保留 `native/train/`、`native/infer/`、`native/humanplay/`、`native/tools/` 和 `native/benchmark/` 的源码入口。
