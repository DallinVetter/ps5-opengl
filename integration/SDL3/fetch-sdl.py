#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Fetch the pinned SDL3 source; never reset an existing working tree."""
import argparse
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PIN = json.loads((HERE / "sdl-pin.json").read_text())


def git(path, *args):
    return subprocess.check_output(
        ["git", "-c", f"safe.directory={path}", "-C", str(path), *args], text=True).strip()


def verify(path):
    """An unmodified checkout of exactly the pinned commit and tree."""
    if git(path, "rev-parse", "HEAD") != PIN["revision"]:
        raise ValueError(f"{path}: not the pinned SDL revision; refusing to replace it")
    if git(path, "rev-parse", "HEAD^{tree}") != PIN["tree"]:
        raise ValueError(f"{path}: pinned SDL tree mismatch")
    if git(path, "status", "--porcelain"):
        raise ValueError(f"{path}: SDL checkout has local changes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", type=Path, default=ROOT / "third_party/SDL")
    args = parser.parse_args()
    path = args.dest.resolve()
    if not path.exists():
        path.mkdir(parents=True)
        git(path, "init", "--quiet")
        git(path, "remote", "add", "origin", PIN["url"])
        git(path, "fetch", "--quiet", "--depth=1", "origin", PIN["revision"])
        git(path, "checkout", "--quiet", "--detach", "FETCH_HEAD")
    verify(path)
    print(f"SDL {PIN['version']}: pinned {PIN['revision']} at {path}")


if __name__ == "__main__":
    main()
