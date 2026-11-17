#!/usr/bin/env bash

set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_dir"

output_dir="build/debs"
if [[ $# -gt 0 && $1 != -* ]]; then
    output_dir=$1
    shift
fi

mkdir -p -- "$output_dir"
output_dir="$(cd -- "$output_dir" && pwd)"

version="$(dpkg-parsechangelog -S Version)"
version_without_epoch="${version#*:}"
arch="$(dpkg-architecture -qDEB_BUILD_ARCH)"
artifact_base="kolourpaint_${version_without_epoch}_${arch}"

DEB_OUTPUT_DIR="$output_dir" dpkg-buildpackage \
    --build=binary \
    --post-clean \
    --no-sign \
    --buildinfo-option="-u$output_dir" \
    --changes-option="-u$output_dir" \
    --buildinfo-file="$output_dir/$artifact_base.buildinfo" \
    --changes-file="$output_dir/$artifact_base.changes" \
    "$@"

printf '\nDebian package artifacts written to:\n%s\n' "$output_dir"
find "$output_dir" -maxdepth 1 -type f \
    \( -name '*.deb' -o -name '*.ddeb' -o -name '*.changes' -o -name '*.buildinfo' \) \
    -printf '  %f\n' | sort
