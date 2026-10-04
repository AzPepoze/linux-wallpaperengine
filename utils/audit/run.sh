#!/bin/bash
# Screenshot wallpapers with the release build by grabbing each app window with grim.
# Windows must be visible to render, so run this on a workspace you are not using; it opens one window per wallpaper.
# usage: utils/audit/run.sh <workshop-content-dir> <out-dir> [id ...]   (utils/out/ is a git-ignored place for <out-dir>)
# Needs: hyprctl, grim, ffmpeg, python3. Resumable: finished ids are skipped.
# LWE_SETTLE=<secs> wait after init before the shot (default 0.5); LWE_MOTION=1 adds a second shot.
set -u
export LWE_NO_AUDIO=1
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${LWE_BIN:-$ROOT/bin/release/linux-wallpaperengine}
CONTENT=$1; OUT=$2; shift 2
mkdir -p "$OUT"
ids=("$@"); [ ${#ids[@]} -eq 0 ] && mapfile -t ids < <(ls "$CONTENT")
for id in "${ids[@]}"; do
  [ -f "$OUT/$id/done" ] && continue
  mkdir -p "$OUT/$id"; cd "$OUT/$id" || continue
  cp "$ROOT/config.json" .
  "$BIN" "$CONTENT/$id" > run.log 2>&1 & pid=$!
  for _ in $(seq 1 400); do
    sleep 0.05
    grep -q Initialized run.log 2>/dev/null && break
    kill -0 $pid 2>/dev/null || break
  done
  sleep ${LWE_SETTLE:-0.5}
  geom=$(hyprctl clients -j | PID=$pid python3 -c "import json,sys,os
for c in json.load(sys.stdin):
    if c['pid']==int(os.environ['PID']): print('%d,%d %dx%d'%(c['at'][0],c['at'][1],c['size'][0],c['size'][1])); break")
  [ -n "$geom" ] && grim -g "$geom" shot.png
  [ -n "${LWE_MOTION:-}" ] && [ -n "$geom" ] && { sleep 1; grim -g "$geom" shot2.png; }
  kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null; rm -rf extracted
  ref=$(ls "$CONTENT/$id"/preview.* 2>/dev/null | head -1)
  [ -n "$ref" ] && ffmpeg -loglevel error -y -i "$ref" -frames:v 1 preview.png
  touch done
done
