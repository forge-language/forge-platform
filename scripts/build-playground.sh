#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
mkdir -p frontend/public/playground
emcc playground/main.c vendor/toolchain/compiler/ast.c \
 vendor/toolchain/compiler/lexer.c vendor/toolchain/compiler/parser.c \
 vendor/toolchain/compiler/optimize.c vendor/toolchain/compiler/mod_registry.c \
 vendor/toolchain/compiler/module_loader.c vendor/toolchain/compiler/codegen_js.c \
 vendor/toolchain/runtime/platform.c \
 -I vendor/toolchain/compiler -I vendor/toolchain/include -std=c2x -O2 \
 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=worker \
 -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 \
 -sINITIAL_MEMORY=16777216 -sMAXIMUM_MEMORY=67108864 \
 -sEXPORTED_RUNTIME_METHODS=FS,callMain \
 -o frontend/public/playground/compiler.mjs
