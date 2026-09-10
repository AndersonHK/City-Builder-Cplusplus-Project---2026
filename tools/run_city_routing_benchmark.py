"""Run isolated saved-city variants sequentially, alternating order between trials."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--variants", nargs="+", default=["old", "new"])
    parser.add_argument("--mode", choices=["full", "routing"], default="full")
    parser.add_argument("--x", default=1, type=int)
    parser.add_argument("--y", default=1, type=int)
    parser.add_argument("--warmup", default=100, type=int)
    parser.add_argument("--samples", default=300, type=int)
    parser.add_argument("--repeats", default=3, type=int)
    parser.add_argument("--main-cpu", type=int)
    parser.add_argument("--tag", default="comparison")
    parser.add_argument("--verify-publication", action="store_true")
    args = parser.parse_args()
    root = args.directory.resolve()
    if args.repeats < 1 or args.samples < 1 or args.warmup < 0:
        raise SystemExit("Invalid sample counts")
    for name in [args.tag, *args.variants]:
        if not name or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in name):
            raise SystemExit("Variant/tag must be a simple directory or filename component")
    for trial in range(1, args.repeats + 1):
        variants = args.variants if trial % 2 else list(reversed(args.variants))
        for version in variants:
            name = f"{args.tag}-{args.mode}-{version}-{trial}"
            log_path, csv_path = root / (name + ".log"), root / (name + ".csv")
            if log_path.exists() or csv_path.exists():
                raise SystemExit(f"Refusing to overwrite {name}")
            command = [str(root / version / "Distributable/x64/Release/CityRoutingBenchmark.exe"),
                       str(root / "inputs/saves"), str(root / "inputs/Data"), str(args.x), str(args.y),
                       args.mode, str(args.warmup), str(args.samples), str(csv_path)]
            if args.main_cpu is not None:
                command.extend(["--main-cpu", str(args.main_cpu)])
            if args.verify_publication:
                command.append("--verify-publication")
            print("START", name, flush=True)
            with log_path.open("w") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
            for line in log_path.read_text().splitlines():
                if line.startswith(("RESULT", "FAIL", "SNAPSHOT", "OUTCOME stage=measured_end")):
                    print(line, flush=True)
            if result.returncode:
                raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
