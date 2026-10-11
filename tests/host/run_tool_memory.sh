#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
test_binary="$(mktemp "${TMPDIR:-/tmp}/awok-tool-memory.XXXXXX")"
trap 'rm -f "$test_binary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 -g -pthread \
  -fsanitize=address,undefined \
  -Itool_memory_stubs -I../../AWOKxDAG \
  test_tool_memory.cpp -o "$test_binary"
"$test_binary"
