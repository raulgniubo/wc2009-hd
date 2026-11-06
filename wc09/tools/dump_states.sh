#!/bin/bash
# dump_states.sh OUTDIR state1 state2 ...   -> runs each savestate in the GL build with WC09_DUMP3D
OUT=$1; shift; mkdir -p $OUT
T=/root/.config/melonDS/melonDS.toml
sed -i 's/^ScaleFactor = .*/ScaleFactor = 1/' $T
for s in "$@"; do
  cp "$s" /home/claude/livetest/wc09.ml8
  WC09_DUMP3D=$OUT WW=600 WH=480 /home/claude/tools/live.sh start >/dev/null
  sleep 10; HOLD=1 /home/claude/tools/live.sh keys "F8 w${WAIT:-10}"
  echo "$s $(ls $OUT | wc -l)"
done
/home/claude/tools/live.sh stop 2>/dev/null; pkill -f build/melonDS
sed -i 's/^ScaleFactor = .*/ScaleFactor = 4/' $T
echo "dump done"
