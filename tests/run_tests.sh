#!/bin/bash

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT/minishell}"
BASH_BIN="${BASH_BIN:-bash}"
BASH_MAJOR="$("$BASH_BIN" -c 'echo ${BASH_VERSINFO[0]}')"
MODE="${1:-mandatory}"
WORK="$(mktemp -d /tmp/minishell_tests.XXXXXX)"
PASS=0
FAIL=0
SKIP=0

normalize_err() {
	sed -E -e 's/^(bash|minishell|.*bash): (line [0-9]+: )?//' \
		-e 's/^-c: line [0-9]+: //' \
		-e 's/^line [0-9]+: //'
}

run_case() {
	local input="$1"
	local exp_out exp_err exp_code got_out got_err got_code

	if [ "${input%%$'\n'*}" == "#bash5" ]; then
		input="${input#*$'\n'}"
		if [ "$BASH_MAJOR" -lt 4 ]; then
			SKIP=$((SKIP + 1))
			return
		fi
	fi
	rm -rf "$WORK/b" "$WORK/m"
	mkdir -p "$WORK/b" "$WORK/m"
	(cd "$WORK/b" && printf '%s\n' "$input" | env -i PATH="$PATH" HOME="$HOME" \
		USER=tester "$BASH_BIN" >"$WORK/b.out" 2>"$WORK/b.err")
	exp_code=$?
	(cd "$WORK/m" && printf '%s\n' "$input" | env -i PATH="$PATH" HOME="$HOME" \
		USER=tester "$MINISHELL" >"$WORK/m.out" 2>"$WORK/m.err")
	got_code=$?
	exp_out="$(sed "s#$WORK/b#WORKDIR#g" "$WORK/b.out")"
	got_out="$(sed "s#$WORK/m#WORKDIR#g" "$WORK/m.out")"
	exp_err="$(normalize_err <"$WORK/b.err")"
	got_err="$(normalize_err <"$WORK/m.err")"
	local exp_has_err=0 got_has_err=0
	[ -n "$exp_err" ] && exp_has_err=1
	[ -n "$got_err" ] && got_has_err=1
	if [ "$exp_out" == "$got_out" ] && [ "$exp_code" == "$got_code" ] \
		&& { [ -z "$STRICT_ERR" ] || [ "$exp_err" == "$got_err" ]; } \
		&& [ "$exp_has_err" == "$got_has_err" ]; then
		PASS=$((PASS + 1))
	else
		FAIL=$((FAIL + 1))
		printf '\033[31mFAIL\033[0m: %s\n' "$input"
		printf '  bash  [%s] out: %s\n        err: %s\n' "$exp_code" "$exp_out" "$exp_err"
		printf '  mini  [%s] out: %s\n        err: %s\n' "$got_code" "$got_out" "$got_err"
	fi
}

run_file() {
	local test_case="" line has_lines=0

	while IFS= read -r line || [ -n "$line" ]; do
		if [ "$line" == "%%" ]; then
			[ "$has_lines" -eq 1 ] && run_case "$test_case"
			test_case=""
			has_lines=0
		elif [ "$has_lines" -eq 0 ]; then
			test_case="$line"
			has_lines=1
		else
			test_case="$test_case"$'\n'"$line"
		fi
	done <"$1"
	[ "$has_lines" -eq 1 ] && run_case "$test_case"
}

run_file "$ROOT/tests/cases_mandatory.txt"
[ -f "$ROOT/tests/cases_extra.txt" ] && run_file "$ROOT/tests/cases_extra.txt"
if [ "$MODE" == "bonus" ]; then
	run_file "$ROOT/tests/cases_bonus.txt"
fi

rm -rf "$WORK"
printf '\nReference: %s (bash %s)\n' "$BASH_BIN" \
	"$("$BASH_BIN" -c 'echo $BASH_VERSION')"
printf 'Passed: %d  Failed: %d' "$PASS" "$FAIL"
if [ "$SKIP" -gt 0 ]; then
	printf '  Skipped: %d (bash 5 behavior, reference bash is older)' "$SKIP"
fi
printf '\n'
[ "$FAIL" -eq 0 ]
