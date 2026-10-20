# PlatformIO pre-build script: MESHPUTER_VERSION = `git describe`, e.g. v0.1.0-3-gabc1234.
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)

try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--always", "--dirty"],
        cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL, text=True).strip()  # noqa: F821
except Exception:
    version = "dev"

env.Append(CPPDEFINES=[("MESHPUTER_VERSION", env.StringifyMacro(version))])  # noqa: F821
