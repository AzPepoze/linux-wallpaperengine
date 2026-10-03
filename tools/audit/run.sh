#!/bin/bash
# Screenshot wallpapers with the release build, off-screen on a Hyprland headless output.
# usage: tools/audit/run.sh <workshop-content-dir> <out-dir> [id ...]
# Needs: hyprctl, grim, ffmpeg, python3. Resumable: finished ids are skipped.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${LWE_BIN:-$ROOT/bin/release/linux-wallpaperengine}
CONTENT=$1; OUT=$2; shift 2
mkdir -p "$OUT"
hyprctl output create headless LWE >/dev/null 2>&1
hyprctl eval 'hl.window_rule({ name="lwe", match={ title="^Linux Wallpaper Engine" }, workspace="5 silent", no_focus=true })' >/dev/null 2>&1
ids=("$@"); [ ${#ids[@]} -eq 0 ] && mapfile -t ids < <(ls "$CONTENT")
for id in "${ids[@]}"; do
  [ -f "$OUT/$id/done" ] && continue
  mkdir -p "$OUT/$id"; cd "$OUT/$id" || continue
  cp "$ROOT/config.json" .
  "$BIN" "$CONTENT/$id" > run.log 2>&1 & pid=$!
  for _ in $(seq 1 200); do
    sleep 0.1
    grep -q Initialized run.log 2>/dev/null && break
    kill -0 $pid 2>/dev/null || break
  done
  sleep 1.2
  geom=$(hyprctl clients -j | python3 -c "import json,sys
for c in json.load(sys.stdin):
    if c['title']=='Linux Wallpaper Engine': print('%d,%d %dx%d'%(c['at'][0],c['at'][1],c['size'][0],c['size'][1]))")
  [ -n "$geom" ] && grim -g "$geom" shot.png
  sleep 1.0; [ -n "$geom" ] && grim -g "$geom" shot2.png
  kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null; rm -rf extracted
  ref=$(ls "$CONTENT/$id"/preview.* 2>/dev/null | head -1)
  [ -n "$ref" ] && ffmpeg -loglevel error -y -i "$ref" -frames:v 1 preview.png
  touch done
done
