#!/bin/bash
# live.sh start|keys "k1 k2 ..."|shot name|stop
export DISPLAY=:99
case $1 in
start)
  pkill -f "Xvfb :99" ; pkill -f build/melonDS; sleep 1
  Xvfb :99 -screen 0 ${XS:-1280x1024}x24 >/tmp/xvfb.log 2>&1 &
  sleep 2
  cd /home/claude/livetest && WC09_DEBUG=1 WC09_REGS=1 WC09_TXTDBG=1 WC09_DUMP=$DUMP WC09_DUMP3D=$DUMP3D WC09_TEXDBG=$TEXDBG WC09_HIDEBG=$HIDEBG stdbuf -oL /home/claude/melonDS-up/build/melonDS /home/claude/livetest/wc09.nds >/tmp/mds_live.log 2>&1 &
  sleep 6; W=$(xdotool search --onlyvisible --name melonDS | head -1); xdotool windowsize $W ${WW:-600} ${WH:-960}; xdotool windowmove $W 0 0; echo $W ;;
keys)
  W=$(xdotool search --onlyvisible --name melonDS | head -1); xdotool windowfocus $W 2>/dev/null
  for k in $2; do
    case $k in w*) sleep ${k#w};; *) xdotool keydown $k; sleep ${HOLD:-0.12}; xdotool keyup $k; sleep 0.25;; esac
  done ;;
tap)
  # tap X Y in DS bottom-screen coords (vertical layout, window at 0,0)
  W=$(xdotool search --onlyvisible --name melonDS | head -1)
  eval $(xdotool getwindowgeometry --shell $W)
  # find the screen area: assume content below a ~20px menu bar, two 4:3 screens stacked
  CH=$((HEIGHT-20)); SW=$WIDTH; SH=$((CH/2)); [ $((SW*3/4)) -lt $SH ] && SH=$((SW*3/4)); SW=$((SH*4/3)); OX=$(( (WIDTH-SW)/2 )); OY=$((20 + (CH-2*SH)/2))
  PX=$((OX + $2*SW/256)); PY=$((OY + SH + $3*SH/192))
  xdotool mousemove --window $W $PX $PY mousedown 1; sleep ${HOLD:-0.15}; xdotool mouseup 1 ;;
shot)
  import -window root /home/claude/livetest/$2.png ;;
esac
