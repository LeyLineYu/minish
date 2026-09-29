#!/usr/bin/bash

SOURCES="src/main.c src/exec.c src/echo.c"
BUILD_DIR="build"
TARGET="$BUILD_DIR/minish"
FLAGS="-Wall -Wextra -fsanitize=address"

set -xe

mkdir -p $BUILD_DIR
gcc $SOURCES -o $TARGET $FLAGS
