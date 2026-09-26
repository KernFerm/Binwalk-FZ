#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""UART controller bridge for genuine upstream Binwalk on Linux."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import threading
from datetime import datetime, timezone
from pathlib import Path

import serial

BRIDGE_VERSION = "1.0.0"
PROTOCOL_VERSION = 1
MAX_LINE = 256
MAX_JSON = 32 * 1024 * 1024
DATA_ROOT = Path("/var/lib/binwalk-fz")
SERIAL_RE = re.compile(r"/dev/(serial[0-9]+|tty(?:AMA|USB|ACM|S)[0-9]+)\Z")


def token(value: object, limit: int) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_./:+-]", "_", str(value))
    return (cleaned or "-")[:limit]


class Bridge:
    def __init__(self, port: str, baud: int, input_dir: Path, output_dir: Path):
        self.port = port
        self.baud = baud
        self.input_dir = input_dir
        self.output_dir = output_dir
        self.binwalk = shutil.which("binwalk")
        self.binwalk_version = self._version()
        self.files: list[Path] = []
        self.index = 0
        self.state = "IDLE"
        self.detections = 0
        self.first_offset = 0
        self.first_size = 0
        self.confidence = 0
        self.result_name = "-"
        self.description = "-"
        self.process: subprocess.Popen[bytes] | None = None
        self.worker: threading.Thread | None = None
        self.lock = threading.Lock()
        self.cancel_requested = False
        self.refresh()

    def _version(self) -> str:
        if not self.binwalk:
            return "missing"
        try:
            result = subprocess.run(
                [self.binwalk, "--version"],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=5,
            )
        except (OSError, subprocess.TimeoutExpired):
            return "unknown"
        match = re.search(r"([0-9]+\.[0-9]+(?:\.[0-9]+)?)", result.stdout)
        return match.group(1) if match else "unknown"

    def refresh(self) -> None:
        self.input_dir.mkdir(parents=True, exist_ok=True)
        selected = self.files[self.index] if self.files and self.index < len(self.files) else None
        self.files = sorted(
            (
                entry
                for entry in self.input_dir.iterdir()
                if entry.is_file() and not entry.is_symlink()
            ),
            key=lambda path: path.name.casefold(),
        )[:10000]
        if selected in self.files:
            self.index = self.files.index(selected)
        elif self.files:
            self.index = min(self.index, len(self.files) - 1)
        else:
            self.index = 0

    def selected(self) -> Path | None:
        return self.files[self.index] if self.files else None

    def busy(self) -> bool:
        return self.worker is not None and self.worker.is_alive()

    @staticmethod
    def write(uart: serial.Serial, line: str) -> None:
        uart.write((line[: MAX_LINE - 2] + "\n").encode("ascii", "strict"))
        uart.flush()

    def info(self, uart: serial.Serial) -> None:
        with self.lock:
            self.refresh()
            selected = self.selected()
            count = len(self.files)
            index = self.index
            size = selected.stat().st_size if selected else 0
            name = token(selected.name, 63) if selected else "-"
        self.write(
            uart,
            f"BWF1 INFO {PROTOCOL_VERSION} {BRIDGE_VERSION} "
            f"{token(self.binwalk_version, 31)} {count} {index} {size} {name}",
        )

    def status(self, uart: serial.Serial) -> None:
        with self.lock:
            values = (
                self.state,
                self.detections,
                self.first_offset,
                self.first_size,
                self.confidence,
                self.result_name,
                self.description,
            )
        self.write(
            uart,
            "BWF1 STATUS "
            f"{token(values[0], 15)} {values[1]} {values[2]} {values[3]} {values[4]} "
            f"{token(values[5], 31)} {token(values[6], 79)} END",
        )

    def clear_result(self) -> None:
        self.detections = 0
        self.first_offset = 0
        self.first_size = 0
        self.confidence = 0
        self.result_name = "-"
        self.description = "-"

    def scan_worker(self, target: Path, json_path: Path) -> None:
        try:
            with self.lock:
                if self.cancel_requested:
                    self.state = "CANCELLED"
                    return
                self.process = subprocess.Popen(
                    [
                        self.binwalk or "binwalk",
                        "--quiet",
                        "--threads",
                        "1",
                        "--log",
                        str(json_path),
                        str(target),
                    ],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                process = self.process
            return_code = process.wait()
            with self.lock:
                cancelled = self.cancel_requested
                self.process = None
            if cancelled:
                with self.lock:
                    self.clear_result()
                    self.state = "CANCELLED"
                return
            if return_code != 0:
                with self.lock:
                    self.clear_result()
                    self.state = "ERROR"
                return
            if not json_path.is_file() or json_path.stat().st_size > MAX_JSON:
                with self.lock:
                    self.clear_result()
                    self.state = "RESULT_ERROR"
                return
            with json_path.open("r", encoding="utf-8") as source:
                payload = json.load(source)
            file_map: list[dict[str, object]] = []
            if isinstance(payload, list):
                for record in payload:
                    if isinstance(record, dict):
                        analysis = record.get("Analysis")
                        if isinstance(analysis, dict) and isinstance(analysis.get("file_map"), list):
                            file_map.extend(
                                item for item in analysis["file_map"] if isinstance(item, dict)
                            )
            with self.lock:
                if self.cancel_requested:
                    self.clear_result()
                    self.state = "CANCELLED"
                    return
                self.detections = len(file_map)
                if file_map:
                    first = min(file_map, key=lambda item: int(item.get("offset", 0)))
                    self.first_offset = max(0, int(first.get("offset", 0)))
                    self.first_size = max(0, int(first.get("size", 0)))
                    self.confidence = min(255, max(0, int(first.get("confidence", 0))))
                    self.result_name = token(first.get("name", "unknown"), 31)
                    self.description = token(first.get("description", "-"), 79)
                self.state = "DONE"
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            with self.lock:
                self.clear_result()
                self.process = None
                self.state = "ERROR"

    def start_scan(self, uart: serial.Serial) -> None:
        with self.lock:
            if self.busy():
                self.write(uart, "BWF1 ERROR ALREADY_RUNNING")
                return
            self.refresh()
            target = self.selected()
            if target is None:
                self.write(uart, "BWF1 ERROR NO_INPUT_FILES")
                return
            if not self.binwalk:
                self.write(uart, "BWF1 ERROR BINWALK_NOT_INSTALLED")
                return
            self.output_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
            json_path = self.output_dir / f"scan-{stamp}.json"
            self.cancel_requested = False
            self.clear_result()
            self.state = "SCANNING"
            self.worker = threading.Thread(
                target=self.scan_worker,
                args=(target, json_path),
                name="binwalk-scan",
                daemon=True,
            )
            worker = self.worker
        worker.start()
        self.status(uart)

    def stop_scan(self) -> None:
        with self.lock:
            self.cancel_requested = True
            process = self.process
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)

    def move(self, uart: serial.Serial, delta: int) -> None:
        with self.lock:
            if self.busy():
                self.write(uart, "BWF1 ERROR BUSY")
                return
            self.refresh()
            if self.files:
                self.index = (self.index + delta) % len(self.files)
            self.clear_result()
            self.state = "IDLE"
        self.info(uart)
        self.status(uart)

    def handle(self, uart: serial.Serial, text: str) -> None:
        fields = text.split()
        if fields == ["BWF1", "HELLO"]:
            self.info(uart)
            self.status(uart)
        elif fields == ["BWF1", "STATUS"]:
            self.status(uart)
        elif fields == ["BWF1", "SCAN"]:
            self.start_scan(uart)
        elif fields == ["BWF1", "NEXT"]:
            self.move(uart, 1)
        elif fields == ["BWF1", "PREV"]:
            self.move(uart, -1)
        elif fields == ["BWF1", "STOP"]:
            self.stop_scan()
            self.status(uart)
        else:
            self.write(uart, "BWF1 ERROR INVALID_COMMAND")

    def run(self) -> None:
        self.input_dir.mkdir(parents=True, exist_ok=True)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        try:
            with serial.Serial(self.port, self.baud, timeout=0.5, write_timeout=2) as uart:
                while True:
                    raw = uart.readline(MAX_LINE)
                    if not raw:
                        continue
                    if len(raw) >= MAX_LINE and not raw.endswith(b"\n"):
                        uart.reset_input_buffer()
                        self.write(uart, "BWF1 ERROR LINE_TOO_LONG")
                        continue
                    try:
                        text = raw.decode("ascii").strip()
                    except UnicodeDecodeError:
                        self.write(uart, "BWF1 ERROR NON_ASCII")
                        continue
                    self.handle(uart, text)
        finally:
            self.stop_scan()


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default="/dev/serial0", help="3.3 V UART device")
    parser.add_argument("--baud", type=int, choices=(115200, 230400, 460800), default=115200)
    parser.add_argument("--input-dir", type=Path, default=Path("/var/lib/binwalk-fz/input"))
    parser.add_argument("--output-dir", type=Path, default=Path("/var/lib/binwalk-fz/output"))
    args = parser.parse_args()
    if not SERIAL_RE.fullmatch(args.serial):
        parser.error("serial device must be a supported /dev UART")
    args.input_dir = args.input_dir.expanduser().resolve()
    args.output_dir = args.output_dir.expanduser().resolve()
    data_root = DATA_ROOT.resolve()
    if not args.input_dir.is_relative_to(data_root) or not args.output_dir.is_relative_to(data_root):
        parser.error("input and output directories must stay under /var/lib/binwalk-fz")
    if args.input_dir == args.output_dir:
        parser.error("input and output directories must differ")
    return args


def main() -> int:
    args = arguments()
    Bridge(args.serial, args.baud, args.input_dir, args.output_dir).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
