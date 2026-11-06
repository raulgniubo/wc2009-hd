#!/bin/bash
# capture.sh NAME SLOT "keys" [wait] -> dumps 3D textures into /tmp/claude-0/cap/NAME, screenshot NAME_b/t
N=$1; S=$2; K=$3; W=${4:-6}
D=/tmp/claude-0/cap/$N; rm -rf $D; mkdir -p $D
pkill -f build/melonDS; sleep 1
DUMP=$D/dump2d.bin DUMP3D=$D /home/claude/tools/lv.sh start
/home/claude/tools/lv.sh k "F$S"; sleep 8
for t in $K; do case $t in w*) sleep ${t#w};; k:*) HOLD=0.4 /home/claude/tools/lv.sh k "${t#k:}"; sleep 0.8;; *) /home/claude/tools/tap2.sh ${t%,*} ${t#*,} 0.25; sleep 0.8;; esac; done
sleep $W
/home/claude/tools/lv.sh shot $N; cp $D/dump2d.bin $D/final2d.bin
python3 /home/claude/wc2009-hd/wc09/tools/dump3d_png.py $D $D/png $D/sheet.png >/dev/null
echo "$N $(ls $D/*.rgb6a5 | wc -l) textures"
