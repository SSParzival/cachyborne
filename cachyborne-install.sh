#!/usr/bin/env bash
# Installs dependencies system-wide and commands for the invoking user.
set -euo pipefail
here=$(dirname -- "$(readlink -f -- "$0")")
cd -- "$here"
[[ -f /etc/arch-release ]] || { echo 'Requires CachyOS/Arch Linux.' >&2; exit 1; }
(( EUID != 0 )) || { echo 'Run as your desktop user; pacman uses sudo.' >&2; exit 1; }
user_only=0
skip_build=0
while (( $# )); do
    case $1 in
        --user) user_only=1 ;;
        --skip-build) skip_build=1 ;;
        --help) echo 'Usage: cachyborne-install.sh [--user] [--skip-build]'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if (( ! user_only )); then
    sudo pacman -Syu --needed clang llvm mold ninja cmake pkgconf vulkan-radeon vulkan-headers \
        vulkan-icd-loader sdl3 libadwaita gtk4 python python-gobject gamemode gamescope rsync \
        fmt boost robin-map xxhash libx11 miniz zydis \
        glslang ffmpeg git protobuf nlohmann-json openssl zlib miniupnpc
else
    echo 'Installing for this user with existing system/private-prefix dependencies.'
fi
export CC=clang CXX=clang++ BB_LTO=ON BB_PGO=off
if (( ! skip_build )); then bash build.sh; fi
[[ -x out/bb-probe && -f out/gpu/libbbgpu.so ]] || { echo "Build artifacts missing; run build.sh." >&2; exit 1; }
bin=${HOME}/.local/bin
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$bin" "$apps"
ln -sfn "$here/cachyborne-setup.sh" "$bin/cachyborne-setup"
ln -sfn "$here/cachyborne-launch.sh" "$bin/cachyborne-launch"
ln -sfn "$here/cachyborne-gui" "$bin/cachyborne-gui"
# One implementation generates both menu and desktop entries and preserves local artwork.
python3 launcher/cachyborne.py desktop --desktop --download-icon
if command -v update-desktop-database >/dev/null; then update-desktop-database "$apps"; fi
printf 'Installed. Keep this checkout at %s and add ~/.local/bin to PATH.\n' "$here"
printf 'Prepare: cachyborne-setup /path/to/app0\nPlay: cachyborne-launch\nSettings: cachyborne-gui\n'
