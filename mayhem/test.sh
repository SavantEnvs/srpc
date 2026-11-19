#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# srpc's own gtest suite (test/unittest.cc, test/var_unittest.cc), built by mayhem/build.sh with
# upstream's normal flags. unittest starts SRPC / SRPC-HTTP / BRPC / Thrift / Thrift-HTTP servers
# on localhost, calls them through the generated clients and asserts the computed replies (Add,
# Substr, compressed bodies); var_unittest asserts metric values.
passed=0; failed=0; skipped=0
for t in unittest var_unittest; do
  bin="$SRC/test/build.cmake/$t"
  if [ ! -x "$bin" ]; then
    echo "test.sh: $bin missing (mayhem/build.sh should have built it)" >&2
    failed=$((failed + 1)); continue
  fi
  log=$(mktemp)
  (cd "$SRC/test/build.cmake" && "$bin") >"$log" 2>&1
  rc=$?
  cat "$log"
  ran=$(sed -n 's/^\[==========\] \([0-9]*\) tests\? from .* ran\..*/\1/p' "$log" | tail -1)
  p=$(sed -n 's/^\[  PASSED  \] \([0-9]*\) tests\?\..*/\1/p' "$log" | tail -1)
  f=$(sed -n 's/^\[  FAILED  \] \([0-9]*\) tests\?, listed below.*/\1/p' "$log" | tail -1)
  s=$(sed -n 's/^\[  SKIPPED \] \([0-9]*\) tests\?, listed below.*/\1/p' "$log" | tail -1)
  rm -f "$log"
  p=${p:-0}; f=${f:-0}; s=${s:-0}
  if [ -z "$ran" ] || { [ "$p" -eq 0 ] && [ "$f" -eq 0 ]; }; then
    echo "test.sh: $t produced no gtest summary (rc=$rc)" >&2
    f=$((f + 1))
  elif [ "$rc" -ne 0 ] && [ "$f" -eq 0 ]; then
    echo "test.sh: $t exited $rc" >&2
    f=$((f + 1))
  fi
  passed=$((passed + p)); failed=$((failed + f)); skipped=$((skipped + s))
done

emit_ctrf "gtest" "$passed" "$failed" "$skipped"
