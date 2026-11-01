#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Adapted from bbport PR #165. Explicit PID; detach by default, terminate only on request.
set -euo pipefail
root=$(dirname -- "$(dirname -- "$(readlink -f -- "$0")")")
keep=1
pid=
for arg in "$@"; do
    case $arg in
        --keep) keep=1 ;;
        --terminate) keep=0 ;;
        --help) echo 'Usage: tools/debug_attach.sh PID [--keep|--terminate]'; exit 0 ;;
        *) [[ -z $pid && $arg =~ ^[1-9][0-9]*$ ]] || { echo 'Expected one numeric bb-probe PID.' >&2; exit 2; }; pid=$arg ;;
    esac
done
[[ -n $pid ]] || { echo 'Provide the bb-probe PID printed at the fault.' >&2; exit 2; }
command -v gdb >/dev/null || { echo 'Install gdb to capture the fault.' >&2; exit 2; }
process_exe=$(readlink -- "/proc/$pid/exe") || { echo 'Process no longer exists.' >&2; exit 1; }
process_exe=${process_exe% (deleted)}
[[ ${process_exe##*/} == bb-probe ]] || { echo 'PID is not a bb-probe process.' >&2; exit 2; }
umask 077
logs=${XDG_STATE_HOME:-$HOME/.local/state}/cachyborne/debug
mkdir -p -- "$logs"
report=$(mktemp "$logs/crash-$(date +%Y%m%d_%H%M%S)-$pid.XXXXXX.txt")
BB_DEBUG_OUT=$report BB_DEBUG_KEEP=$keep gdb -nx -batch -q -iex 'set debuginfod enabled off' -p "$pid" -x "$root/tools/debug_attach.py"
[[ -s $report ]] || { echo "Empty debugger report: $report" >&2; exit 1; }
printf 'Debugger report: %s\n' "$report"
