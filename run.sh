#!/usr/bin/env bash
# Preparation is explicit: cachyborne-setup.sh GAME_DIR.
set -euo pipefail
exec "$(dirname -- "$(readlink -f -- "$0")")/cachyborne-launch.sh" "$@"
