#!/bin/sh

set -x

BUILDDIR="build/"

if [ ! -d ${BUILDDIR} ]; then
  mkdir ${BUILDDIR}
fi

cd ${BUILDDIR}

if [ -f "KISSTest" ]; then
  make clean &&
    make
else
  cmake .. &&
    make
fi

./KISSTest
