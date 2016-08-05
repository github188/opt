#!/bin/sh
LIBUV_ROOT=`dirname ${PWD}/$0`

LIBUV_RELEASE=v1.9.1
LIBUV_PACKAGE=${LIBUV_ROOT}/libuv-${LIBUV_RELEASE}.tar.gz
LIBUV_DIR=${LIBUV_ROOT}/libuv-${LIBUV_RELEASE}
echo $LIBUV_ROOT

function untar()
{
    tar zxf ${LIBUV_PACKAGE} -C ${LIBUV_ROOT}
}

function build()
{
    mkdir ${LIBUV_ROOT}/install
    cd ${LIBUV_DIR}
    if [ -f configure ];then
        ./configure --prefix=${LIBUV_ROOT}/install
    elif [ -f autogen.sh ];then
        ./autogen.sh;
        ./configure --prefix=${LIBUV_ROOT}/install
    fi
    make && make install
    cd ..
}

function clean()
{
    rm ${LIBUV_DIR} -rf
}


untar
build
clean
