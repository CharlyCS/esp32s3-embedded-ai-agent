#!/usr/bin/env python3

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]

REQUIRED = (
    "aws/certs/AmazonRootCA1.pem",
    "aws/certs/device-certificate.pem.crt",
    "aws/certs/device-private.pem.key",
    "data/audio/inicio.wav",
    "data/audio/bienvenida.wav",
)


def main() -> int:
    missing: list[str] = []

    for relative in REQUIRED:
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            missing.append(relative)
            print(f"[FALTA] {relative}")
        else:
            print(f"[OK] {relative}")

    print("\nConfiguracion que debes completar con idf.py menuconfig:")
    print("  - Wi-Fi SSID/password")
    print("  - AWS IoT endpoint")
    print("  - ThingName, igual al MQTT clientId")
    print("  - Bearer token del HTTP MCP local")

    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
