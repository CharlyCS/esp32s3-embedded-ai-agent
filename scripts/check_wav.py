#!/usr/bin/env python3

from pathlib import Path
import sys
import wave


ROOT = Path(__file__).resolve().parents[1]
AUDIO_DIR = ROOT / "data" / "audio"
EXPECTED = ("inicio.wav", "bienvenida.wav")


def validate(path: Path) -> bool:
    if not path.exists():
        print(f"[FALTA] {path}")
        return False

    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            compression = wav.getcomptype()
            frames = wav.getnframes()

        valid = (
            channels == 1
            and sample_width == 2
            and sample_rate == 16000
            and compression == "NONE"
            and frames > 0
        )

        state = "OK" if valid else "INVALIDO"
        print(
            f"[{state}] {path.name}: "
            f"canales={channels}, bits={sample_width * 8}, "
            f"Hz={sample_rate}, compresion={compression}, frames={frames}"
        )
        return valid
    except (wave.Error, EOFError, OSError) as exc:
        print(f"[INVALIDO] {path}: {exc}")
        return False


def main() -> int:
    results = [validate(AUDIO_DIR / name) for name in EXPECTED]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
