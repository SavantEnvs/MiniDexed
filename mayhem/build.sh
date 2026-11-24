#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# MiniDexed is bare-metal Raspberry Pi firmware (Circle). The input-parsing sources fuzzed here are
# compiled for the host against mayhem/shim/: small stand-ins for the Circle headers they include
# (types, logger, sockets, scheduler, timer). The project sources themselves are compiled unmodified.
OUT="$SRC/mayhem/out"            # gitignored build dir (mayhem/.gitignore)
mkdir -p "$OUT/fuzz" "$OUT/test"
CIRCLE=/opt/toolchains/deps/circle-c009c52/src   # pinned in mayhem/Dockerfile
# -idirafter: Circle's own include/ and addon/ dirs carry assert.h, linux/*.h etc. that must not
# shadow the host's system headers. ARM_ALLOW_MULTI_CORE + RASPPI=4 select the Pi 4 release
# configuration (16 tone generators) in src/config.h, as upstream's build.sh does for RPI=4.
INC=(-DARM_ALLOW_MULTI_CORE -DRASPPI=4 -I"$SRC/mayhem/shim" -I"$SRC/src" -idirafter "$CIRCLE/addon" -idirafter "$CIRCLE/include")
CXXSTD=-std=c++17
PROJECT_SRCS=(src/sysexfileloader.cpp src/net/applemidi.cpp src/performanceconfig.cpp)
# Circle's Properties parser reads the performance file, so it is built with the fuzz flags too
# (out of tree, objects under mayhem/out).
CIRCLE_SRCS=("$CIRCLE/lib/string.cpp" "$CIRCLE/lib/ptrarray.cpp" "$CIRCLE/lib/ptrlist.cpp"
  "$CIRCLE/addon/Properties/properties.cpp" "$CIRCLE/addon/Properties/propertiesbasefile.cpp"
  "$CIRCLE/addon/Properties/propertiesfatfsfile.cpp")

# The MIDI input path (src/mididevice.cpp, src/serialmididevice.cpp) calls into CMiniDexed and
# CUserInterface, which need the whole synth/UI stack; mayhem/shim/midi/standin.h is force-included
# into those compiles to stand in for both (it defines their include guards; see the header).
MIDI_INC=(-include "$SRC/mayhem/shim/midi/standin.h")
MIDI_SRCS=(src/mididevice.cpp src/serialmididevice.cpp mayhem/harnesses/midi_standin.cc)
# The FTP worker names CmDNSPublisher (Circle scheduler mutex/event classes behind it);
# mayhem/shim/ftp/standin.h stands in for that header the same way.
FTP_INC=(-include "$SRC/mayhem/shim/ftp/standin.h" -I"$SRC/mayhem/harnesses")
FTP_SRCS=(src/net/ftpworker.cpp mayhem/harnesses/ftp_standin.cc)

# 1) Project sources, sanitized + DWARF-3, then each harness twice (libFuzzer + standalone).
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1"
for s in "${PROJECT_SRCS[@]}" "${CIRCLE_SRCS[@]}"; do
  # shellcheck disable=SC2086
  $CXX $CXXSTD $FUZZ_FLAGS -fsanitize=fuzzer-no-link "${INC[@]}" -c "$s" -o "$OUT/fuzz/$(basename "${s%.*}").o"
done
for s in "${MIDI_SRCS[@]}"; do
  # shellcheck disable=SC2086
  $CXX $CXXSTD $FUZZ_FLAGS -fsanitize=fuzzer-no-link "${INC[@]}" "${MIDI_INC[@]}" -c "$s" -o "$OUT/fuzz/$(basename "${s%.*}").o"
done
for s in "${FTP_SRCS[@]}"; do
  # shellcheck disable=SC2086
  $CXX $CXXSTD $FUZZ_FLAGS -fsanitize=fuzzer-no-link "${INC[@]}" "${FTP_INC[@]}" -c "$s" -o "$OUT/fuzz/$(basename "${s%.*}").o"
done
# shellcheck disable=SC2086
$CXX $CXXSTD $FUZZ_FLAGS -c mayhem/lsan_off.cc -o "$OUT/fuzz/lsan_off.o"
# shellcheck disable=SC2086
$CC $FUZZ_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/fuzz/standalone_main.o"

# harness:the objects it links (beyond the harness itself)
for pair in "fuzz_sysex_bank:sysexfileloader" "fuzz_applemidi:applemidi string" \
            "fuzz_performance_ini:performanceconfig string ptrarray ptrlist properties propertiesbasefile propertiesfatfsfile" \
            "fuzz_performance_dir:performanceconfig string ptrarray ptrlist properties propertiesbasefile propertiesfatfsfile" \
            "fuzz_serial_midi:mididevice serialmididevice midi_standin string ptrarray ptrlist properties propertiesbasefile propertiesfatfsfile" \
            "fuzz_ftp:ftpworker ftp_standin string"; do
  h=${pair%%:*}; objs=(); for o in ${pair#*:}; do objs+=("$OUT/fuzz/$o.o"); done; extra=(); hinc=()
  [ "$h" = fuzz_serial_midi ] && hinc=("${MIDI_INC[@]}")
  [ "$h" = fuzz_ftp ] && hinc=("${FTP_INC[@]}")
  # see the comment in fuzz_sysex_bank.cc: per-input cleanup of DIR handles the loader leaks
  [ "$h" = fuzz_sysex_bank ] && extra=(-Wl,--wrap=opendir -Wl,--wrap=closedir)
  # shellcheck disable=SC2086
  $CXX $CXXSTD $FUZZ_FLAGS -fsanitize=fuzzer-no-link "${INC[@]}" "${hinc[@]}" -c "mayhem/harnesses/$h.cc" -o "$OUT/fuzz/$h.o"
  # shellcheck disable=SC2086
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz/$h.o" "${objs[@]}" "$OUT/fuzz/lsan_off.o" "${extra[@]}" -o "/mayhem/$h"
  # shellcheck disable=SC2086
  $CXX $FUZZ_FLAGS "$OUT/fuzz/standalone_main.o" "$OUT/fuzz/$h.o" "${objs[@]}" "$OUT/fuzz/lsan_off.o" "${extra[@]}" -o "/mayhem/$h-standalone"
done

# 2) Functional test runner with normal flags (no sanitizers), run by mayhem/test.sh.
# shellcheck disable=SC2086
$CXX $CXXSTD -O1 $COVERAGE_FLAGS "${INC[@]}" mayhem/tests/test_minidexed.cc "${PROJECT_SRCS[@]}" "${CIRCLE_SRCS[@]}" \
  -o "$OUT/test/test_minidexed"
# shellcheck disable=SC2086
$CXX $CXXSTD -O1 $COVERAGE_FLAGS "${INC[@]}" "${MIDI_INC[@]}" mayhem/tests/test_serial_midi.cc "${MIDI_SRCS[@]}" \
  "${CIRCLE_SRCS[@]}" -o "$OUT/test/test_serial_midi"
# shellcheck disable=SC2086
$CXX $CXXSTD -O1 $COVERAGE_FLAGS "${INC[@]}" "${FTP_INC[@]}" mayhem/tests/test_ftp.cc "${FTP_SRCS[@]}" \
  "$CIRCLE/lib/string.cpp" -o "$OUT/test/test_ftp"
echo "build.sh: done"
