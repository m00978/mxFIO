# Copyright (c) 2025 MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.

rm -rf build

mkdir build

cd build

../configure --enable-libcufile --enable-cuda

make -j 12

sudo make install
