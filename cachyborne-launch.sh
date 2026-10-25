#!/usr/bin/env bash
# The profile symlink atomically identifies both JSON and the prepared shell plan.
set -euo pipefail
profile=${XDG_CONFIG_HOME:-$HOME/.config}/cachyborne/game_profile.json
if [[ -L $profile ]]; then
    prepared=$(readlink -- "$profile")
    plan=${prepared%/*}/launch.sh
    if [[ -f $plan ]]; then
        source "$plan"
        exit 1
    fi
fi
# Legacy JSON, diagnostics or an unconfigured installation.
here=$(dirname -- "$(readlink -f -- "$0")")
exec python3 "$here/launcher/cachyborne.py" launch "$@"
