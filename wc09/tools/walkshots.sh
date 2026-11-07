#!/bin/bash
# walkshots.sh STATE PREFIX "keys..." : 4x bottom-screen shots after each key hold
export DISPLAY=:99
cp -R /home/claude/wc09/ui_hd/* /home/claude/livetest/HDCards/
pkill -f build/melonDS; sleep 1
cp /home/claude/livetest/wc09.ml8 /tmp/claude-0/ml8.keep
sed -i 's/^ScaleFactor = .*/ScaleFactor = 4/' /root/.config/melonDS/melonDS.toml
XS=2200x900 bash /home/claude/wc2009-hd/wc09/tools/live.sh start >/dev/null
W=$(xdotool search --onlyvisible --name melonDS | head -1); xdotool windowsize $W 2100 820; sleep 2
cp /home/claude/wc09/run/$1.st /home/claude/livetest/wc09.ml8
bash /home/claude/wc2009-hd/wc09/tools/live.sh keys "F8"; sleep 7
i=0
for k in $3; do
  HOLD=${HOLD:-0.5} bash /home/claude/wc2009-hd/wc09/tools/live.sh keys "$k"; sleep 0.3
  import -window root /tmp/claude-0/full4.png
  python3 -c "
from PIL import Image; Image.open('/tmp/claude-0/full4.png').crop((1050,20,2100,808)).save('/tmp/claude-0/$2_$i.png')"
  i=$((i+1))
done
pkill -f build/melonDS; sleep 1
cp /tmp/claude-0/ml8.keep /home/claude/livetest/wc09.ml8
sed -i 's/^ScaleFactor = .*/ScaleFactor = 2/' /root/.config/melonDS/melonDS.toml
