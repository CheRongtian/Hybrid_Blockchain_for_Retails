#!/usr/bin/env python3
"""Run the N-ary Merkle builder and stream its build events to a browser."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


VISUALIZER_ROOT = Path(__file__).resolve().parent
MODULE_ROOT = VISUALIZER_ROOT.parent


def json_bytes(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False).encode("utf-8")


class BuildSession:
    def __init__(
        self,
        binary: Path,
        arity: int,
        input_file: Path,
        output_dir: Path,
        proof_index: int,
    ) -> None:
        self.binary = binary
        self.arity = arity
        self.input_file = input_file
        self.output_dir = output_dir
        self.proof_index = proof_index
        self.events_path = output_dir / "tree-events.ndjson"
        self.result_path = output_dir / "tree-result.json"
        self.events: list[dict] = []
        self.condition = threading.Condition()
        self.done = False
        self.returncode: int | None = None
        self.stderr = ""

    def start(self) -> None:
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.events_path.write_text("", encoding="utf-8")
        self.result_path.unlink(missing_ok=True)
        threading.Thread(target=self._run, name="ntree-builder", daemon=True).start()

    def _append_event(self, event: dict) -> None:
        with self.condition:
            self.events.append(event)
            with self.events_path.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(event, ensure_ascii=False) + "\n")
            self.condition.notify_all()

    def _run(self) -> None:
        command = [
            str(self.binary),
            "--arity",
            str(self.arity),
            "--input",
            str(self.input_file),
            "--proof-index",
            str(self.proof_index),
            "--output",
            str(self.result_path),
            "--events",
        ]
        try:
            process = subprocess.Popen(
                command,
                cwd=MODULE_ROOT,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
            assert process.stdout is not None
            for line in process.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                    if isinstance(event, dict):
                        self._append_event(event)
                except json.JSONDecodeError:
                    self._append_event(
                        {
                            "type": "build_failed",
                            "message": "The C++ builder emitted invalid JSON.",
                            "arity": self.arity,
                            "level": 0,
                            "nodeId": None,
                            "nodeHash": "",
                            "rootHash": "",
                            "nodes": [],
                        }
                    )
            self.stderr = process.stderr.read() if process.stderr is not None else ""
            self.returncode = process.wait()
        except OSError as error:
            self.stderr = str(error)
            self.returncode = 2

        with self.condition:
            if not self.events:
                message = self.stderr.strip() or "The C++ builder exited without an event."
                self.events.append(
                    {
                        "type": "build_failed",
                        "message": message,
                        "arity": self.arity,
                        "level": 0,
                        "nodeId": None,
                        "nodeHash": "",
                        "rootHash": "",
                        "nodes": [],
                    }
                )
                with self.events_path.open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(self.events[-1], ensure_ascii=False) + "\n")
            self.done = True
            self.condition.notify_all()

    def status(self) -> dict:
        with self.condition:
            last_type = self.events[-1].get("type", "") if self.events else ""
            return {
                "done": self.done,
                "returncode": self.returncode,
                "eventCount": len(self.events),
                "lastEvent": last_type,
                "resultPath": str(self.result_path),
                "eventsPath": str(self.events_path),
                "stderr": self.stderr,
            }

    def result(self) -> dict:
        if not self.result_path.exists():
            return {"ready": False, **self.status()}
        try:
            value = json.loads(self.result_path.read_text(encoding="utf-8"))
            return {"ready": True, **value}
        except (OSError, json.JSONDecodeError) as error:
            return {"ready": False, "error": str(error), **self.status()}

    def stream(self, handler: BaseHTTPRequestHandler) -> None:
        handler.send_response(200)
        handler.send_header("Content-Type", "text/event-stream; charset=utf-8")
        handler.send_header("Cache-Control", "no-cache")
        handler.send_header("Connection", "keep-alive")
        handler.send_header("X-Content-Type-Options", "nosniff")
        handler.end_headers()
        handler.wfile.write(b": connected\n\n")
        handler.wfile.flush()

        cursor = 0
        try:
            while True:
                with self.condition:
                    if cursor >= len(self.events) and not self.done:
                        self.condition.wait(timeout=0.5)
                    pending = self.events[cursor:]
                    cursor = len(self.events)
                    finished = self.done and cursor >= len(self.events)

                for index, event in enumerate(pending, start=cursor - len(pending) + 1):
                    payload = json.dumps(event, ensure_ascii=False)
                    handler.wfile.write(f"id: {index}\ndata: {payload}\n\n".encode("utf-8"))
                    handler.wfile.flush()

                if finished:
                    while True:
                        time.sleep(15)
                        handler.wfile.write(b": keep-alive\n\n")
                        handler.wfile.flush()
                elif not pending:
                    handler.wfile.write(b": keep-alive\n\n")
                    handler.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            return


def make_handler(session: BuildSession):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: object) -> None:
            return

        def _json(self, status: int, value: object) -> None:
            body = json_bytes(value)
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/events":
                session.stream(self)
                return
            if parsed.path == "/api/status":
                self._json(200, session.status())
                return
            if parsed.path == "/api/result":
                self._json(200, session.result())
                return

            requested = unquote(parsed.path.lstrip("/")) or "index.html"
            candidate = (VISUALIZER_ROOT / requested).resolve()
            if not str(candidate).startswith(f"{VISUALIZER_ROOT.resolve()}/"):
                self._json(404, {"error": "Not found"})
                return
            if not candidate.is_file():
                self._json(404, {"error": "Not found"})
                return

            content_type = {
                ".html": "text/html; charset=utf-8",
                ".css": "text/css; charset=utf-8",
                ".js": "application/javascript; charset=utf-8",
            }.get(candidate.suffix, "application/octet-stream")
            body = candidate.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--arity", type=int, default=3)
    parser.add_argument(
        "--input",
        type=Path,
        default=MODULE_ROOT / "examples" / "leaves.txt",
    )
    parser.add_argument("--proof-index", type=int, default=0)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--output-dir", type=Path, default=MODULE_ROOT / "output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.arity < 2:
        print("--arity must be at least 2.", file=sys.stderr)
        return 2
    if args.proof_index < 0:
        print("--proof-index cannot be negative.", file=sys.stderr)
        return 2

    binary = args.binary.resolve()
    input_file = args.input.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    session = BuildSession(binary, args.arity, input_file, output_dir, args.proof_index)
    session.start()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(session))
    print(f"N-ary Merkle visualizer: http://127.0.0.1:{args.port}/", flush=True)
    print(f"Input: {input_file}", flush=True)
    print(f"Output: {output_dir}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        return 0
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
