
编译 train 的 release 版本：
cmake -B build -DENABLE_TRAIN=ON -DENABLE_INFER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake -B build -DENABLE_TRAIN=OFF -DENABLE_INFER=ON -DCMAKE_BUILD_TYPE=Release

运行：

cd build
./build/train/train -config /root/Desktop/AIGame/train/train_config.json5
./build/train/train -config <config_file>

./build/infer/infer -config <config_file>
./build/infer/infer -config /root/Desktop/AIGame/infer/test_config.json5

train编译流程：
rm -rf build/
cmake -B build -DENABLE_TRAIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/train/train -config /root/Desktop/AIGame/train/train_config.json5

测试train编译流程：
rm -rf build/
cmake -B build -DENABLE_TRAIN_TEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/train/train_test -config /root/Desktop/AIGame/train/train_test_config.json5