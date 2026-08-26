#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
test_list=${TEST_LIST:-$script_dir/kvm-selftests.list}
timeout=${TIMEOUT:-120}
selected_tests=
test_list_arg=

usage()
{
	cat <<EOF
Usage: [TEST_LIST=FILE] $0 [-t TEST[,TEST...]]... [FILE]

Run every KVM selftest binary listed in FILE and print a result for each.
Defaults to TEST_LIST, or $script_dir/kvm-selftests.list if TEST_LIST is unset.
Set TIMEOUT to change the per-test timeout in seconds (default: 120).

  -t, --test TEST   Run only TEST. May be repeated or comma-separated.
                    TEST may be an absolute path, path suffix, or basename.
  -h, --help        Show this help text.
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	-t | --test)
		if [ "$#" -lt 2 ] || [ -z "$2" ]; then
			echo "$0: $1 requires a test name" >&2
			usage >&2
			exit 1
		fi
		tests=$(printf '%s' "$2" | tr ',' ' ')
		if [ -z "$(printf '%s' "$tests" | tr -d '[:space:]')" ]; then
			echo "$0: $1 requires a non-empty test name" >&2
			exit 1
		fi
		selected_tests="$selected_tests $tests"
		shift 2
		;;
	-h | --help)
		usage
		exit 0
		;;
	--)
		shift
		break
		;;
	-*)
		echo "$0: unknown option: $1" >&2
		usage >&2
		exit 1
		;;
	*)
		if [ -n "$test_list_arg" ]; then
			echo "$0: more than one test-list file specified" >&2
			usage >&2
			exit 1
		fi
		test_list_arg=$1
		shift
		;;
	esac
done

if [ "$#" -gt 1 ] || { [ "$#" -eq 1 ] && [ -n "$test_list_arg" ]; }; then
	echo "$0: more than one test-list file specified" >&2
	usage >&2
	exit 1
elif [ "$#" -eq 1 ]; then
	test_list_arg=$1
fi

if [ -n "$test_list_arg" ]; then
	test_list=$test_list_arg
fi

if [ ! -r "$test_list" ]; then
	echo "$0: cannot read test list: $test_list" >&2
	exit 1
fi

case "$timeout" in
"" | *[!0-9]*)
	echo "$0: TIMEOUT must be a non-negative integer: $timeout" >&2
	exit 1
	;;
esac

timeout_cmd=$(command -v timeout 2>/dev/null || :)

matches_selector()
{
	test_path=$1
	selector=$2

	case "$test_path" in
	"$selector" | */"$selector") return 0 ;;
	esac
	[ "${test_path##*/}" = "$selector" ]
}

is_selected()
{
	test_path=$1

	[ -z "$selected_tests" ] && return 0
	for selector in $selected_tests; do
		matches_selector "$test_path" "$selector" && return 0
	done
	return 1
}

for selector in $selected_tests; do
	found=false
	while IFS= read -r test || [ -n "$test" ]; do
		case "$test" in
		"" | \#*) continue ;;
		esac
		if matches_selector "$test" "$selector"; then
			found=true
			break
		fi
	done <"$test_list"
	if ! "$found"; then
		echo "$0: test not found in $test_list: $selector" >&2
		exit 1
	fi
done

total=0
while IFS= read -r test || [ -n "$test" ]; do
	case "$test" in
	"" | \#*) continue ;;
	esac
	is_selected "$test" && total=$((total + 1))
done <"$test_list"

if [ "$total" -eq 0 ]; then
	echo "$0: no tests selected from: $test_list" >&2
	exit 1
fi

pass=0
fail=0
skip=0
xfail=0
current=0
failed_tests=

while IFS= read -r test || [ -n "$test" ]; do
	case "$test" in
	"" | \#*) continue ;;
	esac
	is_selected "$test" || continue

	current=$((current + 1))
	printf '\n[%s/%s] RUN  %s\n' "$current" "$total" "$test"

	if [ ! -x "$test" ]; then
		rc=1
		echo "Test is missing or not executable: $test" >&2
	else
		(
			cd -- "$(dirname -- "$test")" || exit 1
			if [ -n "$timeout_cmd" ]; then
				"$timeout_cmd" --foreground "$timeout" \
					"./$(basename -- "$test")"
			else
				"./$(basename -- "$test")"
			fi
		)
		rc=$?
	fi

	case "$rc" in
	0)
		pass=$((pass + 1))
		status=PASS
		;;
	2)
		xfail=$((xfail + 1))
		status=XFAIL
		;;
	4)
		skip=$((skip + 1))
		status=SKIP
		;;
	124)
		fail=$((fail + 1))
		status="FAIL (timeout=${timeout}s)"
		;;
	*)
		fail=$((fail + 1))
		status="FAIL (exit=$rc)"
		;;
	esac
	if [ "$rc" -ne 0 ] && [ "$rc" -ne 2 ] && [ "$rc" -ne 4 ]; then
		if [ -z "$failed_tests" ]; then
			failed_tests=$test
		else
			failed_tests="$failed_tests
$test"
		fi
	fi
	printf '[%s/%s] %s %s\n' "$current" "$total" "$status" "$test"
done <"$test_list"

printf '\nSummary: pass=%s fail=%s xfail=%s skip=%s total=%s\n' \
	"$pass" "$fail" "$xfail" "$skip" "$total"

if [ "$fail" -gt 0 ]; then
	printf '\nFailed tests:\n%s\n' "$failed_tests"
fi

[ "$fail" -eq 0 ]
