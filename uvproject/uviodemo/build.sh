#!/bin/sh

ROOT=`dirname ${PWD}/$0`

LIBUV_RELEASE=v1.9.1
LIBUV_PACKAGE=${ROOT}/libuv-${LIBUV_RELEASE}.tar.gz
LIBUV_DIR=${ROOT}/libuv-${LIBUV_RELEASE}

BUILD_HOST=$1

if [[ $BUILD_HOST == "x86" ]];then
    UV_CONFIG=""
    MK_CONFIG=""
else
    UV_CONFIG="--host=arm-linux CC=arm-hisiv500-linux-gcc"
    MK_CONFIG="CROSS_COMPILER=arm-hisiv500-linux-"
fi

function untar()
{
    tar zxf ${LIBUV_PACKAGE} -C ${ROOT}
}

function build()
{
    mkdir -p ${ROOT}/install
    cd ${LIBUV_DIR}
    if [ -f configure ];then
        ./configure --prefix=${ROOT}/install ${UV_CONFIG}
    elif [ -f autogen.sh ];then
        ./autogen.sh;
        ./configure --prefix=${ROOT}/install ${UV_CONFIG}
    fi
    make && make install
    cd ..
}

function clean()
{
    rm ${LIBUV_DIR} -rf
}

if [ ! -f install/lib/libuv.so ];then
    untar
    build
    clean
fi

make distclean
makeconfigure -I"${ROOT}/install/include" -L"${ROOT}/install/lib" -luv -lpthread -Wl,-rpath=./ ${MK_CONFIG}
make

if [ bin/main ];then
    # run export LD_LIBRARY_PATH=./ first
    mkdir -p /opt/nfshost/uviodemo
    cp -rvf install/lib/* /opt/nfshost/uviodemo
    cp -rvf bin/main /opt/nfshost/uviodemo
    chmod 777 -Rf /opt/nfshost/uviodemo
fi



