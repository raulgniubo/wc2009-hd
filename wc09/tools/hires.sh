#!/bin/bash
# hires.sh "slot:name slot:name ..." -> 4x screenshots /tmp/claude-0/hi_<name>.png (top+bottom side by side)
export DISPLAY=:99
pkill -f build/melonDS; sleep 1
sed -i 's/^ScaleFactor = .*/ScaleFactor = 4/' /root/.config/melonDS/melonDS.toml
XS=2200x900 bash /home/claude/wc2009-hd/wc09/tools/live.sh start >/dev/null
W=$(xdotool search --onlyvisible --name melonDS | head -1); xdotool windowsize $W 2100 820; sleep 2
for it in $1; do
  s=${it%%:*}; n=${it#*:}
  bash /home/claude/wc2009-hd/wc09/tools/live.sh keys "F$s"; sleep ${WAIT:-25}
  import -window root /tmp/claude-0/full4.png
  python3 -c "
from PIL import Image; Image.open('/tmp/claude-0/full4.png').crop((0,20,2100,808)).save('/tmp/claude-0/hi_$n.png')"
done
pkill -f build/melonDS; sleep 1
sed -i 's/^ScaleFactor = .*/ScaleFactor = 2/' /root/.config/melonDS/melonDS.toml
