#!/usr/bin/env python3
"""Append the legacy AOSP BootSignature trailer used by Fujisan's bootloader.

The P996A26 bootloader accepts a v1 BootSignature DER sequence immediately
following the aligned legacy Android boot image.  The sequence contains the
matching X.509 certificate, SHA-256/RSA algorithm identifier, authenticated
attributes (partition name and signable image length), and a PKCS#1 v1.5 RSA
signature over ``image || authenticated_attributes``.
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

ANDROID_MAGIC = b"ANDROID!"
HEADER_V0 = 0
SHA1_WITH_RSA_OID = bytes.fromhex("06092a864886f70d010105")
# The accepted Fujisan legacy trailer identifies SHA1withRSA but the original
# Android image was signed with SHA256withRSA. Preserve that device behavior.
SIGNATURE_DIGEST = "sha256"


def der_length(length: int) -> bytes:
    if length < 0x80:
        return bytes((length,))
    encoded = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes((0x80 | len(encoded),)) + encoded


def der(tag: int, payload: bytes) -> bytes:
    return bytes((tag,)) + der_length(len(payload)) + payload


def der_integer(value: int) -> bytes:
    if value < 0:
        raise ValueError("negative DER INTEGER is unsupported")
    if value == 0:
        encoded = b"\0"
    else:
        encoded = value.to_bytes((value.bit_length() + 7) // 8, "big")
    if encoded[0] & 0x80:
        encoded = b"\0" + encoded
    return der(0x02, encoded)


def der_printable(value: str) -> bytes:
    return der(0x13, value.encode("ascii"))


def aligned_size(value: int, page_size: int) -> int:
    return ((value + page_size - 1) // page_size) * page_size


def legacy_signable_size(image: bytes) -> int:
    if len(image) < 44 or image[:8] != ANDROID_MAGIC:
        raise ValueError("input is not an Android boot image")
    kernel_size, _, ramdisk_size, _, second_size, _, _, page_size, version = struct.unpack_from(
        "<9I", image, 8
    )
    if version != HEADER_V0:
        raise ValueError(f"only legacy boot header v0 is supported, got {version}")
    if page_size == 0 or page_size & (page_size - 1):
        raise ValueError(f"invalid boot page size: {page_size}")
    return page_size + sum(
        aligned_size(size, page_size)
        for size in (kernel_size, ramdisk_size, second_size)
    )


def certificate_der(certificate: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="fujisan-boot-sign-") as directory:
        output = Path(directory) / "certificate.der"
        subprocess.run(
            ["openssl", "x509", "-in", str(certificate), "-outform", "DER", "-out", str(output)],
            check=True,
        )
        return output.read_bytes()


def rsa_sha256_sign(payload: bytes, key: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="fujisan-boot-sign-") as directory:
        message = Path(directory) / "message.bin"
        signature = Path(directory) / "signature.bin"
        message.write_bytes(payload)
        subprocess.run(
            ["openssl", "dgst", f"-{SIGNATURE_DIGEST}", "-sign", str(key), "-out", str(signature), str(message)],
            check=True,
        )
        return signature.read_bytes()


def make_boot_signature(image: bytes, partition: str, key: Path, certificate: Path) -> bytes:
    signable_size = legacy_signable_size(image)
    if len(image) != signable_size:
        raise ValueError(
            f"input has {len(image)} bytes, expected exactly {signable_size} aligned bytes; "
            "sign the unsigned mkbootimg output before appending any trailer"
        )

    authenticated_attributes = der(
        0x30,
        der_printable(partition) + der_integer(signable_size),
    )
    signature = rsa_sha256_sign(image + authenticated_attributes, key)
    algorithm = der(0x30, SHA1_WITH_RSA_OID + der(0x05, b""))
    certificate_bytes = certificate_der(certificate)

    return der(
        0x30,
        der_integer(1)
        + certificate_bytes
        + algorithm
        + authenticated_attributes
        + der(0x04, signature),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--partition", choices=("/boot", "/recovery"), required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--certificate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    image = args.input.read_bytes()
    trailer = make_boot_signature(image, args.partition, args.key, args.certificate)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image + trailer)
    print(f"signed {args.partition}: image={len(image)} trailer={len(trailer)} output={args.output}")


if __name__ == "__main__":
    main()
