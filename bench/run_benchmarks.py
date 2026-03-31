#!/usr/bin/env python3
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Case:
    name: str
    blaze_file: str
    python_file: str


REPEATS = 7
WARMUPS = 1


def run_cmd(cmd: list[str], cwd: Path) -> tuple[int, str]:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    out = proc.stdout.strip()
    if proc.returncode != 0:
        err = proc.stderr.strip()
        raise RuntimeError(f"Command failed: {' '.join(cmd)}\nstdout:\n{out}\nstderr:\n{err}")
    return proc.returncode, out


def measure(cmd: list[str], cwd: Path) -> tuple[float, str]:
    for _ in range(WARMUPS):
        run_cmd(cmd, cwd)

    samples_ms: list[float] = []
    output = ""
    for _ in range(REPEATS):
        start = time.perf_counter_ns()
        _, output = run_cmd(cmd, cwd)
        elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
        samples_ms.append(elapsed_ms)
    return statistics.median(samples_ms), output


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    bench = repo / "bench"
    blaze_bin = repo / "build" / "blaze"
    if not blaze_bin.exists():
        raise RuntimeError("Missing build/blaze. Build first with: cmake --build build")

    cases = [
        Case("arithmetic_loop", "arithmetic_loop.blaze", "arithmetic_loop.py"),
        Case("hashmap_set_workload", "hashmap_set_workload.blaze", "hashmap_set_workload.py"),
        Case("hashmap_string_and_class_keys", "hashmap_string_and_class_keys.blaze", "hashmap_string_and_class_keys.py"),
        Case("string_join_workload", "string_join_workload.blaze", "string_join_workload.py"),
    ]

    print(f"Running {len(cases)} cases (median of {REPEATS} runs, {WARMUPS} warmup)...")
    print("")

    for case in cases:
        blaze_cmd = [str(blaze_bin), str(bench / case.blaze_file)]
        py_cmd = ["python3", str(bench / case.python_file)]

        blaze_ms, blaze_out = measure(blaze_cmd, repo)
        py_ms, py_out = measure(py_cmd, repo)

        status = "OK" if blaze_out == py_out else "OUTPUT_MISMATCH"
        ratio = py_ms / blaze_ms if blaze_ms > 0 else float("inf")

        print(f"{case.name}:")
        print(f"  blaze : {blaze_ms:8.3f} ms")
        print(f"  python: {py_ms:8.3f} ms")
        print(f"  ratio : {ratio:8.3f}x (python/blaze)")
        print(f"  check : {status}")
        if status != "OK":
            print(f"  blaze_out : {blaze_out}")
            print(f"  python_out: {py_out}")
        print("")


if __name__ == "__main__":
    main()
