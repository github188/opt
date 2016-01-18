#!/bin/sh
PROJECT_ROOT=${PWD}
UV=libuv-v0.11.29
cd ${UV}
./autogen.sh
./configure --prefix="${PROJECT_ROOT}/install"
rm autom4te.cache -rf
cd - 
make -C ${UV} && make -C ${UV} install
make -C ${UV} clean && make -C ${UV} distclean
