"""Check repository-owned C and C++ sources."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {".c", ".cc", ".cpp", ".cxx"}
HEADERS = {".h", ".hh", ".hpp", ".hxx"}


def owned(suffixes):
    names = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "apps", "src", "tests"],
        cwd=ROOT,
    )
    paths = {ROOT / os.fsdecode(name) for name in names.split(b"\0") if name}
    return sorted(path for path in paths if path.suffix in suffixes and path.is_file() and not path.is_symlink())


def tidy(build):
    cpu_count = getattr(os, "process_cpu_count", os.cpu_count)
    default_jobs = min(4, cpu_count() or 1)
    jobs_value = os.environ.get("TIDY_JOBS", str(default_jobs))
    try:
        jobs = int(jobs_value)
        if jobs < 1:
            raise ValueError
    except ValueError:
        raise ValueError("TIDY_JOBS must be a positive integer") from None

    entries = json.loads((build / "compile_commands.json").read_text())
    sources = set(owned(SOURCES))
    selected = []
    for entry in entries:
        directory = (build / entry["directory"]).resolve()
        path = (directory / entry["file"]).resolve()
        if path not in sources:
            continue
        flags = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
        # Match Clang's spelling for the GCC flag exported by the native Qt package.
        flags = ["-fno-direct-access-external-data" if flag == "-mno-direct-extern-access" else flag for flag in flags]
        selected.append({"directory": str(directory), "file": str(path), "arguments": flags})
    if not selected:
        raise ValueError(f"No owned translation units in {build / 'compile_commands.json'}")
    with TemporaryDirectory(prefix="screenshot-tidy-") as directory:
        (Path(directory) / "compile_commands.json").write_text(json.dumps(selected))
        command = [
            "clang-tidy", "--quiet", "-p", directory,
            f"--header-filter=^{re.escape(str(ROOT))}/(apps|src|tests)/",
        ]
        files = sorted({entry["file"] for entry in selected})
        workers = min(jobs, len(files))
        print(f"Analyzing {len(files)} translation units with {workers} clang-tidy jobs", flush=True)

        def analyze(path):
            return subprocess.run(
                [*command, path], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )

        failure = 0
        with ThreadPoolExecutor(max_workers=workers) as executor:
            pending = {executor.submit(analyze, path): path for path in files}
            for future in as_completed(pending):
                result = future.result()
                if result.stdout:
                    sys.stdout.buffer.write(result.stdout)
                    sys.stdout.flush()
                if result.returncode:
                    path = Path(pending[future]).relative_to(ROOT)
                    print(f"clang-tidy failed for {path} (status {result.returncode})", file=sys.stderr)
                    failure = failure or result.returncode
        if failure:
            raise subprocess.CalledProcessError(failure, command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["format", "format-check", "tidy"])
    parser.add_argument("--build", type=Path, default=Path("build/dev"))
    args = parser.parse_args()
    try:
        if args.command == "tidy":
            tidy((ROOT / args.build).resolve())
        else:
            files = owned(SOURCES | HEADERS)
            flags = ["-i"] if args.command == "format" else ["--dry-run", "--Werror"]
            if files:
                subprocess.run(["clang-format", *flags, *files], cwd=ROOT, check=True)
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
