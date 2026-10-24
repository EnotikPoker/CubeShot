#!/usr/bin/env python3
"""Validate release identity, assemble checksums, and publish a complete GitHub Release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVES = ("CubeShot-windows-x64.zip", "CubeShot-linux-x64.tar.gz")


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def project_version():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(CubeShot VERSION (\d+\.\d+\.\d+)\b", text)[1]
    if json.loads((ROOT / "vcpkg.json").read_text())["version-semver"] != version:
        raise RuntimeError("CMake and vcpkg versions differ")
    return version


def validate_tag(tag):
    if tag != "v" + project_version():
        raise RuntimeError("Release tag must match the project version")
    if command("git", "rev-parse", tag + "^{commit}") != command("git", "rev-parse", "HEAD"):
        raise RuntimeError("Checkout does not match release tag")
    subprocess.check_call(["git", "fetch", "--no-tags", "origin",
                           "refs/heads/main:refs/remotes/origin/main"], cwd=ROOT)
    subprocess.check_call(["git", "merge-base", "--is-ancestor", "HEAD", "origin/main"], cwd=ROOT)


def payload_metadata(archive):
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as source:
            return json.loads(source.read("CubeShot/PACKAGE.json"))
    with tarfile.open(archive) as source:
        return json.load(source.extractfile("CubeShot/PACKAGE.json"))


def checksums(directory):
    version = project_version()
    lines = []
    for name in ARCHIVES:
        source = directory / name
        metadata = payload_metadata(source)
        expected_platform = "windows-x64" if name.endswith(".zip") else "linux-x64"
        if (metadata["version"] != version or not metadata["release_eligible"] or
                metadata["platform"] != expected_platform):
            raise RuntimeError(f"{name}: wrong version, platform or development build")
        lines.append(hashlib.sha256(source.read_bytes()).hexdigest() + "  " + name + "\n")
    (directory / "SHA256SUMS.txt").write_text("".join(lines), encoding="ascii")


def publish(directory, tag):
    validate_tag(tag)
    checksums(directory)
    existing = subprocess.run(["gh", "release", "view", tag, "--json", "isDraft"],
                              cwd=ROOT, text=True, capture_output=True)
    if existing.returncode == 0:
        if not json.loads(existing.stdout)["isDraft"]:
            raise RuntimeError("Release is already public; existing published assets will not be replaced")
    else:
        subprocess.check_call(["gh", "release", "create", tag, "--draft", "--verify-tag",
                               "--title", f"CubeShot {project_version()}", "--generate-notes"], cwd=ROOT)
    assets = [directory / name for name in (*ARCHIVES, "SHA256SUMS.txt")]
    subprocess.check_call(["gh", "release", "upload", tag, *map(str, assets), "--clobber"], cwd=ROOT)
    uploaded = json.loads(command("gh", "release", "view", tag, "--json", "assets"))["assets"]
    expected = {p.name: p.stat().st_size for p in assets}
    if {a["name"]: a["size"] for a in uploaded} != expected:
        raise RuntimeError("Draft assets are incomplete; release will stay unpublished")
    subprocess.check_call(["gh", "release", "edit", tag, "--draft=false", "--latest"], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    sub.add_parser("version")
    sub.add_parser("validate-tag").add_argument("tag")
    sub.add_parser("checksums").add_argument("directory", type=Path)
    publication = sub.add_parser("publish")
    publication.add_argument("directory", type=Path)
    publication.add_argument("tag")
    args = parser.parse_args()
    if args.operation == "version":
        print(project_version())
    elif args.operation == "validate-tag":
        validate_tag(args.tag)
    elif args.operation == "checksums":
        checksums(args.directory.resolve())
    else:
        publish(args.directory.resolve(), args.tag)


if __name__ == "__main__":
    main()
