"""Run MSBuild with case-insensitive environment keys normalized on Windows."""
import os
import shutil
import subprocess
import sys

build_env = {key.upper(): value for key, value in os.environ.items()}
msbuild = shutil.which("msbuild", path=build_env.get("PATH"))
if not msbuild:
    raise SystemExit("MSBuild was not found. Run from a Visual Studio developer shell.")
raise SystemExit(subprocess.call([msbuild, *sys.argv[1:]], env=build_env))
