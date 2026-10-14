#!/usr/bin/env python3
"""Optional local protoc/runtime bootstrap; writes only inside the build directory."""
import argparse
import pathlib
import subprocess

PIN = "f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c"  # protobuf v21.12 (C++ 3.21.12)


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / "build")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    root = args.build_root.resolve()
    source, build, prefix = root / "_sources/protobuf", root / "_protobuf", root / "_prefix"
    source.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        run("git", "clone", "--depth", "1", "--branch", "v21.12", "https://github.com/protocolbuffers/protobuf.git", source)
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise SystemExit(f"Unexpected protobuf revision in {source}: {revision}; expected {PIN}")
    run("cmake", "-S", source, "-B", build, "-Dprotobuf_BUILD_TESTS=OFF", "-Dprotobuf_WITH_ZLIB=OFF",
        "-Dprotobuf_BUILD_SHARED_LIBS=OFF", "-Dprotobuf_MSVC_STATIC_RUNTIME=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix}")
    run("cmake", "--build", build, "--config", "Release", "--parallel", max(1, args.jobs))
    run("cmake", "--install", build, "--config", "Release")
    print(f"Configure CubeShot with -DCMAKE_PREFIX_PATH={prefix}")


if __name__ == "__main__":
    main()
