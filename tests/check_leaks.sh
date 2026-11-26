#!/bin/bash

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT/minishell}"
WORK="$(mktemp -d /tmp/minishell_leaks.XXXXXX)"
REPORT="$WORK/report.txt"

if ! command -v valgrind >/dev/null 2>&1; then
	echo "valgrind not found (install: apt install valgrind)" >&2
	exit 1
fi

for file in "$ROOT/tests/cases_mandatory.txt" "$ROOT/tests/cases_extra.txt" \
	${1:+"$ROOT/tests/cases_bonus.txt"}; do
	grep -v -E '^(%%|#|exit|unterminated)' "$file" | grep -v '^cat | cat'
done >"$WORK/script.txt"

valgrind --leak-check=full --show-leak-kinds=definite \
	--errors-for-leak-kinds=definite --error-exitcode=42 \
	--log-file="$REPORT" \
	"$MINISHELL" <"$WORK/script.txt" >/dev/null 2>&1
status=$?

grep -E "definitely lost:" "$REPORT" | head -1
if [ "$status" -eq 42 ]; then
	rm -rf "$WORK"
	exit 1
fi
grep -q "definitely lost: 0 bytes" "$REPORT"
status=$?
rm -rf "$WORK"
exit $status
