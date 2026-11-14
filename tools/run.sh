#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$(uname -m)" in
    arm64) platform=arm64-macos ;;
    *) platform=x86_64-macos ;;
esac
exec "$project_dir/build/$platform/dmengine" "$@" "$project_dir/build/default/game.projectc"
