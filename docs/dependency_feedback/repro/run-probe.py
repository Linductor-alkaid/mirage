"""Build the offline probe against an already built native-debug dependency graph."""
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
build = root / "build/native-debug"
line = subprocess.check_output(
    ["ninja", "-C", str(build), "-t", "commands", "tests/mira_host_test"], text=True
).splitlines()[-1]
parts = shlex.split(line)
start = next(i for i, p in enumerate(parts) if pathlib.Path(p).name in ("c++", "g++", "clang++"))
args = parts[start:]
args = args[: args.index("&&")]
with tempfile.TemporaryDirectory(prefix="mirage-feedback-") as temporary:
    executable = str(pathlib.Path(temporary) / "mira-contract-probe")
    source = str(pathlib.Path(__file__).with_name("mira-contract-probe.cpp"))
    obj = next(i for i, p in enumerate(args) if p.endswith("mira_host_test.cpp.o"))
    args[obj] = source
    args[args.index("-o") + 1] = executable
    args[1:1] = [
        "-std=c++20",
        "-I" + str(root / "third_party/mira/include"),
        "-I" + str(root / "third_party/mira/third_party/executor/include"),
    ]
    subprocess.run(args, cwd=build, check=True)
    subprocess.run([executable], check=True)
