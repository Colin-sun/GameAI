#!/usr/bin/env python3
"""Package the Chrome extension as a self-contained CRX3 or development ZIP."""

from __future__ import annotations

import argparse
from io import BytesIO
import hashlib
import math
from pathlib import Path
import secrets
import shutil
import struct
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]
EXTENSION = ROOT / "extension"
DEFAULT_OUTPUT = ROOT / "dist" / "gameai-extension"
EXCLUDED_DIRS = {"tests", "__pycache__"}
EXCLUDED_FILES = {"README.md"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", choices=("zip", "crx", "all"), default="crx")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def has_excluded_part(path: Path) -> bool:
    return any(part in EXCLUDED_DIRS for part in path.parts)


def copy_runtime(stage: Path) -> None:
    model_files = sorted((EXTENSION / "models").glob("*.bin"))
    if not model_files:
        raise SystemExit(
            "No converted prior model found. Run "
            "python scripts/extension/export_prior_model.py --force --json first."
        )

    for source in sorted(EXTENSION.rglob("*")):
        if not source.is_file():
            continue
        relative = source.relative_to(EXTENSION)
        if has_excluded_part(relative) or source.name in EXCLUDED_FILES:
            continue
        if relative.as_posix() == "wasm/engine.c":
            continue
        destination = stage / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def build_zip(stage: Path) -> bytes:
    payload = BytesIO()
    with ZipFile(payload, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for source in sorted(stage.rglob("*")):
            if not source.is_file():
                continue
            relative = source.relative_to(stage).as_posix()
            info = ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())
    return payload.getvalue()


def write_zip(payload: bytes, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)


def encode_varint(value: int) -> bytes:
    encoded = bytearray()
    while value >= 0x80:
        encoded.append((value & 0x7F) | 0x80)
        value >>= 7
    encoded.append(value)
    return bytes(encoded)


def protobuf_bytes(field_number: int, value: bytes) -> bytes:
    key = (field_number << 3) | 2
    return encode_varint(key) + encode_varint(len(value)) + value


def der_length(length: int) -> bytes:
    if length < 0x80:
        return bytes([length])
    raw = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(raw)]) + raw


def der_tlv(tag: int, value: bytes) -> bytes:
    return bytes([tag]) + der_length(len(value)) + value


def der_integer(value: int) -> bytes:
    raw = value.to_bytes((value.bit_length() + 7) // 8, "big")
    if not raw:
        raw = b"\x00"
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return der_tlv(0x02, raw)


def probable_prime(candidate: int) -> bool:
    if candidate < 2:
        return False
    for divisor in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if candidate == divisor:
            return True
        if candidate % divisor == 0:
            return False
    odd = candidate - 1
    rounds = 0
    while odd % 2 == 0:
        rounds += 1
        odd //= 2
    for base in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31):
        if base >= candidate:
            continue
        value = pow(base, odd, candidate)
        if value in (1, candidate - 1):
            continue
        for _ in range(rounds - 1):
            value = pow(value, 2, candidate)
            if value == candidate - 1:
                break
        else:
            return False
    return True


def generate_rsa_key() -> tuple[int, int, int]:
    public_exponent = 65537

    def generate_prime() -> int:
        while True:
            candidate = secrets.randbits(1024) | (1 << 1023) | 1
            if math.gcd(candidate - 1, public_exponent) == 1 and probable_prime(candidate):
                return candidate

    prime_p = generate_prime()
    prime_q = generate_prime()
    while prime_q == prime_p:
        prime_q = generate_prime()
    modulus = prime_p * prime_q
    totient = (prime_p - 1) * (prime_q - 1)
    private_exponent = pow(public_exponent, -1, totient)
    return modulus, public_exponent, private_exponent


def public_key_der(modulus: int, public_exponent: int) -> bytes:
    rsa_public_key = der_tlv(0x30, der_integer(modulus) + der_integer(public_exponent))
    algorithm = der_tlv(0x30, bytes.fromhex("06092a864886f70d0101010500"))
    subject_public_key_info = algorithm + der_tlv(0x03, b"\x00" + rsa_public_key)
    return der_tlv(0x30, subject_public_key_info)


def rsa_sha256_signature(data: bytes, modulus: int, private_exponent: int) -> bytes:
    digest_info_prefix = bytes.fromhex("3031300d060960864801650304020105000420")
    digest_info = digest_info_prefix + hashlib.sha256(data).digest()
    modulus_length = (modulus.bit_length() + 7) // 8
    padding_length = modulus_length - len(digest_info) - 3
    encoded_message = b"\x00\x01" + (b"\xFF" * padding_length) + b"\x00" + digest_info
    return pow(int.from_bytes(encoded_message, "big"), private_exponent, modulus).to_bytes(
        modulus_length,
        "big",
    )


def build_crx(payload: bytes) -> bytes:
    modulus, public_exponent, private_exponent = generate_rsa_key()
    public_key = public_key_der(modulus, public_exponent)
    crx_id = hashlib.sha256(public_key).digest()[:16]
    signed_header_data = protobuf_bytes(1, crx_id)
    signed_data = b"CRX3 Signed Data\x00" + signed_header_data + payload
    signature = rsa_sha256_signature(signed_data, modulus, private_exponent)
    proof = protobuf_bytes(1, public_key) + protobuf_bytes(2, signature)
    header = protobuf_bytes(2, proof) + protobuf_bytes(10000, signed_header_data)
    return b"Cr24" + struct.pack("<II", 3, len(header)) + header + payload


def write_crx(payload: bytes, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(build_crx(payload))


def main() -> None:
    args = parse_args()
    output_base = args.output.with_suffix("")
    output_zip = output_base.with_suffix(".zip")
    output_crx = output_base.with_suffix(".crx")

    with tempfile.TemporaryDirectory(prefix="gameai-extension-") as temporary:
        stage = Path(temporary) / "extension"
        copy_runtime(stage)
        payload = build_zip(stage)
        if args.format in ("zip", "all"):
            write_zip(payload, output_zip)
            print(f"Wrote {output_zip}")
        if args.format in ("crx", "all"):
            write_crx(payload, output_crx)
            print(f"Wrote {output_crx}")


if __name__ == "__main__":
    main()
