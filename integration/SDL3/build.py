#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Offline SDL3 build against a verified public SDK; all outputs are isolated."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PIN = json.loads((HERE / "sdl-pin.json").read_text())
# The SDK contract does not depend on the SDL version: reuse the SDL2 lane's verifier.
spec = importlib.util.spec_from_file_location("sdl2_build", HERE.parent / "SDL2/build.py")
SDL2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(SDL2)
PATCH = "ps5-platform.patch"
NATIVE_ARTIFACTS = ("cmake/sdl/libSDL3.a", "cmake/CMakeFiles/sdl3-ps5-example.dir/example.c.obj")


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("host", "native"))
    parser.add_argument("--sdl-source", required=True, type=Path)
    parser.add_argument("--sdk-prefix", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--payload-sdk", type=Path)
    parser.add_argument("--compiler-wrapper", type=Path)
    args = parser.parse_args()
    source, prefix, out = (p.resolve() for p in (args.sdl_source, args.sdk_prefix, args.out))
    # Never write into the source cache, SDK, another checkout or an existing stage.
    if not out.is_relative_to(ROOT / "build") or out == ROOT / "build" or out.exists():
        parser.error("--out must be a new directory below this clone's build/")
    inputs = [source, prefix, *(p.resolve() for p in (args.payload_sdk, args.compiler_wrapper) if p)]
    if any(out.is_relative_to(p) or p.is_relative_to(out) for p in inputs):
        parser.error("output overlaps a read-only input")
    if args.mode == "native" and (not args.payload_sdk or not args.compiler_wrapper):
        parser.error("native requires --payload-sdk and --compiler-wrapper")
    SDL2.verify_sdk(prefix)
    git = ["git", "-c", f"safe.directory={source}", "-C", source]
    run(*git, "cat-file", "-e", f"{PIN['revision']}^{{commit}}")
    tree = subprocess.check_output([str(a) for a in (*git, "rev-parse", f"{PIN['revision']}^{{tree}}")],
                                   text=True).strip()
    if tree != PIN["tree"]:
        raise ValueError("Pinned SDL tree mismatch")
    out.mkdir(parents=True)
    lane = out / "integration"
    shutil.copytree(HERE, lane, ignore=shutil.ignore_patterns("__pycache__"))
    snapshot = out / "SDL"
    snapshot.mkdir()
    archive = out / "sdl-source.tar"
    with archive.open("wb") as stream:
        run(*git, "archive", PIN["revision"], stdout=stream)
    with tarfile.open(archive) as tar:
        tar.extractall(snapshot, filter="data")
    # Export the pinned commit rather than the cache's working tree.
    # Give git apply a repository boundary: otherwise it can silently ignore
    # paths when this exported source lives below the parent clone's worktree.
    run("git", "init", "--quiet", snapshot)
    run("git", "apply", "--check", lane / PATCH, cwd=snapshot)
    run("git", "apply", lane / PATCH, cwd=snapshot)
    if "&PS5_bootstrap" not in (snapshot / "src/video/SDL_video.c").read_text():
        raise SystemExit("SDL platform patch was not applied")
    configure = ["cmake", "-S", lane, "-B", out / "cmake", "-G", "Ninja",
                 f"-DSDL_SOURCE={snapshot}", f"-DPS5_OPENGL_PREFIX={prefix}",
                 "-DCMAKE_BUILD_TYPE=Release"]
    # SDL reads its hints from the environment; the contract must not inherit any.
    env = {k: v for k, v in os.environ.items() if not k.startswith("SDL_")}
    if args.mode == "host":
        configure += ["-DPS5_SDL3_HOST_TEST=ON"]
    else:
        env["PS5_PAYLOAD_SDK"] = str(args.payload_sdk.resolve())
        # The boilerplate's wrapper is a shell script that need not be executable.
        compiler = out / "prospero-cc"
        compiler.write_text(f'#!/bin/sh\nexec sh "{args.compiler_wrapper.resolve()}" "$@"\n')
        compiler.chmod(0o755)
        configure += ["-DCMAKE_SYSTEM_NAME=Generic", "-DCMAKE_SYSTEM_PROCESSOR=x86_64",
                      "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                      f"-DCMAKE_C_COMPILER={compiler}",
                      "-DCMAKE_C_FLAGS=-fPIC -ffunction-sections -fdata-sections "
                      f"-ffile-prefix-map={out}=."]
    run(*configure, env=env)
    run("cmake", "--build", out / "cmake", "--parallel", "8", env=env)
    if args.mode == "host":
        run("ctest", "--test-dir", out / "cmake", "--output-on-failure",
            env={**env, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                 "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
    else:
        for artifact in NATIVE_ARTIFACTS:
            if not (out / artifact).is_file():
                raise SystemExit(f"Missing native artifact: {artifact}")
    print(f"SDL3 {args.mode} build: {out / 'cmake'}")


if __name__ == "__main__":
    main()
