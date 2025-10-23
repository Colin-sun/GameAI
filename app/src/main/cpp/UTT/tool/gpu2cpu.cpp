// 将训练保存的GPU模型转换为CPU模型，以便在CPU上运行
#include <torch/torch.h>
#include <torch/script.h>
#include <iostream>
#include <filesystem>
#include <fstream>  // 添加这个头文件

namespace fs = std::filesystem;

int main()
{
    std::string base_path = "/workspace/AIGame/0919_UTT/";
    int cnt = 0;

    for (const auto& entry : fs::directory_iterator(base_path)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("model_turn", 0) != 0 || name.substr(name.size() - 3) != ".pt")
            continue;

        std::cout << "Processing " << name << " ...\n";

        try {
            std::string raw_path = entry.path().string();

            // 正确加载方式：传字符串路径
            torch::jit::script::Module model = torch::jit::load(raw_path);

            // 搬到 CPU
            model.to(torch::kCPU);

            // 保存为新的 CPU 文件
            std::string out_dir = base_path + "model_cpu/";
            fs::create_directories(out_dir);  // 确保目录存在
            std::string out_path = out_dir + name.substr(0, name.size() - 3) + "_cpu.pt";
            model.save(out_path);

            std::cout << "  -> " << out_path << "  done.\n";
            ++cnt;
        }
        catch (const std::exception& ex) {
            std::cerr << "  failed: " << ex.what() << '\n';
        }
    }

    if (cnt == 0) {
        std::cout << "No model_turn*.pt found.\n";
    } else {
        std::cout << "Converted " << cnt << " files.\n";
    }

    return 0;
}