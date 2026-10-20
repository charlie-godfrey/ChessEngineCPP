#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
ENGINE="$BUILD_DIR/uci"

if [ ! -x "$ENGINE" ]; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >&2
  cmake --build "$BUILD_DIR" --target uci >&2
fi

cd "$ROOT_DIR"
exec "$ENGINE" "$@"
