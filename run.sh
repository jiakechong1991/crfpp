#!/bin/bash
make clean
./configure
make -j 8
cd /Users/wangxiaoke/project/CRF++-0.58_source/example/seg
exec.sh
