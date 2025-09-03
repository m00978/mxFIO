# Copyright (c) 2025 MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.

rm -rf build

mkdir build

cd build

../configure --enable-cuda --enable-libcufile

make -j 12

sudo make install
