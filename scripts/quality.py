"""Check repository-owned C and C++ sources."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
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
        subprocess.run(
            ["clang-tidy", "--quiet", "-p", directory, f"--header-filter=^{re.escape(str(ROOT))}/(apps|src|tests)/",
             *sorted({entry["file"] for entry in selected})],
            cwd=ROOT, check=True,
        )


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
