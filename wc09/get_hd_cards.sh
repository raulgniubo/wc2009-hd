#!/bin/bash
# Downloads full HD card images for every WC2009 (US) card that has a passcode,
# from YGOPRODeck's image server, into an HDCards folder.
#
# Usage:  bash get_hd_cards.sh [HDCards folder]
#   Default folder: ./HDCards (put it next to your ROM).
#
# Safe to re-run: files you already have are skipped, so each image is pulled
# only once (YGOPRODeck asks clients to download once and store locally).
# Rate: about 8 images per second, well under their 20 requests/second limit.

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LIST="$HERE/passcodes_us.txt"
OUT="${1:-./HDCards}"
URL="https://images.ygoprodeck.com/images/cards"

[ -f "$LIST" ] || { echo "Can't find $LIST (keep it next to this script)."; exit 1; }
mkdir -p "$OUT"

total=$(grep -c . "$LIST"); n=0; got=0; skip=0; miss=0
: > "$OUT/_missing.txt"
while read -r pc; do
  [ -z "$pc" ] && continue
  n=$((n+1))
  if [ -s "$OUT/$pc.jpg" ] || [ -s "$OUT/$pc.png" ]; then skip=$((skip+1)); continue; fi
  if curl -fsL --retry 2 -o "$OUT/$pc.jpg.part" "$URL/$pc.jpg"; then
    mv "$OUT/$pc.jpg.part" "$OUT/$pc.jpg"; got=$((got+1))
  else
    rm -f "$OUT/$pc.jpg.part"; echo "$pc" >> "$OUT/_missing.txt"; miss=$((miss+1))
  fi
  printf "\r%d/%d  downloaded %d  already had %d  not found %d" "$n" "$total" "$got" "$skip" "$miss"
  sleep 0.12
done < "$LIST"
echo
echo "Done. Images are in: $OUT"
[ "$miss" -gt 0 ] && echo "$miss passcodes weren't on YGOPRODeck; they're listed in $OUT/_missing.txt (those cards keep the original art)."
exit 0
