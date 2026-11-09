#!/usr/bin/env bash
# Pack build/homebrew-ps5/dist/FAKE77031 into the layout of an upstream Playden release zip:
# FAKE77031/ at the top, with tools/ and the player documents beside it.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
app="$root/build/homebrew-ps5/dist/FAKE77031"
[[ -f "$app/eboot.bin" && -f "$app/playden-version" && -f "$app/sce_sys/param.json" ]] || {
  echo "missing PS5 package at $app" >&2
  exit 1
}
version=$(tr -d '[:space:]' < "$app/playden-version")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-beta\.[0-9]+|-nightly\.[0-9]+\.[0-9]+)?$ ]] || {
  echo "unexpected playden-version: $version" >&2
  exit 1
}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp -a "$app" "$stage/FAKE77031"
mkdir -p "$stage/tools"
cp "$root/tools/console_title_deploy.py" "$root/tools/collect_logs.py" "$root/tools/console_kit_upload.py" "$stage/tools/"
cp "$root/README.md" "$root/COMPATIBILITY.md" "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$stage/"
out="$root/build/release/playden-ps5-$version.zip"
mkdir -p "$(dirname "$out")"
rm -f "$out"
(cd "$stage" && zip -r "$out" FAKE77031 tools README.md COMPATIBILITY.md LICENSE THIRD_PARTY_NOTICES.md)
echo "$out"
