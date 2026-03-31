#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[debug-protocol-smoke] FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    blaze = repo_root / "build" / "blaze"
    script = repo_root / "tests" / "debugger" / "debug_protocol_basic.blaze"

    if not blaze.exists():
        fail(f"missing binary: {blaze}")
    if not script.exists():
        fail(f"missing script: {script}")

    commands = [
        {"command": "stack"},
        {"command": "locals"},
        {"command": "next"},
        {"command": "continue"},
    ]
    input_payload = "".join(json.dumps(cmd) + "\n" for cmd in commands)

    proc = subprocess.run(
        [str(blaze), "--debug-protocol", str(script)],
        input=input_payload,
        text=True,
        capture_output=True,
        cwd=str(repo_root),
    )

    if proc.returncode != 0:
        fail(f"non-zero exit code: {proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        fail("no output produced")

    json_events = []
    final_output_lines = []
    for line in lines:
        if line.startswith("{"):
            try:
                json_events.append(json.loads(line))
            except json.JSONDecodeError as exc:
                fail(f"invalid JSON event line: {line}\nerror: {exc}")
        else:
            final_output_lines.append(line)

    if not json_events:
        fail("no json events parsed from output")

    event_names = [evt.get("event") for evt in json_events]
    required = {"debuggerEnabled", "stopped", "stack", "locals", "continued"}
    missing = sorted(name for name in required if name not in event_names)
    if missing:
        fail(f"missing expected events: {missing}\nseen: {event_names}")

    if not any(line == "5" for line in final_output_lines):
        fail(f"expected final program output '5'; got: {final_output_lines}")

    print("[debug-protocol-smoke] PASS")


if __name__ == "__main__":
    main()
