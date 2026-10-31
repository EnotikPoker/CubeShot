#!/usr/bin/env python3
"""Verify a downloaded player archive in an isolated profile and working directory."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from linux_runtime import CXX_RUNTIMES, SYSTEM_LIBRARY_DIRS


def extract(archive, destination):
    def target(name):
        parts = PurePosixPath(name).parts
        if (not parts or parts[0] != "CubeShot" or ".." in parts or "\\" in name or
                any(":" in part for part in parts)):
            raise RuntimeError(f"Unsafe archive entry: {name}")
        return destination.joinpath(*parts)

    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as source:
            for item in source.infolist():
                path = target(item.filename)
                if item.is_dir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(source.read(item))
    else:
        with tarfile.open(archive) as source:
            for item in source:
                path = target(item.name)
                if item.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                elif item.isfile():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(item) as content, path.open("wb") as output:
                        shutil.copyfileobj(content, output)
                    path.chmod(item.mode & 0o777)
                else:
                    raise RuntimeError(f"Unsupported archive entry: {item.name}")


def verify_files(package):
    expected = json.loads((package / "FILES.sha256.json").read_text(encoding="utf-8"))
    actual = {p.relative_to(package).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in package.rglob("*") if p.is_file() and p.name != "FILES.sha256.json"}
    if actual != expected:
        raise RuntimeError("Archive payload differs from FILES.sha256.json (missing or changed file)")
    if not (package / "licenses" / "SDL3.txt").is_file() or not (package / "ИГРАТЬ.txt").is_file():
        raise RuntimeError("Missing player instructions or third-party licenses")


def verify_linux(package, env):
    # glibc and GPU drivers belong to the target OS; everything else must be bundled.
    base = re.compile(r"^(lib(c|m|dl|pthread|rt|resolv|util|anl)\.so|ld-linux|linux-vdso|"
                      r"lib(GL|GLX|EGL|OpenGL|GLES|vulkan|drm|gbm))")
    libraries = (package / "lib").resolve()
    if not libraries.is_dir():
        raise RuntimeError("Missing bundled Linux libraries")
    system_dirs = {Path(directory).resolve() for directory in SYSTEM_LIBRARY_DIRS}
    for binary in [package / "CubeShot", package / "cubeshot_server",
                   *(p for p in libraries.rglob("*") if p.is_file())]:
        output = subprocess.check_output(["ldd", str(binary)], env=env, text=True)
        if "not found" in output:
            raise RuntimeError(output)
        for line in output.splitlines():
            match = re.match(r"\s*(\S+) => (/.+?) \(", line)
            if not match or base.match(match[1]):
                continue
            parent = Path(match[2]).resolve().parent
            if match[1] in CXX_RUNTIMES:
                if parent not in system_dirs and parent != libraries / "fallback":
                    raise RuntimeError(f"C++ runtime loaded from unexpected directory: {line.strip()}")
            elif parent != libraries:
                raise RuntimeError(f"{binary.name} loaded outside package: {line.strip()}")


def verify_windows(package):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32]
    kernel.GetModuleFileNameW.restype = ctypes.c_uint32
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    kernel.FreeLibrary.restype = ctypes.c_int
    for library in package.glob("*.dll"):
        loaded = ctypes.WinDLL(str(library), winmode=0x100 | 0x800)
        try:
            if library.name.lower().startswith(("vcruntime", "msvcp", "concrt")):
                path = ctypes.create_unicode_buffer(32768)
                if not kernel.GetModuleFileNameW(loaded._handle, path, len(path)):
                    raise ctypes.WinError(ctypes.get_last_error())
                # GetModuleFileNameW may return an 8.3 alias such as RUNNER~1.
                # Compare directory identity, not two spellings of the same path.
                if not Path(path.value).parent.samefile(package):
                    raise RuntimeError(f"Runtime loaded outside package: {path.value}")
        finally:
            # ctypes does not unload DLLs on GC; release our reference so Windows
            # can remove the extracted temporary package after the smoke test.
            if not kernel.FreeLibrary(loaded._handle):
                raise ctypes.WinError(ctypes.get_last_error())


def package_environment(profile, windows):
    # os.environ is case-insensitive on Windows; dict(os.environ) is not.
    # Python normalizes Windows keys to uppercase (including SYSTEMROOT).
    env = {key.upper() if windows else key: value for key, value in os.environ.items()}
    for key in ("LD_LIBRARY_PATH", "LD_PRELOAD", "CUBESHOT_DIRECTORY_URL", "SSL_CERT_FILE"):
        env.pop(key, None)
    for key in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "APPDATA", "LOCALAPPDATA"):
        env[key] = str(profile)
    if windows:
        if not env.get("SYSTEMROOT"):
            raise RuntimeError("Windows environment is missing SYSTEMROOT")
        env["PATH"] = str(Path(env["SYSTEMROOT"]) / "System32")
    else:
        env["PATH"] = "/usr/bin:/bin"
    return env


def verify_visual_assets(package):
    for name in ("effects.png", "materials.png", "font.png"):
        path = package / "assets" / name
        if not path.is_file() or path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            raise RuntimeError(f"Missing or invalid visual asset: assets/{name}")
    if not (package / "assets" / "fonts" / "OFL.txt").is_file():
        raise RuntimeError("Missing font license: assets/fonts/OFL.txt")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--require-release", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="CubeShot проверка ") as directory:
        root = Path(directory)
        extract(args.archive.resolve(), root)
        package = root / "CubeShot"
        verify_files(package)
        verify_visual_assets(package)
        metadata = json.loads((package / "PACKAGE.json").read_text(encoding="utf-8"))
        if metadata["version"] != args.version:
            raise RuntimeError("Package version does not match requested release")
        if args.require_release and not metadata["release_eligible"]:
            raise RuntimeError("Development package cannot be published")
        windows = os.name == "nt"
        if metadata["platform"] != ("windows-x64" if windows else "linux-x64"):
            raise RuntimeError("Wrong package platform")
        profile = root / "isolated profile"
        profile.mkdir()
        # Preserve the graphical session, but never search build/vcpkg directories.
        env = package_environment(profile, windows)
        if windows:
            verify_windows(package)
        else:
            verify_linux(package, env)
        executable = package / ("CubeShot.exe" if windows else "CubeShot")
        server = package / ("cubeshot_server.exe" if windows else "cubeshot_server")
        for binary in (executable, server):
            result = subprocess.run([str(binary), "--version"], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=10, check=True)
            if result.stdout.strip() != f"CubeShot {args.version}":
                raise RuntimeError(f"Wrong executable version: {result.stdout!r}")
        result = subprocess.run([str(executable), "--smoke-test"], cwd=root, env=env,
                                capture_output=True, text=True, timeout=20)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode:
            raise RuntimeError(f"Packaged game smoke test exited with {result.returncode}")
        print(f"Verified {args.archive.name}: menu, rendering, bot match, packaged LAN server and owner connection")


if __name__ == "__main__":
    main()
