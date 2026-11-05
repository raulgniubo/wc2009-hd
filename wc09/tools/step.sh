#!/bin/bash
# step.sh IN OUT "seq"  -> runs, saves OUT.st, OUT.png, and a strip OUT_strip.png
cd /home/claude/wc09/run
rm -f ${2}_0*.png
python3 /home/claude/tools/ds.py run --state $1.st --seq "$3" --shot $2.png --save $2.st --shots-every ${4:-60} > $2.log 2>&1
python3 -I - "$2" <<'PY'
import sys,glob
from PIL import Image
fs=sorted(glob.glob(sys.argv[1]+'_0*.png'))[-8:]+[sys.argv[1]+'.png']
ims=[Image.open(f) for f in fs]; w,h=ims[0].size
s=Image.new('RGB',(w*len(ims),h))
for i,im in enumerate(ims): s.paste(im,(i*w,0))
s.save(sys.argv[1]+'_strip.png')
PY
grep WC09TEX $2.log > $2.tex; python3 -I /home/claude/tools/texmatch.py $2.tex | tail -1
