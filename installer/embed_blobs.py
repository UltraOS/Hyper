#!/usr/bin/env python3
"""Populate the hyper_install Python template with the boot blobs.

Reads the MBR, ISO MBR and stage2 binaries, deflate-compresses and base64-encodes
them and substitutes them into hyper_install.py.in to produce a standalone,
executable installer script that can be shipped as-is to any platform with a
Python 3 interpreter. Raw zlib (rather than gzip) keeps the output deterministic
- the gzip container stamps an mtime, so it would differ on every build.
"""
import argparse
import base64
import os
import stat
import zlib


def encode(path):
    with open(path, "rb") as f:
        data = f.read()

    if not data:
        raise SystemExit(f"{path} is empty!")

    return base64.b64encode(zlib.compress(data, 9)).decode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--mbr", required=True)
    parser.add_argument("--iso-mbr", required=True)
    parser.add_argument("--stage2", required=True)
    args = parser.parse_args()

    with open(args.template, "r") as f:
        text = f.read()

    replacements = {
        "@MBR_B64@": encode(args.mbr),
        "@ISO_MBR_B64@": encode(args.iso_mbr),
        "@STAGE2_B64@": encode(args.stage2),
    }

    for placeholder, value in replacements.items():
        if placeholder not in text:
            raise SystemExit(f"template is missing placeholder {placeholder}")
        text = text.replace(placeholder, value)

    with open(args.output, "w") as f:
        f.write(text)

    # Make the generated script directly runnable (chmod +x).
    mode = os.stat(args.output).st_mode
    os.chmod(args.output, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


if __name__ == "__main__":
    main()
