#!/bin/bash
# capk.sh NAME SLOT "keys" [wait]  (keys via lv.sh k; HOLD 0.25)
N=$1; S=$2; K=$3; W=${4:-3}
D=/tmp/claude-0/cap/$N; rm -rf $D; mkdir -p $D
pkill -f build/melonDS; sleep 1
DUMP3D=$D /home/claude/tools/lv.sh start
/home/claude/tools/lv.sh k "F$S"; sleep 7
HOLD=${H:-0.6} /home/claude/tools/lv.sh k "$K"
sleep $W
/home/claude/tools/lv.sh shot $N
python3 /home/claude/wc2009-hd/wc09/tools/dump3d_png.py $D $D/png $D/sheet.png >/dev/null
echo "$N $(ls $D/*.rgb6a5 | wc -l) textures"
