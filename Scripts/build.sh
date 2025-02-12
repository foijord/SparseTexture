#!/usr/bin/env bash

Config=${1:-Debug}

mkdir -p Build
cmake -S Code/ -B Build/ -DCMAKE_BUILD_TYPE=$Config
cmake --build Build/ --config $Config
