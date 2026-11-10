#!/usr/bin/env python3
"""Build and run host-side tests of the platform-independent firmware core.

Compiler: $CXX, else g++/clang++ on PATH, else `python -m ziglang c++`
(pip install ziglang) - the last option works on Windows without MinGW.

    python tools/run_host_tests.py [name-filter]
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "HFMConnect"
TESTS = ROOT / "tests" / "host"
BUILD = ROOT / "build" / "host"

# Platform-independent modules only (no Arduino.h).
CORE = ["HfmSession.cpp", "TraceLog.cpp", "HfmDecode.cpp", "HfmIdent.cpp", "HfmDtc.cpp",
        "HfmProfiles.cpp", "HfmLive.cpp", "HfmServices.cpp", "ServiceGuard.cpp", "ImpulseDecoder.cpp", "KLineWake.cpp"]


def compiler():
    if os.environ.get("CXX"):
        return os.environ["CXX"].split()
    for c in ("g++", "clang++"):
        if shutil.which(c):
            return [c]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "c++"]
    except ImportError:
        sys.exit("no C++ compiler: install g++/clang++ or `pip install ziglang`")


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("host_tests.exe" if os.name == "nt" else "host_tests")
    srcs = [str(FW / f) for f in CORE if (FW / f).exists()] + [str(p) for p in sorted(TESTS.glob("*.cpp"))]
    cmd = compiler() + ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-Wno-missing-field-initializers", "-DHFM_HOST_TEST=1",
                        "-I", str(FW), "-I", str(TESTS), "-o", str(exe)] + srcs
    print("compiling", len(srcs), "files ...")
    r = subprocess.run(cmd)
    if r.returncode:
        return r.returncode
    return subprocess.run([str(exe)] + sys.argv[1:], cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
