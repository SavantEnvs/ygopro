#!/usr/bin/env bash
#
# mayhem/build.sh — builds the ygopro fuzz harnesses (sanitized) and the selftest oracle (normal flags).
#
# ygopro is a game client whose executable is one big Irrlicht GUI program, so the fuzz surface is the
# data layer it links in: replay.cpp, deck_manager.cpp, data_manager.cpp and file_system.cpp, compiled
# unmodified from gframe/. Those units include the whole client header chain, so the build needs the
# Irrlicht headers and the ygopro-core headers (the `ocgcore` submodule, which upstream pins to a commit
# too old for its own client code). Both come from /opt/toolchains/deps (see mayhem/Dockerfile).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

CORE=/opt/toolchains/deps/ygopro-core-1f7769cd/src
IRR=/opt/toolchains/deps/irrlicht-966d8756/src/include
for d in "$CORE/common.h" "$IRR/irrlicht.h"; do
  [ -f "$d" ] || { echo "missing dependency $d (baked into the image by mayhem/Dockerfile)" >&2; exit 1; }
done

cd "$SRC"

# gframe sources include "../ocgcore/<header>"; the submodule dir is empty in the checkout, so the include
# falls through to the -I path: <build>/include/../ocgcore -> the pinned ygopro-core sources.
BUILD="$SRC/build"
rm -rf "$BUILD/san" "$BUILD/test"
mkdir -p "$BUILD/include" "$BUILD/san" "$BUILD/test"
ln -sfn "$CORE" "$BUILD/ocgcore"

INCLUDES=(-I"$SRC/gframe" -I"$SRC/mayhem" -I"$BUILD/include" -I"$IRR" -I/usr/include/freetype2)
DEFS=(-D_IRR_STATIC_LIB_ -DYGOPRO_USE_AUDIO)
LIBS=(-lsqlite3 -llzma -lpthread)
UNITS=(replay deck_manager data_manager file_system)

compile() {   # compile <out.o> <source> <flags...>
  local out="$1" src="$2"; shift 2
  "$CXX" -std=c++17 "$@" "${DEFS[@]}" "${INCLUDES[@]}" -c "$src" -o "$out"
}

# 1) Project units, sanitized + SanCov-instrumented (so Mayhem sees coverage in the fuzzed code).
SAN=($SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1)
pids=()
for u in "${UNITS[@]}"; do
  compile "$BUILD/san/$u.o" "$SRC/gframe/$u.cpp" "${SAN[@]}" & pids+=($!)
done
compile "$BUILD/san/stubs.o" "$SRC/mayhem/stubs.cpp" "${SAN[@]}" & pids+=($!)
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$BUILD/san/lsan_off.o" & pids+=($!)
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$BUILD/san/standalone_main.o" & pids+=($!)
for p in "${pids[@]}"; do wait "$p"; done
OBJS=("$BUILD/san/replay.o" "$BUILD/san/deck_manager.o" "$BUILD/san/data_manager.o" "$BUILD/san/file_system.o" "$BUILD/san/stubs.o" "$BUILD/san/lsan_off.o")

# 2) Harnesses: libFuzzer binary + standalone reproducer each.
TARGETS=(fuzz_replay fuzz_lflist fuzz_deck fuzz_carddb fuzz_strings)
pids=()
build_target() {
  local t="$1"
  compile "$BUILD/san/$t.o" "$SRC/mayhem/$t.cpp" "${SAN[@]}"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$BUILD/san/$t.o" "${OBJS[@]}" "${LIBS[@]}" -o "$SRC/$t"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$BUILD/san/$t.o" "$BUILD/san/standalone_main.o" "${OBJS[@]}" "${LIBS[@]}" -o "$SRC/$t-standalone"
}
for t in "${TARGETS[@]}"; do build_target "$t" & pids+=($!); done
for p in "${pids[@]}"; do wait "$p"; done

# 3) Selftest oracle: the same project units with the project's normal flags (no sanitizers, no fuzzer
#    instrumentation), left at /mayhem/ygopro_selftest for mayhem/test.sh.
NORMAL=(-O2 $COVERAGE_FLAGS)
pids=()
for u in "${UNITS[@]}"; do
  compile "$BUILD/test/$u.o" "$SRC/gframe/$u.cpp" "${NORMAL[@]}" & pids+=($!)
done
compile "$BUILD/test/stubs.o" "$SRC/mayhem/stubs.cpp" "${NORMAL[@]}" & pids+=($!)
compile "$BUILD/test/selftest.o" "$SRC/mayhem/selftest.cpp" "${NORMAL[@]}" & pids+=($!)
for p in "${pids[@]}"; do wait "$p"; done
"$CXX" $COVERAGE_FLAGS "$BUILD"/test/*.o "${LIBS[@]}" -o "$SRC/ygopro_selftest"

echo "build ok: ${TARGETS[*]} ygopro_selftest"
