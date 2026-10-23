"""Shared Linux runtime policy for packaging and archive verification."""

# The system C++ runtime must win when installed: Mesa/LLVM can require a newer
# ABI than the game built on Ubuntu 22.04. Keep our copies as an offline fallback.
CXX_RUNTIMES = {"libstdc++.so.6", "libgcc_s.so.1"}
SYSTEM_LIBRARY_DIRS = (
    "/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu",
    "/lib64", "/usr/lib64", "/lib", "/usr/lib",
)


def search_path(library_origin, fallback_origin):
    return ":".join((library_origin, *SYSTEM_LIBRARY_DIRS, fallback_origin))
