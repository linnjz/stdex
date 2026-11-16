#!/usr/bin/env bash
#
# Configure, build and run ALL tests under tests/ with a chosen Linux compiler.
#
# Usage:
#   ci/run-tests.sh [-c gcc|clang] [-s 23|26|...] [-d "FOO=1;BAR"] [-x "extra flags"]
#                   [-t Debug|Release|RelWithDebInfo|MinSizeRel] [-b builddir]
#                   [-f ctest-regex] [-j N] [-C] [-n]
#     -C   configure only
#     -n   skip running ctest
#
# Requires: cmake, ninja, and the selected compiler on PATH.
set -euo pipefail

compiler=gcc
std=23
defines=""
extra=""
config=Debug
builddir=""
filter=""
jobs=8
configure_only=0
skip_run=0
relaxed=0

while getopts "c:s:d:x:t:b:f:j:Crnh" opt; do
  case "$opt" in
    c) compiler=$OPTARG ;;
    s) std=$OPTARG ;;
    d) defines=$OPTARG ;;
    x) extra=$OPTARG ;;
    t) config=$OPTARG ;;
    b) builddir=$OPTARG ;;
    f) filter=$OPTARG ;;
    j) jobs=$OPTARG ;;
    C) configure_only=1 ;;
    n) skip_run=1 ;;
    r) relaxed=1 ;;
    h) grep '^#' "$0" | sed 's/^#//'; exit 0 ;;
    *) echo "unknown option" >&2; exit 2 ;;
  esac
done

repo=$(cd "$(dirname "$0")/.." && pwd)
case "$compiler" in
  gcc)   cc=gcc;   cxx=g++ ;;
  clang) cc=clang; cxx=clang++ ;;
  *) echo "unknown compiler: $compiler (use gcc|clang)" >&2; exit 2 ;;
esac

if [ -z "$builddir" ]; then
  builddir="$repo/out/ci/${compiler}-cxx${std}-${config}"
fi

# NAME[=VAL] -> -DNAME[=VAL]
defflags=""
if [ -n "$defines" ]; then
  IFS=';,' read -ra parts <<< "$defines"
  for p in "${parts[@]}"; do
    p="$(echo "$p" | xargs)"
    [ -n "$p" ] && defflags+=" -D$p"
  done
fi
cxxflags="$(echo "$defflags $extra" | xargs)"
# Relaxed mode: -w suppresses all warnings, which also neutralises the scaffold's
# hard-coded -Werror. Useful to *run* the whole suite while some unrelated test
# files still carry warnings; prefer strict (default) once those are cleaned up.
if [ "$relaxed" -eq 1 ]; then
  cxxflags="$(echo "$cxxflags -w" | xargs)"
fi

export BUILD_MODE="$(echo "$config" | tr '[:upper:]' '[:lower:]')"

echo "== configure: $compiler ($cxx), C++$std, $config =="
cmake -S "$repo" -B "$builddir" -G Ninja \
  -DCMAKE_BUILD_TYPE="$config" \
  -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
  -DCMAKE_C_STANDARD=23 -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_C_EXTENSIONS=OFF \
  -DCMAKE_CXX_STANDARD="$std" -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="$cxxflags"

[ "$configure_only" -eq 1 ] && { echo "configure-only requested; done."; exit 0; }

echo "== build all tests =="
cmake --build "$builddir" -j "$jobs"

[ "$skip_run" -eq 1 ] && { echo "skip-run requested; done."; exit 0; }

echo "== ctest =="
ctest_args=(--test-dir "$builddir" --output-on-failure -j "$jobs")
[ -n "$filter" ] && ctest_args+=(-R "$filter")
ctest "${ctest_args[@]}"
