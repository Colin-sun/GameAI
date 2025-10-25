// 速度测试：空盘生成
#include "json5cpp.h"
#include "utt.h"
#include "MCTS/TreeNode.h"

#ifdef MCTS_PURE
#include "MCTS/mcts_pure.h"
#endif

#include "MCTS/mcts_pure.h" // develop use

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <random>
#include <chrono>

// 解析配置文件
class Config {
public:
    Config(std::string config_path) {
        // 读文件
        std::ifstream config_file;
        config_file.open(config_path);
        if (!config_file) {
            throw std::runtime_error("无法打开配置文件: " + config_path);
        }
        // 解析 JSON5
        Json::Value parsed_json;
        std::string err;
        if (!Json5::parse(config_file, parsed_json, &err)) {
            config_file.close();
            throw std::runtime_error("配置文件解析错误: " + err);
        }
        config_file.close();
        config_json = parsed_json; // 将解析结果赋值给成员变量
    };

    // 根据参数获取 MCTSPure结构体
    MCTSPure<UltimateTicTacToe, ActionList> getMCTSPure() {
        // 创建随机数生成器
        std::random_device rd;
        std::mt19937 rand_engine(rd());
        return MCTSPure<UltimateTicTacToe, ActionList>(
            config_json["mcts"]["n_playout"].asInt(),
            config_json["mcts"]["c_puct"].asFloat(),
            rand_engine
        );
    }

private:
    Json::Value config_json;
};



// 测试
int main(int argc, char* argv[]) {
    // 读取配置文件
    std::string configPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-config" && i + 1 < argc) {
            configPath = argv[i + 1];
            std::cout << "读取配置文件: " << configPath << '\n';
            break;
        }
    }
    if (configPath.empty()) {
        std::cerr << "请使用 -config <路径> 指定配置文件\n";
        return EXIT_FAILURE;
    }
    Config config(configPath);

    // 创建初始棋盘和 MCTS对象
    UltimateTicTacToe board;
    auto aiplayer = config.getMCTSPure();
    std::cout << "初始化完成，开始测试\n";

    for (int time = 0; time < 10; time++) {
        auto start = std::chrono::high_resolution_clock::now();
        int best_index = aiplayer.get_move(board);
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "第" << time << "次测试，耗时: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms\n";
        std::cout << "最佳动作: " << best_index << '\n';
    }

    return 0;
}