#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import stat
import sys


ROOT = Path(__file__).resolve().parents[1]
CERT_DIR = ROOT / "aws" / "certs"


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"{label} no existe: {path}")


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="strict")


def validate_pem(path: Path, expected_tokens: tuple[str, ...], label: str) -> None:
    text = read_text(path)
    if not any(token in text for token in expected_tokens):
        raise ValueError(f"{label} no parece un PEM valido: {path}")


def copy_secure(source: Path, destination: Path, private: bool = False) -> None:
    shutil.copyfile(source, destination)
    if private:
        destination.chmod(stat.S_IRUSR | stat.S_IWUSR)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Instala y renombra los certificados de un Thing AWS IoT."
    )
    parser.add_argument("--root-ca", required=True, type=Path)
    parser.add_argument("--certificate", required=True, type=Path)
    parser.add_argument("--private-key", required=True, type=Path)
    args = parser.parse_args()

    require_file(args.root_ca, "Root CA")
    require_file(args.certificate, "Certificado")
    require_file(args.private_key, "Clave privada")

    validate_pem(
        args.root_ca,
        ("BEGIN CERTIFICATE",),
        "Root CA",
    )
    validate_pem(
        args.certificate,
        ("BEGIN CERTIFICATE",),
        "Certificado",
    )
    validate_pem(
        args.private_key,
        (
            "BEGIN PRIVATE KEY",
            "BEGIN RSA PRIVATE KEY",
            "BEGIN EC PRIVATE KEY",
        ),
        "Clave privada",
    )

    CERT_DIR.mkdir(parents=True, exist_ok=True)

    copy_secure(args.root_ca, CERT_DIR / "AmazonRootCA1.pem")
    copy_secure(
        args.certificate,
        CERT_DIR / "device-certificate.pem.crt",
    )
    copy_secure(
        args.private_key,
        CERT_DIR / "device-private.pem.key",
        private=True,
    )

    print("Certificados instalados:")
    print(f"  {CERT_DIR / 'AmazonRootCA1.pem'}")
    print(f"  {CERT_DIR / 'device-certificate.pem.crt'}")
    print(f"  {CERT_DIR / 'device-private.pem.key'}")
    print("\nNo subas estos archivos a Git.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
