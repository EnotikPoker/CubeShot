#!/usr/bin/env python3
"""Build a relocatable player archive from CMake's portable metadata."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from linux_runtime import CXX_RUNTIMES, search_path


ROOT = Path(__file__).resolve().parents[1]
# These belong to the OS/driver stack, never to the game distribution.
BASE_LINUX = re.compile(
    r"^(linux-vdso|ld-linux|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so|"
    r"lib(GL|GLX|EGL|OpenGL|GLES|vulkan|drm|gbm))"
)
SDL_RUNTIME = (
    "libX11.so.6", "libXext.so.6", "libXrandr.so.2", "libXcursor.so.1",
    "libXi.so.6", "libXfixes.so.3", "libXrender.so.1", "libXss.so.1",
    "libwayland-client.so.0", "libwayland-cursor.so.0", "libwayland-egl.so.1",
    "libxkbcommon.so.0", "libdecor-0.so.0", "libdbus-1.so.3",
    "libibus-1.0.so.5", "libudev.so.1",
)
REQUIRED_SDL_RUNTIME = {
    "libX11.so.6", "libXext.so.6", "libXcursor.so.1", "libXrandr.so.2",
    "libwayland-client.so.0", "libwayland-cursor.so.0", "libxkbcommon.so.0",
}


def run(args):
    return subprocess.check_output([str(a) for a in args], text=True, errors="replace")


def linux_dependencies(binary):
    output = run(["ldd", binary])
    if "not found" in output:
        raise RuntimeError(f"Unresolved dependencies of {binary}:\n{output}")
    result = []
    for line in output.splitlines():
        match = re.match(r"\s*(\S+) => (/.+?) \(", line)
        if match and not BASE_LINUX.match(match[1]):
            result.append((match[1], Path(match[2])))
    return result


def glibc_versions(binary):
    return [tuple(map(int, value.split("."))) for value in
            re.findall(r"\bGLIBC_(\d+\.\d+(?:\.\d+)?)\b", run(["objdump", "-T", binary]))]


def linux_libraries(metadata, package, allow_newer):
    if not shutil.which("patchelf"):
        raise RuntimeError("Packaging requires patchelf on the build machine")
    cache = run(["/sbin/ldconfig", "-p"])
    pending = []
    for name in SDL_RUNTIME:
        match = re.search(r"^\s*" + re.escape(name) + r" \(.*x86-64.*\) => (\S+)", cache, re.M)
        if match:
            pending.append((name, Path(match[1])))
        elif name in REQUIRED_SDL_RUNTIME:
            raise RuntimeError(f"Missing SDL backend runtime on packaging host: {name}")
    for key in ("client", "server"):
        pending.extend(linux_dependencies(Path(metadata[key])))
    libraries = package / "lib"
    libraries.mkdir()
    fallback = libraries / "fallback"
    fallback.mkdir()
    copied = {}
    while pending:
        name, source = pending.pop()
        if name in copied:
            if copied[name].resolve() != source.resolve():
                raise RuntimeError(f"Conflicting libraries named {name}")
            continue
        copied[name] = source
        pending.extend(linux_dependencies(source))
        destination = fallback if name in CXX_RUNTIMES else libraries
        shutil.copy2(source, destination / name, follow_symlinks=True)
    for binary in [package / "CubeShot", package / "cubeshot_server",
                   *(p for p in libraries.rglob("*") if p.is_file())]:
        versions = glibc_versions(binary)
        if versions and max(versions) > (2, 35) and not allow_newer:
            raise RuntimeError(f"{binary.name} requires GLIBC_{'.'.join(map(str, max(versions)))}; "
                               "release packages must be built on Ubuntu 22.04")
        if binary.parent == fallback:
            rpath = search_path("$ORIGIN/..", "$ORIGIN")
        elif binary.parent == libraries:
            rpath = search_path("$ORIGIN", "$ORIGIN/fallback")
        else:
            rpath = search_path("$ORIGIN/lib", "$ORIGIN/lib/fallback")
        subprocess.check_call(["patchelf", "--set-rpath", rpath, str(binary)])
    return list(copied.values())


def windows_libraries(metadata, package):
    runtime = [Path(p) for p in metadata["msvc_runtime"].split(";") if p]
    if not runtime or not any(p.name.lower().startswith("vcruntime") for p in runtime):
        raise RuntimeError("CMake did not locate the redistributable MSVC runtime")
    dlls = [Path(p) for p in metadata["dlls"].split(";") if p]
    # vcpkg app-local deployment also finds imported UNKNOWN targets (notably
    # FindOpenSSL), which TARGET_RUNTIME_DLLS cannot enumerate.
    dlls.extend(Path(metadata["client"]).parent.glob("*.dll"))
    for source in set(runtime + dlls):
        shutil.copy2(source, package / source.name)
    # Recursively verify imports with the MSVC tool from the configured compiler.
    dumpbin = Path(metadata["linker"]).with_name("dumpbin.exe")
    if not dumpbin.is_file():
        raise RuntimeError("dumpbin.exe not found next to the configured MSVC linker")
    for binary in [*package.glob("*.exe"), *package.glob("*.dll")]:
        for name in re.findall(r"^\s+(\S+\.dll)\s*$", run([dumpbin, "/dependents", binary]), re.I | re.M):
            if (package / name).is_file():
                continue
            if name.lower().startswith(("msvcp", "vcruntime", "concrt")):
                raise RuntimeError(f"Missing app-local runtime: {name}")
            if not name.lower().startswith(("api-ms-", "ext-ms-")) and not (
                    Path(os.environ["SystemRoot"]) / "System32" / name).is_file():
                raise RuntimeError(f"Missing runtime dependency of {binary.name}: {name}")
    return []


def copy_licenses(metadata, package, system_libraries):
    destination = package / "licenses"
    shutil.copytree(ROOT / "packaging" / "licenses", destination)
    shutil.copy2(Path(metadata["gns_source"]) / "LICENSE", destination / "GameNetworkingSockets.txt")
    installed = Path(metadata["vcpkg_installed"])
    for source in sorted((installed / "share").glob("*/copyright")):
        shutil.copy2(source, destination / ("vcpkg-" + source.parent.name + ".txt"))
    # Native and distro-provided backend libraries retain their distribution notices,
    # including GCC's runtime exception and accompanying license texts.
    if os.name != "nt":
        for source in system_libraries:
            try:
                owner = run(["dpkg-query", "-S", source.resolve()]).split(": /", 1)[0]
            except subprocess.CalledProcessError:
                continue  # /usr/local SDL is covered by the included SDL notice.
            for name in owner.split(", "):
                name = name.split(":", 1)[0]
                copyright_file = Path("/usr/share/doc") / name / "copyright"
                if copyright_file.is_file():
                    shutil.copy2(copyright_file, destination / ("system-" + name + ".txt"))
        common = Path("/usr/share/common-licenses")
        if common.is_dir():
            shutil.copytree(common, destination / "common-licenses")
    (package / "THIRD_PARTY.txt").write_text(
        "CubeShot includes third-party software. Copyright notices, licenses and GCC runtime\n"
        "exceptions are in licenses/. Dependency versions are recorded in the project's\n"
        "vcpkg.json and pinned CMake sources: https://github.com/EnotikPoker/CubeShot\n",
        encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-newer-glibc", action="store_true",
                        help="local development only; output is not compatible with the release baseline")
    args = parser.parse_args()
    metadata = json.loads((args.build / f"portable-{args.config}.json").read_text(encoding="utf-8"))
    if args.config != "Release" or args.allow_newer_glibc:
        print("DEVELOPMENT PACKAGE: not eligible for publication")
    output = (args.output or args.build / "packages").resolve()
    output.mkdir(parents=True, exist_ok=True)
    windows = os.name == "nt"
    with tempfile.TemporaryDirectory(prefix="cubeshot-package-") as staging:
        package = Path(staging) / "CubeShot"
        package.mkdir()
        client = "CubeShot.exe" if windows else "CubeShot"
        server = "cubeshot_server.exe" if windows else "cubeshot_server"
        shutil.copy2(metadata["client"], package / client)
        shutil.copy2(metadata["server"], package / server)
        system_libraries = (windows_libraries(metadata, package) if windows else
                            linux_libraries(metadata, package, args.allow_newer_glibc))
        copy_licenses(metadata, package, system_libraries)
        instructions = (ROOT / "packaging" / "PLAY.txt").read_text(encoding="utf-8")
        instructions = instructions.replace("@VERSION@", metadata["version"]).replace("@CLIENT@", client)
        (package / "ИГРАТЬ.txt").write_text(instructions, encoding="utf-8")
        (package / "VERSION.txt").write_text(metadata["version"] + "\n", encoding="utf-8")
        (package / "PACKAGE.json").write_text(json.dumps({
            "version": metadata["version"], "platform": "windows-x64" if windows else "linux-x64",
            "release_eligible": args.config == "Release" and not args.allow_newer_glibc,
        }, indent=2), encoding="utf-8")
        # Hash every payload file so extraction and accidental missing DLLs are checked.
        hashes = {p.relative_to(package).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(package.rglob("*")) if p.is_file()}
        (package / "FILES.sha256.json").write_text(json.dumps(hashes, ensure_ascii=False, indent=2),
                                                  encoding="utf-8")
        archive = output / ("CubeShot-windows-x64.zip" if windows else "CubeShot-linux-x64.tar.gz")
        if windows:
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
                for source in sorted(package.rglob("*")):
                    if source.is_file():
                        zipped.write(source, source.relative_to(package.parent))
        else:
            with tarfile.open(archive, "w:gz") as tar:
                tar.add(package, arcname="CubeShot")
        print(f"Created {archive} (CubeShot {metadata['version']})")


if __name__ == "__main__":
    main()
