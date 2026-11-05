import csv,sys
T={}
for line in open('/home/claude/wc09/texhash_us.tsv'):
    k,h,i=line.split(); T[h]=(k,int(i))
names={int(r['index']):r['name'] for r in csv.DictReader(open('/home/claude/wc09/cards_us.csv'))}
hit=miss=0
for line in open(sys.argv[1]):
    h=line.split('hash=')[1].split()[0]; m=T.get(h)
    if m: hit+=1
    else: miss+=1
    print(line.strip()[8:], '->', f"{m[0]} #{m[1]} {names[m[1]]}" if m else 'no match')
print('hits',hit,'misses',miss)
