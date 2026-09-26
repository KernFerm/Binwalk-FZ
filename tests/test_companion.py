#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host tests for the Linux/Raspberry Pi Binwalk bridge."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "companion" / "binwalk_fz_bridge.py"
SPEC = importlib.util.spec_from_file_location("binwalk_fz_bridge", MODULE_PATH)
assert SPEC and SPEC.loader
BRIDGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BRIDGE
SPEC.loader.exec_module(BRIDGE)


class FakeProcess:
    def wait(self, timeout=None):
        del timeout
        return 0

    def poll(self):
        return 0


class CompanionTests(unittest.TestCase):
    def test_files_are_real_sorted_regular_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs, outputs = root / "input", root / "output"
            inputs.mkdir()
            (inputs / "z.bin").write_bytes(b"z")
            (inputs / "A.bin").write_bytes(b"aa")
            (inputs / "folder").mkdir()
            bridge = BRIDGE.Bridge("/dev/serial0", 115200, inputs, outputs)
            self.assertEqual([path.name for path in bridge.files], ["A.bin", "z.bin"])
            self.assertEqual(bridge.selected().stat().st_size, 2)

    def test_genuine_json_fields_are_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs, outputs = root / "input", root / "output"
            inputs.mkdir()
            target = inputs / "firmware.bin"
            target.write_bytes(b"real input")
            output = root / "result.json"
            output.write_text(
                json.dumps(
                    [
                        {
                            "Analysis": {
                                "file_path": str(target),
                                "file_map": [
                                    {
                                        "offset": 507,
                                        "size": 1024,
                                        "name": "png",
                                        "confidence": 250,
                                        "description": "PNG image, 10 x 20",
                                    },
                                    {
                                        "offset": 4096,
                                        "size": 0,
                                        "name": "gzip",
                                        "confidence": 128,
                                        "description": "gzip compressed data",
                                    },
                                ],
                                "extractions": {},
                            }
                        }
                    ]
                ),
                encoding="utf-8",
            )
            bridge = BRIDGE.Bridge("/dev/serial0", 115200, inputs, outputs)
            bridge.binwalk = "/usr/local/bin/binwalk"
            with patch.object(BRIDGE.subprocess, "Popen", return_value=FakeProcess()):
                bridge.scan_worker(target, output)
            self.assertEqual(bridge.state, "DONE")
            self.assertEqual(bridge.detections, 2)
            self.assertEqual(bridge.first_offset, 507)
            self.assertEqual(bridge.result_name, "png")
            self.assertEqual(bridge.description, "PNG_image__10_x_20")

    def test_protocol_tokens_are_bounded_ascii(self):
        self.assertEqual(BRIDGE.token("PNG image!", 20), "PNG_image_")
        self.assertEqual(len(BRIDGE.token("x" * 100, 12)), 12)


if __name__ == "__main__":
    unittest.main()
