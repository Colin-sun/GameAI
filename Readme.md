# AIGame
基于 cpp 和 libtorch，制作不同算法的游戏 ai

计划开发的游戏：[终极井字棋](https://game.hullqin.cn/jzq)，[璀璨宝石](https://game.hullqin.cn/ccbs)



## 运行项目

### 游戏核心逻辑

现在开发的纯MCTS只依赖标准库，安装 Cmake 即可正确编译

AI MCTS 基于 libtorch 开发，目前尚未开发完成

纯 MCTS 编译流程：

```shell
# 在 AIGame/Games/UTT 目录下
rm -rf build/
cmake -B build -DENABLE_MCTS_PURE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 开始人机对战
./build/humanplay/gameplay -config /root/Desktop/AIGame/humanplay/infer_config_mcts_pure.json5
```

下面是目前被弃用的编译脚本：

```shell
# 编译 train 的 release 版本：
cmake -B build -DENABLE_TRAIN=ON -DENABLE_INFER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake -B build -DENABLE_TRAIN=OFF -DENABLE_INFER=ON -DCMAKE_BUILD_TYPE=Release

# 运行：
cd build
./build/train/train -config /root/Desktop/AIGame/train/train_config.json5
./build/train/train -config <config_file>

./build/infer/infer -config <config_file>
./build/infer/infer -config /root/Desktop/AIGame/infer/test_config.json5

# train编译流程：
rm -rf build/
cmake -B build -DENABLE_TRAIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/train/train -config /root/Desktop/AIGame/train/train_config.json5

# 测试train编译流程：
rm -rf build/
cmake -B build -DENABLE_TRAIN_TEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/train/train_test -config /root/Desktop/AIGame/train/train_test_config.json5

# 测试train编译流程：
rm -rf build/
cmake -B build -DENABLE_TRAIN_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/train/train_test -config /root/Desktop/AIGame/train/train_test_config.json5
```

### 安卓程序

开发中……



## 文件目录说明

```cpp
// 带 // 的文件为目前不使用的文件
Games/UTT/
├── CMakeLists.txt
├── Readme.md
├── humanplay/
│   ├── CMakeLists.txt
│   ├── gameplay.cpp // 简易的人机对战的主程序
│   └── infer_config_mcts_pure.json5
├── core/
│   ├── CMakeLists.txt
│   ├── json5cpp.h // 解析配置文件
│   ├── base_game.h // 定义Game接口
│   ├── utt.cpp // 实现UTT游戏逻辑
│   ├── utt.h
│   ├── MCTS/
│   │   ├── TreeNode.cpp // MCTS树节点实现
│   │   ├── TreeNode.h
│   │   ├── mcts_pure.cpp // 随机 rollout 的 MCTS 搜索
│   │   ├── mcts_pure.h
│   │   ├── // mcts_alphazero.cpp
│   │   └── // mcts_alphazero.h
│   ├── // TicTacToe.cpp
│   ├── // TicTacToe.h
│   ├── // mcts_broken.cpp
│   ├── // mcts_broken.h
│   ├── // minimax.cpp
│   ├── // minimax.h
│   ├── // network.cpp
│   └── // network.hpp
├── // infer/
│   ├── // CMakeLists.txt
│   ├── // gameplay.cpp
│   ├── // gameplay_mcts.cpp
│   ├── // infer_config.json5
│   ├── // infer_config_mcts.json5
│   ├── // test.cpp
│   └── // test_config.json5
├── // tool/
│   └── // gpu2cpu.cpp
├── // train/
│   ├── // CMakeLists.txt
│   ├── // CMakeLists_train_raw.txt
│   ├── // config.h
│   ├── // omp_rng.hpp
│   ├── // train.cpp
│   ├── // train_config.json5
│   ├── // train_test.cpp
└── └── // train_test_config.json5
```



## 开发指南

### 添加新游戏
在项目中增加文件，实现 base_game.h 定义的接口和自己的 humanplay 逻辑，同时相应修改cmake编译文件即可，可以参考 utt.h 和 utt.cpp 的实现