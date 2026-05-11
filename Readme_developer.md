# GameAI Developer Notes

## 当前结构
- `native/`: 纯原生入口，`native/CMakeLists.txt` 负责 Linux 构建。
- `app/src/main/cpp/CMakeLists.txt`: Android JNI 入口，内部把 `native/` 作为子目录引入。
- `native/core/`: 共用棋类逻辑、MCTS、网络和 JSON5 解析。
- `native/humanplay/`、`native/benchmark/`、`native/tools/`: 当前主要验证路径。
- `native/train/`、`native/infer/`: 实验性代码，未纳入默认回归。

## 推荐环境
先进入 `gameai` conda 环境，再构建和测试：

```shell
conda activate gameai
conda install -n gameai -c conda-forge -y python=3.13 cmake jsoncpp ninja
cmake -S native -B native/build -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_TOOL=ON -DENABLE_BENCHMARK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build native/build -j
```

如果需要 Python 工具：

```shell
python -m pip install -r native/tools/requirements.txt
```

## 验证顺序
优先验证这三个可执行文件：
- `./native/build/humanplay/gameplay -config ./configs/humanplay/humanplay_config.json5`
- `./native/build/benchmark/benchmark -config ./configs/benchmark/benchmark_config.json5`
- `./native/build/tools/self_play 20 0.8 20 0.8`

## 代码约定
- 新增 JSON5 解析代码时，继续使用 `native/core/json5cpp.h`，不要写死系统绝对路径。
- MCTS 相关代码放在 `native/core/MCTS/`，复用树时要避免父子 `shared_ptr` 引用环。
- Android 只维护 JNI 包装层，不在这里放 Linux 调试逻辑。

## 备注
`native/train/` 和 `native/infer/` 仅保留历史/实验实现，如要恢复编译，需要单独补齐对应依赖和入口。
