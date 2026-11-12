#!/usr/bin/env bash
#
# mayhem/test.sh — runs the ygopro data-layer selftest that mayhem/build.sh built (normal flags).
# It prints one "PASS <name>" / "FAIL <name>" line per known-answer check (replay header/LZMA/info
# parsing, banlist hashes, deck loading and legality errors, card DB decoding, strings.conf parsing);
# the counts below come from those lines, so a neutered or silent binary fails.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

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

# The selftest suite has this many checks; fewer PASS lines means the program did not run them all.
EXPECTED=60

runner="$SRC/ygopro_selftest"
[ -x "$runner" ] || { echo "missing $runner — mayhem/build.sh should have built it" >&2; emit_ctrf "ygopro-selftest" 0 1 0; exit 1; }

out="$("$runner" 2>&1)"
echo "$out"
passed=$(printf '%s\n' "$out" | grep -c '^PASS ')
failed=$(printf '%s\n' "$out" | grep -c '^FAIL ')
if [ "$passed" -lt "$EXPECTED" ] && [ "$failed" -eq 0 ]; then
  echo "only $passed of $EXPECTED checks reported PASS" >&2
  failed=$(( EXPECTED - passed ))
fi
printf '%s\n' "$out" | grep -q '^SELFTEST: ' || { echo "selftest produced no summary line" >&2; failed=$(( failed + 1 )); }
emit_ctrf "ygopro-selftest" "$passed" "$failed" 0
