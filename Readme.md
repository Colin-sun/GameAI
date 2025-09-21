
编译 train 的 release 版本：

cmake -B build -DENABLE_TRAIN=ON -DENABLE_INFER=OFF -DCMAKE_BUILD_TYPE=Release

cmake --build build -j

cmake -B build -DENABLE_TRAIN=OFF -DENABLE_INFER=ON -DCMAKE_BUILD_TYPE=Release

运行：

cd build

./train/train -config <config_file>
./build/infer/infer -config <config_file>
./build/infer/infer -config /root/Desktop/AIGame/infer/infer_config.json5