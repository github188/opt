#!/bin/sh

UVINCLUDE=${PWD}/../dependlib/install/include
UVLIB=${PWD}/../dependlib/install/lib
makeconfigure -I${UVINCLUDE} -L${UVLIB} -luv -Wl,-rpath=.:${UVLIB} cxx