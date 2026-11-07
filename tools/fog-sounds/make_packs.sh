#!/bin/bash
# Builds the Fog sound packs from DDNet's own sounds (data/audio, CC BY-SA) with ffmpeg filters.
# Output: data/fog/sounds/<pack>/<same file name>.wv  (music_menu is left alone)
set -euo pipefail
cd "$(dirname "$0")/../.."
SRC=data/audio
OUT=data/fog/sounds
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

declare -A PACKS=(
	[fog]="lowpass=f=4200,aecho=0.7:0.55:70|140:0.22|0.12,volume=0.92"
	[crisp]="highpass=f=160,treble=g=5,acompressor=threshold=0.25:ratio=3:attack=5:release=60"
	[retro]="aresample=11025,acrusher=bits=5:mode=lin:mix=1,aresample=RATE"
	[deep]="asetrate=RATE*0.82,aresample=RATE,lowpass=f=5500"
	[arcade]="asetrate=RATE*1.25,aresample=RATE,acrusher=bits=9:mix=0.35"
	[quiet]="volume=0.42,lowpass=f=6500"
)

for Pack in "${!PACKS[@]}"; do
	mkdir -p "$OUT/$Pack"
	for File in "$SRC"/*.wv; do
		Name=$(basename "$File")
		[[ $Name == music_menu* ]] && continue
		Rate=$(ffprobe -v error -show_entries stream=sample_rate -of csv=p=0 "$File")
		Filter=${PACKS[$Pack]//RATE/$Rate}
		ffmpeg -v error -y -i "$File" -af "$Filter" -ar "$Rate" -ac 1 -sample_fmt s16 "$TMP/$Name.wav"
		wavpack -q -h -y "$TMP/$Name.wav" -o "$OUT/$Pack/$Name" >/dev/null
	done
	echo "$Pack: $(ls "$OUT/$Pack" | wc -l) sounds"
done
