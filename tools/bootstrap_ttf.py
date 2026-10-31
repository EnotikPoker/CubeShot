#!/usr/bin/env python3
"""Build pinned SDL3_ttf into build/_prefix; does not install system packages."""
import hashlib
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VERSION = "3.2.2"
SHA512 = ("b9adc28d584759b1cc1072d071caad95ade263a1fb24e294d66fc15e132d44bc62925875"
          "cb1f1b596089def9b47d7b73f42ffa4e120ee51982f993dc7a7d3bd7")


def main():
    root = ROOT / "build" / "dependencies"
    root.mkdir(parents=True, exist_ok=True)
    archive = root / f"SDL_ttf-{VERSION}.tar.gz"
    if not archive.exists():
        url = f"https://codeload.github.com/libsdl-org/SDL_ttf/tar.gz/refs/tags/release-{VERSION}"
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha512(data).hexdigest() != SHA512:
            raise RuntimeError("SDL3_ttf archive checksum mismatch")
        archive.write_bytes(data)
    if hashlib.sha512(archive.read_bytes()).hexdigest() != SHA512:
        raise RuntimeError("Cached SDL3_ttf archive checksum mismatch")
    source = root / f"SDL_ttf-release-{VERSION}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            # The source archive is pinned and verified above. Reject path escapes too.
            for member in tar.getmembers():
                resolved = (root / member.name).resolve()
                if not resolved.is_relative_to(root.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError("Unsafe source archive member")
            tar.extractall(root)
    build = root / "ttf-build"
    subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={ROOT / 'build' / '_prefix'}",
                    "-DSDLTTF_HARFBUZZ=OFF", "-DSDLTTF_PLUTOSVG=OFF", "-DSDLTTF_SAMPLES=OFF",
                    "-DSDLTTF_VENDORED=OFF", "-DBUILD_SHARED_LIBS=ON"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "4"], check=True)
    subprocess.run(["cmake", "--install", str(build)], check=True)


if __name__ == "__main__":
    main()
