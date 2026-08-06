#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Uso: $0 <audio_inicio> <audio_bienvenida>"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUDIO_DIR="${ROOT_DIR}/data/audio"
mkdir -p "$AUDIO_DIR"

ffmpeg -y -i "$1" -ar 16000 -ac 1 -c:a pcm_s16le \
  "${AUDIO_DIR}/inicio.wav"

ffmpeg -y -i "$2" -ar 16000 -ac 1 -c:a pcm_s16le \
  "${AUDIO_DIR}/bienvenida.wav"

python3 "${ROOT_DIR}/scripts/check_wav.py"
