#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Assemble an unlaunched native folder from an SDL3 native build."""
import argparse
import json
import os
from pathlib import Path
import shutil
from build import NATIVE_ARTIFACTS, PIN, ROOT, SDL2, run

SDK_CHECKER, digest = SDL2.SDK_CHECKER, SDL2.digest
# Must match ^egl_public_[A-Za-z0-9_]+\.o$ for tools/verify-native-test-app.sh.
SELECTED = "egl_public_core43_sdl3.o"


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"Native template contract changed: {path}: {old}")
    path.write_text(text.replace(old, new))


def folder_parameters(profile):
    """Called only after verifying the SDK manifest."""
    param = json.loads((ROOT / "native-app/param.json").read_text())
    language = param["localizedParameters"]["defaultLanguage"]
    param["localizedParameters"][language]["titleName"] = "SDL3 public SDK local candidate"
    if profile["fps"] > 60:
        param["attribute3"] = 0x80040
    return param


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-build", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--sdk-prefix", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--libc-prx", type=Path,
                        help="runtime libc.prx matching the template's libc.prx.sha256, "
                             "for a template checkout that has not built its own")
    args = parser.parse_args()
    native, template, prefix, out = (p.resolve() for p in
        (args.native_build, args.template, args.sdk_prefix, args.out))
    if (not out.is_relative_to(ROOT / "build") or out == ROOT / "build" or out.exists()
            or any(out.is_relative_to(p) or p.is_relative_to(out) for p in (native, template, prefix))):
        parser.error("--out must be a new directory in this clone's build/, outside inputs")
    sdk_identity = SDL2.verify_sdk(prefix)
    profile = SDK_CHECKER.display_profile(prefix)
    artifacts = [native / name for name in NATIVE_ARTIFACTS]
    for path in artifacts:
        if not path.is_file(): raise SystemExit(f"Missing native build artifact: {path}")
    native_identity = {name: digest(native / name) for name in NATIVE_ARTIFACTS}
    template_commit = run("git", "-c", f"safe.directory={template}", "-C", template,
                          "rev-parse", "HEAD", capture_output=True, text=True).stdout.strip()
    pinned = json.loads((ROOT / "dependencies.json").read_text())["native_boilerplate"]["revision"]
    if template_commit != pinned:
        raise SystemExit(f"Template is {template_commit}; the pinned native boilerplate is {pinned}")
    out.mkdir(parents=True)
    # Copy every potentially written dependency. No symlinks into canonical caches.
    for directory in ("runtime", "sce_sys", "tooling", "tools", ".deps/native"):
        shutil.copytree(template / directory, out / directory)
    if args.libc_prx:
        if (out / "runtime/libc.prx").exists():
            raise SystemExit("The template already has a runtime libc.prx; omit --libc-prx")
        shutil.copy2(args.libc_prx.resolve(), out / "runtime/libc.prx")
    # The template pins the only accepted runtime libc; nothing is rebuilt here.
    run("sha256sum", "--check", "--strict", "libc.prx.sha256", cwd=out / "runtime")
    sdk = out / ".deps/native/ps5-payload-sdk"
    for directory in ("src", "vendor", "build/native-imports"):
        (out / directory).mkdir(parents=True)
    for name in ("runtime_shims.c", "app_heap.c"):
        shutil.copy2(ROOT / "native-app" / name, out / "src" / name)
    shutil.copy2(out / "tooling/native/ps5-pie.ld", out / "tooling/native/ps5-pie-base.ld")
    for name in ("ps5-pie.ld", "app-symbols.map"):
        shutil.copy2(ROOT / "native-app" / name, out / "tooling/native" / name)
    param = folder_parameters(profile)
    (out / "sce_sys/param.json").write_text(json.dumps(param, indent=2) + "\n")
    replace_once(out / "tooling/native/sce_module_writer.cpp",
                 "write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());",
                 "write_u64(result.data, result.heap_size, 0x10000000ULL);")
    script = out / "tools/build.sh"
    replace_once(script, 'bash "$root/tools/setup-native-dependencies.sh" >/dev/null',
                 'test -x "$root/.deps/native/ps5-payload-sdk/bin/prospero-lld"')
    replace_once(script, '[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"',
                 'test -f "$root/runtime/libc.prx"')
    replace_once(script, "--eh-frame-hdr \\",
                 "--eh-frame-hdr --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free "
                 "--wrap=posix_memalign --wrap=malloc_usable_size \\")
    # Use the verified public SDK's imports, only in the private payload SDK copy.
    env = os.environ.copy()
    env["PS5_PAYLOAD_SDK"] = str(sdk)
    for name in ("libSceAgc.so", "libSceAgcDriver.so"):
        shutil.copy2(prefix / "lib" / name, sdk / "target/lib" / name)
    compiler_rt = Path(run("clang-18", "--print-resource-dir", capture_output=True,
                           text=True).stdout.strip()) / "lib/linux/libclang_rt.builtins-x86_64.a"
    libraries = [*artifacts, prefix / "lib/libPS5OpenGLCore33.a"]
    libraries += [sdk / f"target/lib/{name}" for name in ("libunwind.a", "libc++abi.a", "libc++.a")]
    libraries += [compiler_rt]
    for path in libraries:
        if not path.is_file(): raise SystemExit(f"Missing link input: {path}")
    group = (f'SEARCH_DIR("{sdk}/target/lib")\nSEARCH_DIR("{prefix}/lib")\n'
             'EXTERN(ps5_agc_gate2_run)\nGROUP (\n' +
             "".join(f'  "{p}"\n' for p in libraries) + ')\n')
    (out / "vendor/libsdl3ps5.a").write_text(group)
    env["APP_STATIC_ARCHIVES"] = "vendor/libsdl3ps5.a"
    for key in ("APP_DEFINITIONS", "APP_INCLUDE_PATHS", "PACBREW_PACKAGES", "PACBREW_INCLUDE_PATHS",
                "PACBREW_STATIC_ARCHIVES", "APP_RUNTIME_MODULES"):
        env[key] = ""
    run("bash", script, "Folder", env=env, cwd=out)
    (out / "selected-test.txt").write_text(SELECTED + "\n")
    run("bash", ROOT / "tools/verify-native-test-app.sh", out)
    if (SDL2.verify_sdk(prefix) != sdk_identity
            or {name: digest(native / name) for name in NATIVE_ARTIFACTS} != native_identity):
        raise ValueError("Inputs changed during folder assembly")
    candidate = out / "dist" / param["titleId"]
    result = {"hardware_run": False, "sdl_commit": PIN["revision"],
              "template_commit": template_commit,
              "display_profile": profile, **sdk_identity,
              "native_artifacts": native_identity,
              "selected_test_sha256": digest(out / "selected-test.txt"),
              "template_libc_sha256": digest(out / "runtime/libc.prx"),
              "files": {str(p.relative_to(candidate)): digest(p)
                        for p in sorted(candidate.rglob("*")) if p.is_file()}}
    (out / "candidate.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"Unlaunched SDL3 folder: {candidate}; identities: {out / 'candidate.json'}")


if __name__ == "__main__":
    main()
