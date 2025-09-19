
编译 train 的 release 版本：

cmake -B build -DENABLE_TRAIN=ON -DENABLE_INFER=OFF -DCMAKE_BUILD_TYPE=Release

cmake --build build -j


运行：

cd build

./train/train -config <config_file>