# GameAI
基于 cpp 和 libtorch，制作不同算法的游戏 ai，同时提供可供 AI 运行的安卓界面

计划开发的游戏：[终极井字棋](https://game.hullqin.cn/jzq)，[璀璨宝石](https://game.hullqin.cn/ccbs)

**注意：** 项目正在开发中，可能会有较大的变化

## 运行项目

### 只编译游戏核心逻辑

现在开发的纯 MCTS AI 只依赖标准库，安装 Cmake 即可正确编译

如果需要使用代码中简单的 humanplay，需安装 jsoncpp (只支持 apt 安装到默认路径)

AI MCTS 基于 libtorch 开发，目前尚未开发完成

纯 MCTS 编译流程：

```shell
# 初次编译，安装jsoncpp
sudo apt-get install libjsoncpp-dev   # headers + .so
# 在 GameAI/app/src/main/cpp/UTT 目录下
rm -rf build/ # 可选
cmake -B build -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 开始人机对战
./build/humanplay/gameplay -config ./humanplay/humanplay_config.json5
```

一并编译benckmark：

```shell
# 在 GameAI/app/src/main/cpp/UTT 目录下
rm -rf build/ # 可选
cmake -B build -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_BENCHMARK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 运行 benchmark
./build/benchmark/benchmark -config ./benchmark/benchmark_config.json5
```


### 安卓程序

安装 release 中的 apk 即可

如果需要自己编译，将项目导入 Android Studio 即可

#### 使用指南

![user_guide](./img/user_guide.jpg)

## 文件目录说明

```cpp
GameAI/ // 只显示正在使用中的文件
├── .gitignore
├── Readme_developer.md // 开发者readme，存储一些暂时弃用的功能
├── Readme.md
├── app
│   ├── .gitignore
│   ├── build.gradle.kts // 构建配置
│   └── src
│       └── main
│           ├── AndroidManifest.xml // 记录应用权限
│           ├── assets
│           ├── cpp
│           │   └── UTT
│           │       ├── CMakeLists.txt
│           │       ├── core
│           │       │   ├── CMakeLists.txt
│           │       │   ├── MCTS
│           │       │   │   ├── TreeNode.cpp // MCTS 树节点实现
│           │       │   │   ├── TreeNode.h
│           │       │   │   └── mcts_pure.h // MCTS算法
│           │       │   ├── base_game.h // 游戏基类接口
│           │       │   ├── json5cpp.h // 解析json5 (依赖 jsoncpp)
│           │       │   ├── utt.cpp // 终极井字棋游戏逻辑
│           │       │   └── utt.h
│           │       ├── humanplay
│           │       │   ├── CMakeLists.txt
│           │       │   ├── gameplay.cpp
│           │       │   └── humanplay_config.json5 // 人机对战配置文件
│           │       └── interface
│           │           ├── CMakeLists.txt
│           │           └── mcts_pure_bridge.cpp // 安卓使用的 JNI 接口
│           ├── java
│           │   └── com
│           │       └── aiprojects
│           │           └── gameai
│           │               ├── MainActivity.kt
│           │               ├── Native_objects_AIHuman.kt // UTTAIvsHuman 使用的 native_functions 封装
│           │               ├── SettingActivity.kt
│           │               ├── UTTAIvsHuman.kt
│           │               ├── UTTHumanPlayerSelect.kt
│           │               ├── UTTModeSelect.kt
│           │               └── native_functions.kt // JNI 接口
│           └── res
│               ├── drawable // 使用的图标
│               │   ├── baseline_arrow_back_24.xml // 返回键
│               │   └── baseline_settings_24.xml // 设置键
│               ├── layout // 界面布局
│               ├── mipmap-... // 存储不同清晰度的应用图标
│               ├── values
│               │   ├── arrays.xml
│               │   ├── colors.xml
│               │   ├── ids.xml
│               │   ├── strings.xml
│               │   └── themes.xml
│               └── xml
│                   └── root_preferences.xml // 设置界面
├── build.gradle.kts // gradle 配置
├── gradle
│   ├── libs.versions.toml
│   └── wrapper
│       ├── gradle-wrapper.jar
│       └── gradle-wrapper.properties
├── gradle.properties
├── gradlew
├── gradlew.bat
└── settings.gradle.kts
```
