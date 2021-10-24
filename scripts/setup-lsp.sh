#!/bin/bash -e

if [[ "$WORKSPACE_MSYS2" ]]; then
  cd "$WORKSPACE_MSYS2"
fi

CXX=clang++ meson setup BUILD-clang --buildtype=debug --prefix=/usr
ln -sf BUILD-clang/compile_commands.json
