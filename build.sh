#!/usr/bin/bash

SOURCES="main.c exec.c echo.c"
BUILD_DIR="build"
TARGET="$BUILD_DIR/minish"
FLAGS="-Wall -Wextra -fsanitize=address"

set -xe

mkdir -p $BUILD_DIR
gcc $SOURCES -o $TARGET $FLAGS
