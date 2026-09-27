#!/usr/bin/env bash
# Host-side NetworkParse tests with ASan/UBSan. No Arduino toolchain required.
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 \
  -fsanitize=address,undefined \
  -I../../AWOKxDAG \
  -o test_network_parse test_network_parse.cpp
./test_network_parse
fleet_test_bin="$(mktemp)"
trap 'rm -f "$fleet_test_bin"' EXIT
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 \
  -fsanitize=address,undefined \
  -I../../AWOKxDAG \
  -o "$fleet_test_bin" test_fleet_location.cpp
"$fleet_test_bin"
