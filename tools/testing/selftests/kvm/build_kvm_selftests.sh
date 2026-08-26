#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
output_dir=${OUTPUT:-$script_dir}
test_list=${TEST_LIST:-$output_dir/kvm-selftests.list}

usage()
{
	cat <<EOF
Usage: [OUTPUT=DIR] [TEST_LIST=FILE] $0 [MAKE_OPTIONS]

Build all KVM selftest binaries for the host architecture and write their
absolute paths to FILE.  MAKE_OPTIONS (for example, -j8 or LLVM=1) are passed
to make.

Defaults:
  OUTPUT=$script_dir
  TEST_LIST=OUTPUT/kvm-selftests.list
EOF
}

case " ${*:-} " in
*" --help "* | *" -h "*)
	usage
	exit 0
	;;
esac

mkdir -p -- "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd -P)

case "$test_list" in
/*) ;;
*) test_list=$PWD/$test_list ;;
esac

list_dir=$(dirname -- "$test_list")
mkdir -p -- "$list_dir"
list_dir=$(CDPATH= cd -- "$list_dir" && pwd -P)
test_list=$list_dir/$(basename -- "$test_list")
tmp_list=$(mktemp "$test_list.tmp.XXXXXX")
trap 'rm -f -- "$tmp_list"' EXIT HUP INT TERM

make -C "$script_dir" OUTPUT="$output_dir" "$@" all
make -s --no-print-directory -C "$script_dir" OUTPUT="$output_dir" \
	emit_test_binaries >"$tmp_list"

if [ ! -s "$tmp_list" ]; then
	echo "$0: no KVM selftest binaries were emitted" >&2
	exit 1
fi

while IFS= read -r test; do
	if [ ! -x "$test" ]; then
		echo "$0: built test is missing or not executable: $test" >&2
		exit 1
	fi
done <"$tmp_list"

mv -f -- "$tmp_list" "$test_list"
trap - EXIT HUP INT TERM
printf 'Wrote %s KVM selftests to %s\n' "$(wc -l <"$test_list")" "$test_list"
