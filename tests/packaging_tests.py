"""Release regressions: unsafe archives, incomplete payloads and premature publication."""
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_package
import package_game
import release_assets


class PackagingTests(unittest.TestCase):
    def test_windows_environment_accepts_systemroot_casing(self):
        for name in ("SYSTEMROOT", "SystemRoot"):
            with self.subTest(name=name), patch.dict(os.environ, {
                    name: "D:/Windows", "PATH": "build/vcpkg", "APPDATA": "normal-profile",
                    "CUBESHOT_DIRECTORY_URL": "https://old.example", "LD_LIBRARY_PATH": "build/lib"},
                    clear=True):
                profile = Path("isolated-profile")
                env = check_package.package_environment(profile, windows=True)
                self.assertEqual(env["PATH"], str(Path("D:/Windows") / "System32"))
                self.assertEqual(env["APPDATA"], str(profile))
                self.assertEqual(env["SYSTEMROOT"], "D:/Windows")
                self.assertNotIn("LD_LIBRARY_PATH", env)
                self.assertNotIn("CUBESHOT_DIRECTORY_URL", env)

    def test_reject_archive_traversal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "bad.zip"
            with zipfile.ZipFile(archive, "w") as source:
                source.writestr("CubeShot/../outside", "bad")
            with self.assertRaisesRegex(RuntimeError, "Unsafe archive"):
                check_package.extract(archive, root / "out")
            self.assertFalse((root / "out" / "outside").exists())

    def test_reject_archive_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "bad.tar.gz"
            with tarfile.open(archive, "w:gz") as source:
                link = tarfile.TarInfo("CubeShot/lib")
                link.type = tarfile.SYMTYPE
                link.linkname = "/tmp"
                source.addfile(link)
            with self.assertRaisesRegex(RuntimeError, "Unsupported archive"):
                check_package.extract(archive, root / "out")

    def test_reject_windows_drive_in_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "bad.zip"
            with zipfile.ZipFile(archive, "w") as source:
                source.writestr("CubeShot/C:/outside", "bad")
            with self.assertRaisesRegex(RuntimeError, "Unsafe archive"):
                check_package.extract(archive, root / "out")

    def test_detect_missing_or_damaged_library(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "licenses").mkdir()
            (root / "licenses" / "SDL3.txt").write_text("license")
            (root / "ИГРАТЬ.txt").write_text("instructions", encoding="utf-8")
            library = root / "vcruntime140.dll"
            library.write_bytes(b"runtime")
            hashes = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in root.rglob("*") if p.is_file()}
            (root / "FILES.sha256.json").write_text(json.dumps(hashes))
            check_package.verify_files(root)
            library.write_bytes(b"damaged")
            with self.assertRaisesRegex(RuntimeError, "missing or changed"):
                check_package.verify_files(root)
            library.unlink()
            with self.assertRaisesRegex(RuntimeError, "missing or changed"):
                check_package.verify_files(root)

    def test_linux_audit_checks_paths_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="CubeShot проверка ") as directory:
            root = Path(directory)
            package = root / "CubeShot"
            (package / "lib").mkdir(parents=True)
            outside = root / "outside libraries" / "libSDL3.so.0"
            output = f"\tlibSDL3.so.0 => {outside} (0x00000001)\n"
            with patch.object(package_game, "run", return_value=output):
                self.assertEqual(package_game.linux_dependencies(package / "CubeShot"),
                                 [("libSDL3.so.0", outside)])
            with patch.object(check_package.subprocess, "check_output", return_value=output):
                with self.assertRaisesRegex(RuntimeError, "loaded outside package"):
                    check_package.verify_linux(package, {})

    def test_linux_cpp_runtime_can_be_system_or_fallback_but_not_override_drivers(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "CubeShot"
            libraries = package / "lib"
            (libraries / "fallback").mkdir(parents=True)
            for path in [Path("/usr/lib/x86_64-linux-gnu/libstdc++.so.6"),
                         libraries / "fallback" / "libstdc++.so.6"]:
                output = f"\tlibstdc++.so.6 => {path} (0x00000001)\n"
                with patch.object(check_package.subprocess, "check_output", return_value=output):
                    check_package.verify_linux(package, {})
            output = f"\tlibstdc++.so.6 => {libraries / 'libstdc++.so.6'} (0x00000001)\n"
            with patch.object(check_package.subprocess, "check_output", return_value=output):
                with self.assertRaisesRegex(RuntimeError, "unexpected directory"):
                    check_package.verify_linux(package, {})

    def make_archives(self, root, eligible=True, version=None):
        version = version or release_assets.project_version()
        for name in release_assets.ARCHIVES:
            metadata = json.dumps({"version": version, "release_eligible": eligible,
                                   "platform": "windows-x64" if name.endswith(".zip") else "linux-x64"})
            if name.endswith(".zip"):
                with zipfile.ZipFile(root / name, "w") as source:
                    source.writestr("CubeShot/PACKAGE.json", metadata)
            else:
                with tarfile.open(root / name, "w:gz") as source:
                    payload = metadata.encode()
                    info = tarfile.TarInfo("CubeShot/PACKAGE.json")
                    info.size = len(payload)
                    source.addfile(info, io.BytesIO(payload))

    def test_no_checksums_for_incomplete_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_archives(root)
            (root / release_assets.ARCHIVES[1]).unlink()
            with self.assertRaises(FileNotFoundError):
                release_assets.checksums(root)
            self.assertFalse((root / "SHA256SUMS.txt").exists())

    def test_development_or_wrong_version_not_publishable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for eligible, version in [(False, None), (True, "0.0.0")]:
                self.make_archives(root, eligible, version)
                with self.assertRaisesRegex(RuntimeError, "development build"):
                    release_assets.checksums(root)

    def test_checksum_covers_both_platforms(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_archives(root)
            release_assets.checksums(root)
            lines = (root / "SHA256SUMS.txt").read_text().splitlines()
            self.assertEqual(len(lines), 2)
            for line, name in zip(lines, release_assets.ARCHIVES):
                self.assertEqual(line, hashlib.sha256((root / name).read_bytes()).hexdigest() + "  " + name)

    @patch.object(release_assets, "validate_tag")
    @patch.object(release_assets, "checksums")
    @patch.object(release_assets.subprocess, "check_call")
    @patch.object(release_assets.subprocess, "run")
    def test_public_release_is_never_replaced(self, run, mutate, checksums, validate):
        run.return_value.returncode = 0
        run.return_value.stdout = '{"isDraft":false}'
        with self.assertRaisesRegex(RuntimeError, "already public"):
            release_assets.publish(Path("unused"), "v0.11.0")
        mutate.assert_not_called()

    @patch.object(release_assets, "validate_tag")
    @patch.object(release_assets, "checksums")
    @patch.object(release_assets.subprocess, "check_call")
    @patch.object(release_assets.subprocess, "run")
    @patch.object(release_assets, "command")
    def test_incomplete_upload_stays_draft(self, command, run, mutate, checksums, validate):
        run.return_value.returncode = 0
        run.return_value.stdout = '{"isDraft":true}'
        command.return_value = '{"assets":[]}'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in (*release_assets.ARCHIVES, "SHA256SUMS.txt"):
                (root / name).write_bytes(b"asset")
            with self.assertRaisesRegex(RuntimeError, "stay unpublished"):
                release_assets.publish(root, "v0.11.0")
        self.assertTrue(any(c.args[0][2] == "upload" for c in mutate.call_args_list))
        self.assertFalse(any(c.args[0][2] == "edit" for c in mutate.call_args_list))


if __name__ == "__main__":
    unittest.main()
