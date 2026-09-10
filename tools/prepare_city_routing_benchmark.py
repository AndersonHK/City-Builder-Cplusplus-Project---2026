"""Create isolated old/current benchmark source trees and immutable input copies.

Does not change the checkout or write to game saves. Build each emitted project
with tools/msbuild.py; run the two executables sequentially, alternating order.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, help="Commit before routing changes")
    parser.add_argument("--save", required=True, type=Path)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("Build/RealCityRouting"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = (root / args.output).resolve()
    output.relative_to(root / "Build")
    if output.exists():
        raise SystemExit("Choose a fresh --output directory to preserve previous measurements")
    baseline = subprocess.check_output(["git", "rev-parse", "--verify", args.baseline + "^{commit}"], cwd=root, text=True).strip()
    archive = subprocess.check_output(["git", "archive", "--format=zip", baseline, "City Builder"], cwd=root)
    suffixes = {".cpp", ".h", ".hpp", ".inl"}
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        for name in files.namelist():
            path = Path(name)
            if len(path.parts) == 2 and path.suffix in suffixes:
                target = output / "old" / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(files.read(name))
    new_source = output / "new" / "City Builder"
    new_source.mkdir(parents=True)
    for source in (root / "City Builder").iterdir():
        if source.suffix in suffixes and source.is_file():
            shutil.copy2(source, new_source / source.name)
    project = (root / "City Builder/CityRoutingBenchmark.vcxproj").read_text()
    for version in ("old", "new"):
        source = output / version / "City Builder"
        for name in ("CityRoutingBenchmark.cpp", "PublicationFingerprint.h"):
            shutil.copy2(root / "City Builder" / name, source)
        configured = project
        if version == "old":
            configured = "\n".join(line for line in project.splitlines() if "TransportRouter." not in line)
            configured = configured.replace("CITY_ROUTING_NEW;", "")
            header = source / "SimulationRuntime.h"
            text = header.read_text()
            if "friend struct TransportCommuteTestAccess;" not in text:
                text = text.replace("class SimulationRuntime {", "class SimulationRuntime {\n    friend struct TransportCommuteTestAccess;")
            if "friend struct TransportCommuteTestAccess;" not in text:
                raise RuntimeError("Baseline runtime declaration was not found")
            header.write_text(text)
        (source / "CityRoutingBenchmark.vcxproj").write_text(configured)
    inputs = output / "inputs"
    (inputs / "saves").mkdir(parents=True)
    saved_copy = inputs / "saves" / args.save.name
    shutil.copy2(args.save, saved_copy)
    assets = inputs / "Data"
    shutil.copytree(args.assets, assets, ignore=shutil.ignore_patterns("Saves", "*.log"))
    # Some runtime configuration readers still resolve Data beside the executable.
    for version in ("old", "new"):
        shutil.copytree(assets, output / version / "Distributable/x64/Release/Data")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {
        "baseline": baseline,
        "save_source": str(args.save.resolve()),
        "save_sha256": digest(saved_copy),
        "assets": {str(path.relative_to(assets)): digest(path) for path in sorted(assets.rglob("*")) if path.is_file()},
        "new_sources": {path.name: digest(path) for path in sorted(new_source.iterdir()) if path.is_file()},
        "baseline_instrumentation": "Identical benchmark driver/project and one friend declaration; routing code unchanged",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(output)
    print("Save SHA256:", manifest["save_sha256"])
    print("Baseline:", baseline)


if __name__ == "__main__":
    main()
