#!/usr/bin/env bash
#
# mayhem/build.sh — build srpc (and its bundled sogou/workflow submodule) twice:
#   1. sanitized + DWARF-3 into fuzz.cmake/ for the fuzz harnesses (mayhem/fuzz_*.cc)
#   2. upstream's normal RelWithDebInfo in-tree build (_lib/, workflow/_lib/) plus the
#      gtest suite in test/ (test/build.cmake), which mayhem/test.sh runs.
# Every build directory matches upstream's `*.cmake` .gitignore pattern, so `git clean -ffdX`
# removes it. Re-runnable and offline: all dependencies come from apt or the git submodules.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
for f in workflow/CMakeLists.txt third_party/lz4/lib/lz4.h third_party/snappy/snappy.cc; do
  [ -f "$f" ] || { echo "missing git submodule file $f (git submodule update --init --recursive)" >&2; exit 1; }
done

F="$SRC/fuzz.cmake"
# UBSan checks relaxed for the fuzz build (ASan and every other UBSan check stay on and halting):
#  - vptr: src/compress/CMakeLists.txt builds rpc_compress_snappy.cc with -fno-rtti, so every
#    RPCSnappySource (a snappy::Source) is reported as "invalid vptr" on each snappy decompress.
#  - null: the `&((T *)0)->member` offset idiom is everywhere: workflow's list_entry() (list.h,
#    used by http_parser.c on every HTTP header walk), srpc's ThriftException descriptor
#    (rpc_message_thrift.h) and the thrift code srpc_generator emits. A real NULL dereference
#    still faults and is reported by ASan.
#    (object-size too: clang reports that same idiom as "member access within null pointer"
#    through the object-size check, so disabling null alone does not silence it; ASan still
#    checks every real out-of-bounds access.)
#  - nonnull-attribute: snappy's SlowAppend does memcpy(NULL, ip, 0) on its first block.
#  - pointer-overflow: snappy 1.1.9's IncrementalCopy copies `dst[i - offset]` with an int i and
#    a size_t offset, so every short back-reference "overflows" to the right address.
RELAX="-fno-sanitize=vptr,null,object-size,nonnull-attribute,pointer-overflow"
FL="$SANITIZER_FLAGS $RELAX -fsanitize=fuzzer-no-link $DEBUG_FLAGS"
# 1) Sanitized libworkflow + libsrpc (out of the source tree's _lib/_include via the
#    projects' own LIB_DIR/INC_DIR/BIN_DIR cache variables). The RelWithDebInfo per-config
#    flags are replaced so -gdwarf-3 is not overridden and assertions stay on.
cmake -S workflow -B "$F/wf-build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="$FL" -DCMAKE_CXX_FLAGS="$FL" \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1" \
  -DLIB_DIR="$F/wf/_lib" -DINC_DIR="$F/wf/_include"
cmake --build "$F/wf-build" -j"$MAYHEM_JOBS"
cmake -S . -B "$F/srpc-build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="$FL" -DCMAKE_CXX_FLAGS="$FL" \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1" \
  -DLIB_DIR="$F/srpc/_lib" -DINC_DIR="$F/srpc/_include" -DBIN_DIR="$F/srpc/_bin"
cmake --build "$F/srpc-build" -j"$MAYHEM_JOBS"

# 2) Upstream's own build with its normal flags + the gtest unit tests (the test.sh oracle).
#    Configured LAST so the in-tree srpc-config.cmake/workflow-config.cmake the test project
#    reads point at these normal libraries.
env -u CFLAGS -u CXXFLAGS cmake -S workflow -B wf.cmake \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS"
cmake --build wf.cmake -j"$MAYHEM_JOBS"
cmake -S . -B build.cmake -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS"
cmake --build build.cmake -j"$MAYHEM_JOBS"
cmake -S test -B test/build.cmake -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS"
cmake --build test/build.cmake -j"$MAYHEM_JOBS" --target unittest var_unittest
# 3) Generated IDL code for the harnesses (the normal srpc_generator + protoc), then the harnesses.
G="$F/gen"
mkdir -p "$G"
cp mayhem/idl/fuzz_pb.proto mayhem/idl/fuzz_thrift.thrift "$G/"
(cd "$G" && protoc --cpp_out=. fuzz_pb.proto \
   && "$SRC/_bin/srpc_generator" thrift fuzz_thrift.thrift . -s >/dev/null)

CXXF="-std=c++17 $SANITIZER_FLAGS $RELAX $DEBUG_FLAGS -I$G -I$F/srpc/_include -I$F/wf/_include"
LIBS="$F/srpc/_lib/libsrpc.a $F/wf/_lib/libworkflow.a -lprotobuf -lssl -lcrypto -lz -lpthread"
$CXX $CXXF -c "$G/fuzz_pb.pb.cc" -o "$G/fuzz_pb.pb.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$F/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$F/standalone_main.o"

link_both() {  # <name> <objs/flags...>
  local name="$1"; shift
  $CXX $CXXF $LIB_FUZZING_ENGINE "$@" "$F/lsan_off.o" -o "$SRC/$name"
  $CXX $CXXF "$F/standalone_main.o" "$@" "$F/lsan_off.o" -o "$SRC/$name-standalone"
}

# One protocol harness source, one binary per message class (MSG_KIND).
i=0
for t in srpc_request brpc_request trpc_request thrift_request srpc_http_request \
         trpc_http_request thrift_http_request trpc_response brpc_response thrift_response; do
  i=$((i + 1))
  $CXX $CXXF -DMSG_KIND=$i -c mayhem/fuzz_message.cc -o "$F/fuzz_$t.o"
  link_both "fuzz_$t" "$F/fuzz_$t.o" "$G/fuzz_pb.pb.o" $LIBS
done

$CXX $CXXF -c mayhem/fuzz_compress.cc -o "$F/fuzz_compress.o"
link_both fuzz_compress "$F/fuzz_compress.o" $LIBS

# srpc_generator's parser is not part of libsrpc: compile it (sanitized) into the harness.
for s in parser generator; do
  $CXX $CXXF -fsanitize=fuzzer-no-link -Isrc -Isrc/generator -c "src/generator/$s.cc" -o "$F/gen_$s.o"
done
for m in thrift pb; do
  $CXX $CXXF -Isrc -Isrc/generator -DGEN_THRIFT=$([ $m = thrift ] && echo 1 || echo 0) \
    -c mayhem/fuzz_generator.cc -o "$F/fuzz_generator_$m.o"
  link_both "fuzz_generator_$m" "$F/fuzz_generator_$m.o" "$F/gen_parser.o" "$F/gen_generator.o"
done

echo "build.sh: done"
