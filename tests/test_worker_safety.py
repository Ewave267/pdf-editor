#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native kernel policy, hostile scripts, protocol limits and corpus regression."""
import hashlib
import base64
import json
import os
import resource
import random
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_probe_fixtures import generate, many_pages_pdf, safety_fixtures
from run_xfa_probe import sandbox_command

WORKER = Path(sys.argv.pop(1)).resolve()
POLICY_WORKER = Path(sys.argv.pop(1)).resolve()
PDFIUM = Path(sys.argv.pop(1)).resolve()


class WorkerSafetyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="pdf-safety-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def command(self, worker, fixture):
        command = sandbox_command(worker, PDFIUM / "lib/libpdfium.so", fixture, self.directory)
        index = command.index("--bind")  # Renderer has no writable host output mount.
        del command[index:index + 3]
        command[command.index("--chdir") + 1] = "/tmp"
        return command + ["/probe"]

    def run_worker(self, fixture, commands=(), environment=None):
        result = subprocess.run(self.command(WORKER, fixture),
                                input="".join(json.dumps(command) + "\n" for command in commands),
                                text=True, capture_output=True, timeout=10, env=environment)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return [json.loads(line) for line in result.stdout.splitlines()]

    def test_merge_rejects_unprepared_form_appearances(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        source = (ROOT / "tests/pdfs/acroform/phase3.pdf").read_bytes()
        options = {"pages": [0], "rotation": 0, "flatten": True, "mergeLengths": [len(source)]}
        payload = b"PDFEDITOR-EXPORT\n" + json.dumps(options).encode() + b"\n" + source
        result = subprocess.run(self.command(WORKER, fixture) + ["--save"], input=payload,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Flatten AcroForm", result.stderr)

    def test_high_resolution_transport_is_compressed(self):
        responses = self.run_worker(ROOT / "tests/pdfs/normal/single-page.pdf",
                                    [{"id": 1, "page": 0, "width": 2400}])
        self.assertLess(len(base64.b64decode(responses[1]["png"])), 1024 * 1024)

    def test_upstream_compatibility_corpus(self):
        for name, expected in (("arabic.pdf", 0), ("simple_xfa.pdf", 2),
                               ("static_password_field_rotate.pdf", 3)):
            fixture = ROOT / "tests/pdfs/upstream-pdfium" / name
            responses = self.run_worker(fixture, [{"id": 1, "page": 0, "width": 300}])
            self.assertEqual(responses[0]["formType"], expected, name)
            self.assertIn("png", responses[1], name)
        fixture = ROOT / "tests/pdfs/upstream-pdfium/encrypted_hello_world_r6.pdf"
        result = subprocess.run(self.command(WORKER, fixture), input=b"", capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"password", result.stderr)

    def test_canadian_government_corpus(self):
        directory = ROOT / "tests/pdfs/government-canada"
        manifest = json.loads((directory / "SOURCE.json").read_text())
        for name, record in manifest["files"].items():
            with self.subTest(fixture=name):
                fixture = directory / name
                before = fixture.read_bytes()
                self.assertEqual(hashlib.sha256(before).hexdigest(), record["sha256"])
                expected = record["expected"]
                responses = self.run_worker(fixture, [
                    {"id": page + 1, "page": page, "width": 300}
                    for page in range(expected["pages"])
                ])
                document = responses[0]
                self.assertEqual(document["formType"], 1)
                self.assertEqual(len(document["pages"]), expected["pages"])
                self.assertTrue(document["fieldsComplete"])
                self.assertEqual(len(document["fields"]), expected["widgets"])
                for field_name, value in expected["sample_values"].items():
                    self.assertEqual({field["value"] for field in document["fields"]
                                      if field["name"] == field_name}, {value})
                self.assertEqual(len(responses), expected["pages"] + 1)
                for response in responses[1:]:
                    self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertEqual(fixture.read_bytes(), before)

    def test_canadian_government_round_trips(self):
        directory = ROOT / "tests/pdfs/government-canada"
        manifest = json.loads((directory / "SOURCE.json").read_text())
        for name, record in manifest["files"].items():
            original = directory / name
            source = original.read_bytes()
            fixture = original
            baseline = self.run_worker(original)[0]
            for generation, value in enumerate(record["expected"]["round_trip"]["values"], 1):
                with self.subTest(fixture=name, generation=generation):
                    field_name = record["expected"]["round_trip"]["field"]
                    field = next(field for field in baseline["fields"] if field["name"] == field_name)
                    responses = self.run_worker(fixture, [
                        {"id": 1, "op": "event", "action": "setFieldText",
                         "page": field["page"], "key": field["id"], "text": value},
                        {"id": 2, "op": "snapshot"},
                    ])
                    self.assertNotIn("error", responses[1])
                    self.assertTrue(responses[1]["handled"])
                    self.assertNotIn("error", responses[2])
                    saved = self.directory / f"{generation}-{name}"
                    saved.write_bytes(base64.b64decode(responses[2]["pdf"]))
                    reopened = self.run_worker(saved, [
                        {"id": page + 1, "page": page, "width": 300}
                        for page in range(record["expected"]["pages"])
                    ])
                    self.assertTrue(reopened[0]["fieldsComplete"])
                    self.assertEqual(reopened[0]["formType"], 1)
                    self.assertEqual(reopened[0]["pages"], baseline["pages"])
                    expected_fields = [dict(item, value=value) if item["name"] == field_name
                                       else item for item in baseline["fields"]]
                    keys = ("name", "page", "annot", "type", "value")
                    self.assertEqual([[item[key] for key in keys] for item in reopened[0]["fields"]],
                                     [[item[key] for key in keys] for item in expected_fields])
                    for response in reopened[1:]:
                        self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
                    reader_command = [
                        os.environ.get("SAVE_NODE") or "node",
                        str(ROOT / "tools/check_canadian_forms.mjs"),
                        os.environ.get("SAVE_PDFJS") or str(ROOT / "tests/reader/node_modules/pdfjs-dist"),
                        str(original), str(saved), field_name, value,
                    ]
                    result = subprocess.run(reader_command, capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    if generation == 1:
                        reader_command[-1] = "Deliberately incorrect saved value"
                        rejected = subprocess.run(reader_command, capture_output=True, text=True, timeout=20)
                        self.assertNotEqual(rejected.returncode, 0)
                        self.assertIn("AssertionError", rejected.stderr)
                    fixture = saved
            self.assertEqual(original.read_bytes(), source)

    def test_irs_production_xfa_corpus(self):
        directory = ROOT / "tests/pdfs/government-us"
        record = json.loads((directory / "SOURCE.json").read_text())
        fixture = directory / record["file"]
        before = fixture.read_bytes()
        self.assertEqual(len(before), record["bytes"])
        self.assertEqual(hashlib.sha256(before).hexdigest(), record["sha256"])
        expected = record["expected"]
        responses = self.run_worker(fixture, [
            {"id": page + 1, "page": page, "width": 300}
            for page in range(len(expected["pages"]))
        ])
        self.assertEqual(responses[0]["formType"], expected["formType"])
        self.assertEqual(responses[0]["pages"], expected["pages"])
        self.assertEqual(len(responses), len(expected["pages"]) + 1)
        for page, response in enumerate(responses[1:]):
            self.assertNotIn("error", response, f"{fixture.name}: render page {page + 1}")
            self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
        command = [os.environ.get("SAVE_NODE") or "node",
                   str(ROOT / "tools/check_production_xfa.mjs"),
                   os.environ.get("SAVE_PDFJS") or str(ROOT / "tests/reader/node_modules/pdfjs-dist"),
                   str(directory / "SOURCE.json")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        record["file"] = str(fixture)
        record["expected"]["controls_per_page"][0] += 1
        incorrect = self.directory / "incorrect-xfa-expectation.json"
        incorrect.write_text(json.dumps(record))
        command[-1] = str(incorrect)
        rejected = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("AssertionError", rejected.stderr)
        self.assertEqual(fixture.read_bytes(), before)

    def test_irs_production_xfa_repeated_rows(self):
        directory = ROOT / "tests/pdfs/government-us"
        manifest = directory / "f14234c-SOURCE.json"
        record = json.loads(manifest.read_text())
        original = directory / record["file"]
        source = original.read_bytes()
        self.assertEqual(len(source), record["bytes"])
        self.assertEqual(hashlib.sha256(source).hexdigest(), record["sha256"])
        expected = record["expected"]
        opening = self.run_worker(original, [{"id": 1, "page": 0, "width": 300}])
        self.assertEqual(opening[0]["formType"], expected["formType"])
        self.assertEqual(opening[0]["pages"], expected["pages"])
        self.assertTrue(base64.b64decode(opening[1]["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
        reader = [os.environ.get("SAVE_NODE") or "node",
                  str(ROOT / "tools/check_production_xfa.mjs"),
                  os.environ.get("SAVE_PDFJS") or str(ROOT / "tests/reader/node_modules/pdfjs-dist"),
                  str(manifest)]
        result = subprocess.run(reader, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        fixture = original
        scenario = expected["repeated_rows_round_trip"]
        for generation, state in enumerate(scenario["generations"], 1):
            with self.subTest(generation=generation):
                commands = []
                for edit in state["edits"]:
                    commands.extend([
                        {"op": "event", "page": edit["page"], "action": "click",
                         "x": edit["x"], "y": edit["y"]},
                        {"op": "event", "page": edit["page"], "action": "selectAll"},
                        {"op": "event", "page": edit["page"], "action": "text", "text": edit["value"]},
                        {"op": "event", "page": edit["page"], "action": "blur"},
                    ])
                live_start = len(commands) + 1
                commands.extend({"op": "event", "page": state["read_page"],
                                 "action": "click", "x": 120, "y": y} for y in state["read_y"])
                commands.append({"op": "snapshot"})
                responses = self.run_worker(fixture, [dict(command, id=index)
                                                     for index, command in enumerate(commands, 1)])
                for response in responses[1:]:
                    self.assertNotIn("error", response)
                for index, edit in enumerate(state["edits"]):
                    self.assertTrue(responses[index * 4 + 1]["handled"], edit["field"])
                    self.assertTrue(responses[index * 4 + 3]["handled"], edit["field"])
                    self.assertEqual(responses[index * 4 + 3]["text"], edit["value"])
                    self.assertTrue(responses[index * 4 + 4]["handled"])
                for response, value in zip(responses[live_start:-1], state["values"]):
                    self.assertTrue(response["handled"])
                    self.assertEqual(response["text"], value)
                saved = self.directory / f"{generation}-{record['file']}"
                saved.write_bytes(base64.b64decode(responses[-1]["pdf"]))
                read_commands = [{"page": page, "width": 300}
                                 for page in range(state["native_page_count"])]
                read_commands.extend({"op": "event", "page": state["read_page"],
                                      "action": "click", "x": 120, "y": y} for y in state["read_y"])
                reopened = self.run_worker(saved, [dict(command, id=index)
                                                  for index, command in enumerate(read_commands, 1)])
                self.assertEqual(reopened[0]["formType"], expected["formType"])
                self.assertEqual(reopened[0]["pages"], expected["pages"] * state["native_page_count"])
                for response in reopened[1:state["native_page_count"] + 1]:
                    self.assertNotIn("error", response)
                    self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertEqual(len(reopened), len(read_commands) + 1)
                for response, value in zip(reopened[state["native_page_count"] + 1:], state["values"]):
                    self.assertTrue(response["handled"])
                    self.assertEqual(response["text"], value)
                command = reader + [str(saved), str(generation), "repeated_rows_round_trip"]
                result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                incorrect = json.loads(manifest.read_text())
                incorrect["file"] = str(original)
                for mismatch in ("value", "lost-row"):
                    bad_state = json.loads(json.dumps(incorrect))
                    bad_generation = bad_state["expected"]["repeated_rows_round_trip"]["generations"][generation - 1]
                    if mismatch == "value":
                        bad_generation["values"][0] = "Deliberately incorrect saved value"
                    else:
                        bad_generation["values"].pop()
                        bad_generation["count"] = str(len(bad_generation["values"]))
                    bad_manifest = self.directory / f"incorrect-{mismatch}.json"
                    bad_manifest.write_text(json.dumps(bad_state))
                    rejected = subprocess.run(reader[:-1] + [str(bad_manifest)] + command[-3:],
                                              capture_output=True, text=True, timeout=20)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertIn("AssertionError", rejected.stderr)
                fixture = saved
        self.assertEqual(original.read_bytes(), source)

    def test_irs_production_xfa_round_trips(self):
        directory = ROOT / "tests/pdfs/government-us"
        record = json.loads((directory / "SOURCE.json").read_text())
        original = directory / record["file"]
        before = original.read_bytes()
        scenario = record["expected"]["round_trip"]
        fixture = original
        for generation, value in enumerate(scenario["values"], 1):
            with self.subTest(generation=generation):
                click = {"op": "event", "page": scenario["page"], "action": "click",
                         "x": scenario["x"], "y": scenario["y"]}
                responses = self.run_worker(fixture, [
                    dict(click, id=1),
                    {"id": 2, "op": "event", "page": scenario["page"], "action": "selectAll"},
                    {"id": 3, "op": "event", "page": scenario["page"],
                     "action": "text", "text": value},
                    {"id": 4, "op": "event", "page": scenario["page"], "action": "blur"},
                    {"id": 5, "op": "snapshot"},
                ])
                for response in responses[1:]:
                    self.assertNotIn("error", response)
                self.assertTrue(responses[1]["handled"])
                self.assertTrue(responses[3]["handled"])
                self.assertTrue(responses[4]["handled"])
                self.assertEqual(responses[3]["text"], value)
                saved = self.directory / f"{generation}-{record['file']}"
                saved.write_bytes(base64.b64decode(responses[5]["pdf"]))
                reopened = self.run_worker(saved, [dict(click, id=1)] + [
                    {"id": page + 2, "page": page, "width": 300}
                    for page in range(len(record["expected"]["pages"]))
                ])
                self.assertEqual(reopened[0]["formType"], record["expected"]["formType"])
                self.assertEqual(reopened[0]["pages"], record["expected"]["pages"])
                self.assertEqual(reopened[1]["text"], value)
                for response in reopened[2:]:
                    self.assertNotIn("error", response)
                    self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
                command = [os.environ.get("SAVE_NODE") or "node",
                           str(ROOT / "tools/check_production_xfa.mjs"),
                           os.environ.get("SAVE_PDFJS") or str(ROOT / "tests/reader/node_modules/pdfjs-dist"),
                           str(directory / "SOURCE.json"), str(saved), value]
                result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if generation == 1:
                    command[-1] = "Deliberately incorrect saved value"
                    rejected = subprocess.run(command, capture_output=True, text=True, timeout=20)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertIn("AssertionError", rejected.stderr)
                fixture = saved
        self.assertEqual(original.read_bytes(), before)

    def test_irs_production_xfa_calculation_round_trips(self):
        directory = ROOT / "tests/pdfs/government-us"
        record = json.loads((directory / "SOURCE.json").read_text())
        original = directory / record["file"]
        before = original.read_bytes()
        scenario = record["expected"]["calculation_round_trip"]
        fixture = original
        for generation, expected in enumerate(scenario["generations"], 1):
            with self.subTest(generation=generation):
                commands = []
                for field, text in zip(scenario["inputs"], expected["text"], strict=True):
                    click = dict(action="click", x=field["x"], y=field["y"])
                    actions = [click, dict(action="selectAll"), dict(action="text", text=text),
                               dict(action="blur"), click]
                    if field["name"] != "year":
                        actions += [dict(action="text", text="abc"), dict(action="blur"), click]
                    for action in actions:
                        commands.append(dict(id=len(commands) + 1, op="event",
                                             page=scenario["page"], **action))
                commands += [dict(id=len(commands) + 1, op="event", page=0, action="blur"),
                             dict(id=len(commands) + 2, op="snapshot")]
                responses = self.run_worker(fixture, commands)
                offset = 1
                for field, text in zip(scenario["inputs"], expected["text"], strict=True):
                    count = 5 if field["name"] == "year" else 8
                    events = responses[offset:offset + count]
                    for response in events:
                        self.assertNotIn("error", response)
                        self.assertEqual(response["validationMessage"], "")
                    self.assertTrue(events[0]["handled"])
                    self.assertTrue(events[2]["handled"])
                    self.assertTrue(events[3]["handled"])
                    committed = text[:4] if field["name"] == "year" else text
                    self.assertEqual(events[2]["text"], committed)
                    self.assertEqual(events[4]["text"], committed)
                    if count == 8:
                        self.assertEqual(events[5]["text"], committed, "alphabetic input must be rejected")
                        self.assertTrue(events[6]["handled"])
                        self.assertEqual(events[7]["text"], committed)
                    offset += count
                self.assertTrue(responses[-2]["handled"])
                self.assertNotIn("error", responses[-1])
                saved = self.directory / f"calculation-{generation}-{record['file']}"
                saved.write_bytes(base64.b64decode(responses[-1]["pdf"]))
                clicks = [dict(id=index + 1, op="event", page=0, action="click",
                               x=field["x"], y=field["y"])
                          for index, field in enumerate(scenario["inputs"])]
                reopened = self.run_worker(saved, clicks + [
                    dict(id=len(clicks) + page + 1, page=page, width=300)
                    for page in range(len(record["expected"]["pages"]))
                ])
                self.assertEqual(reopened[0]["formType"], record["expected"]["formType"])
                self.assertEqual(reopened[0]["pages"], record["expected"]["pages"])
                for response, field, text in zip(reopened[1:len(clicks) + 1], scenario["inputs"],
                                                 expected["text"], strict=True):
                    self.assertTrue(response["handled"])
                    self.assertEqual(response["text"], text[:4] if field["name"] == "year" else text)
                for response in reopened[len(clicks) + 1:]:
                    self.assertNotIn("error", response)
                    self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
                command = [os.environ.get("SAVE_NODE") or "node",
                           str(ROOT / "tools/check_production_xfa.mjs"),
                           os.environ.get("SAVE_PDFJS") or str(ROOT / "tests/reader/node_modules/pdfjs-dist"),
                           str(directory / "SOURCE.json"), str(saved), str(generation), "calculation_round_trip"]
                result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if generation == 1:
                    for key in ("values", "totals"):
                        incorrect = json.loads(json.dumps(record))
                        incorrect["file"] = str(original)
                        incorrect["expected"]["calculation_round_trip"]["generations"][0][key][0] = "999"
                        manifest = self.directory / f"incorrect-{key}.json"
                        manifest.write_text(json.dumps(incorrect))
                        rejected = subprocess.run(command[:3] + [str(manifest)] + command[4:],
                                                  capture_output=True, text=True, timeout=20)
                        self.assertNotEqual(rejected.returncode, 0)
                        self.assertIn("AssertionError", rejected.stderr)
                fixture = saved
        self.assertEqual(original.read_bytes(), before)

    def test_irs_production_xfa_multiline_round_trips(self):
        directory = ROOT / "tests/pdfs/government-us"
        record = json.loads((directory / "SOURCE.json").read_text())
        original = directory / record["file"]
        before = original.read_bytes()
        scenario = record["expected"]["multiline_round_trip"]
        fixture = original
        for generation, expected in enumerate(scenario["generations"], 1):
            with self.subTest(generation=generation):
                page, value = expected["edit_page"], expected["value"]
                responses = self.run_worker(fixture, [
                    dict(id=1, op="event", page=page, action="click", x=scenario["x"], y=expected["edit_y"]),
                    dict(id=2, op="event", page=page, action="selectAll"),
                    # Native Return is CR; focused text and saved XML normalize it to LF.
                    dict(id=3, op="event", page=page, action="text", text=value.replace("\n", "\r")),
                    dict(id=4, op="event", page=page, action="blur"),
                    dict(id=5, op="snapshot"),
                ])
                for response in responses[1:]:
                    self.assertNotIn("error", response)
                self.assertTrue(responses[1]["handled"])
                self.assertTrue(responses[3]["handled"])
                self.assertEqual(responses[3]["text"], value)
                self.assertTrue(responses[4]["handled"])
                saved = self.directory / f"multiline-{generation}-{record['file']}"
                saved.write_bytes(base64.b64decode(responses[5]["pdf"]))
                renders = [dict(id=page + 1, page=page, width=300)
                           for page in range(expected["native_page_count"])]
                reopened = self.run_worker(saved, renders + [
                    dict(id=98, op="event", page=expected["read_page"], action="click",
                         x=scenario["x"], y=expected["read_y"]),
                ])
                self.assertEqual(reopened[0]["formType"], record["expected"]["formType"])
                self.assertEqual(reopened[0]["pages"],
                                 [record["expected"]["pages"][0]] * expected["native_page_count"])
                for response in reopened[1:len(renders) + 1]:
                    self.assertNotIn("error", response)
                    self.assertTrue(base64.b64decode(response["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
                self.assertTrue(reopened[-1]["handled"])
                self.assertEqual(reopened[-1]["text"], value)
                command = [os.environ.get("SAVE_NODE") or "node",
                           str(ROOT / "tools/check_production_xfa.mjs"),
                           os.environ.get("SAVE_PDFJS") or str(ROOT / "tests/reader/node_modules/pdfjs-dist"),
                           str(directory / "SOURCE.json"), str(saved), str(generation), "multiline_round_trip"]
                result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if generation == 1:
                    incorrect = json.loads(json.dumps(record))
                    incorrect["file"] = str(original)
                    incorrect["expected"]["multiline_round_trip"]["generations"][0]["value"] = "Incorrect saved text"
                    manifest = self.directory / "incorrect-multiline.json"
                    manifest.write_text(json.dumps(incorrect))
                    rejected = subprocess.run(command[:3] + [str(manifest)] + command[4:],
                                              capture_output=True, text=True, timeout=20)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertIn("AssertionError", rejected.stderr)
                fixture = saved
        self.assertEqual(original.read_bytes(), before)

    def test_deterministic_mutation_corpus(self):
        """Small reproducible malformed-input campaign under the real sandbox."""
        randomizer = random.Random(20261010)
        original = (ROOT / "tests/pdfs/acroform/controls.pdf").read_bytes()
        for iteration in range(32):
            data = bytearray(original)
            if iteration % 3 == 0:
                del data[randomizer.randrange(5, len(data)):]
            else:
                for _ in range(1 + iteration % 12):
                    data[randomizer.randrange(5, len(data))] = randomizer.randrange(256)
            fixture = self.directory / f"mutation-{iteration}.pdf"
            fixture.write_bytes(data)
            result = subprocess.run(self.command(WORKER, fixture),
                                    input=b'{"id":1,"page":0,"width":96}\n',
                                    capture_output=True, timeout=10)
            self.assertIn(result.returncode, (0, 1, 2),
                          f"mutation {iteration}: {result.returncode} {result.stderr!r}")
            self.assertNotIn(b"AddressSanitizer", result.stderr)
            self.assertLess(len(result.stdout), 4 * 1024 * 1024)

    def test_export_rejects_unbounded_and_invalid_options(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        for options in ({"pages": [5000], "rotation": 0},
                        {"pages": [0], "rotation": 8},
                        {"pages": [0] * 2001, "rotation": 0},
                        {"pages": [0], "rotation": 0, "mergeLengths": [1000]}):
            payload = b"PDFEDITOR-EXPORT\n" + json.dumps(options).encode() + b"\n"
            result = subprocess.run(self.command(WORKER, fixture) + ["--save"],
                                    input=payload, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertFalse(result.stdout.startswith(b"%PDF-"))

    def test_warm_worker_loads_bounded_pipe_input(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        command = self.command(WORKER, fixture) + ["--warm"]
        with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as process:
            self.assertEqual(json.loads(process.stdout.readline()), {"warm": True})
            self.assertIsNone(process.poll())
            # Exercise binary framing across multiple 64KiB transport chunks.
            pdf = fixture.read_bytes() + b" " * 131072
            payload = json.dumps({"op": "load", "bytes": len(pdf)}).encode() + b"\n" + pdf
            payload += b'{"id":1,"page":0,"width":612}\n'
            output, diagnostics = process.communicate(payload, timeout=10)
            self.assertEqual(process.returncode, 0, diagnostics.decode(errors="replace"))
            replies = [json.loads(line) for line in output.splitlines()]
            self.assertEqual(len(replies[0]["pages"]), 1)
            self.assertIn("png", replies[1])

    def test_warm_worker_rejects_invalid_and_truncated_input(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        for payload in (b'{"op":"load","bytes":67108865}\n',
                        b'{"op":"load","bytes":10}\nshort', b'{"op":"load"'):
            with self.subTest(payload=payload):
                result = subprocess.run(self.command(WORKER, fixture) + ["--warm"],
                                        input=payload, capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)

    def test_kernel_denies_access_even_without_javascript_api_checks(self):
        secret = self.directory / "host-secret"
        secret.write_text("host-only sentinel")
        fixture = ROOT / "tests/pdfs/normal/blank.pdf"
        before = hashlib.sha256(fixture.read_bytes()).digest()
        environment = dict(os.environ, PDF_EDITOR_TEST_SECRET="must-not-leak")
        result = subprocess.run(self.command(POLICY_WORKER, fixture) + [str(secret)],
                                capture_output=True, text=True, timeout=10, env=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS: files, commands, sockets, environment, threads and memory policy", result.stdout)
        self.assertEqual(secret.read_text(), "host-only sentinel")
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).digest(), before)

    def test_document_scripts_cannot_access_host_capabilities(self):
        secret = self.directory / "host-secret"
        secret.write_text("host-only sentinel")
        unwanted = self.directory / "host-write"
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(0.1)
            port = listener.getsockname()[1]
            for kind in ("acroform", "xfa"):
                with self.subTest(kind=kind):
                    original = safety_fixtures()[f"host-access-{kind}.pdf"]
                    # Rebuild stream lengths and xref offsets after substituting targets.
                    from generate_probe_fixtures import acroform_controls, xfa_pdf, SAFETY_SCRIPT
                    script = SAFETY_SCRIPT.replace("/tmp/pdf-editor-host-secret", str(secret)).replace("/tmp/pdf-editor-host-write", str(unwanted)).replace("http://example.invalid/safety", f"http://127.0.0.1:{port}/safety")
                    if kind == "acroform":
                        contents = acroform_controls(script)
                    else:
                        # This fixture's XDP is a single uncompressed stream.
                        start = original.index(b"stream\n") + len(b"stream\n")
                        end = original.index(b"\nendstream", start)
                        xml = original[start:end].replace(b"/tmp/pdf-editor-host-secret", str(secret).encode()).replace(b"http://example.invalid/safety", f"http://127.0.0.1:{port}/safety".encode())
                        contents = xfa_pdf(xml)
                    fixture = self.directory / f"host-{kind}.pdf"
                    fixture.write_bytes(contents)
                    environment = dict(os.environ, PDF_EDITOR_TEST_SECRET="must-not-leak")
                    responses = self.run_worker(fixture, [{"id": 1, "op": "event", "page": 0,
                                                           "action": "click", "x": 100, "y": 87}], environment)
                    audit = responses[1]["text"].split("|")
                    self.assertIn("ran", audit, "adversarial script must actually execute")
                    for name in ("process", "require", "Deno", "fetch", "XMLHttpRequest", "getenv", "system"):
                        self.assertIn(f"{name}:undefined", audit)
                    self.assertIn("read:blocked", audit)
                    self.assertTrue(any(item.startswith("network:") for item in audit))
                    if kind == "xfa":
                        self.assertGreaterEqual(responses[0]["deniedHostRequests"], 1)
                    self.assertEqual(secret.read_text(), "host-only sentinel")
                    self.assertFalse(unwanted.exists())
                    self.assertEqual(fixture.read_bytes(), contents)
                    with self.assertRaises(socket.timeout):
                        listener.accept()

    def test_script_exceptions_and_finite_heavy_scripts(self):
        for kind in ("acroform", "xfa"):
            for name, expected in (("throw", "ran-before-error"), ("stress", "sum:4999950000"), ("memory", "memory:blocked")):
                with self.subTest(kind=kind, script=name):
                    fixture = ROOT / f"tests/pdfs/safety/{name}-{kind}.pdf"
                    responses = self.run_worker(fixture, [
                        {"id": 1, "op": "event", "page": 0, "action": "click", "x": 100, "y": 87},
                        {"id": 2, "page": 0, "width": 612}])
                    self.assertEqual(responses[1]["text"], expected)
                    self.assertIn("png", responses[2])

    def test_invalid_native_requests_do_not_kill_worker(self):
        fixture = ROOT / "tests/pdfs/normal/blank.pdf"
        responses = self.run_worker(fixture, [{"id": 1, "page": -1, "width": 612},
                                             {"id": 2, "op": "unexpected"},
                                             {"id": 3, "page": 0, "width": 96}])
        self.assertIn("error", responses[1])
        self.assertIn("error", responses[2])
        self.assertIn("png", responses[3])
        result = subprocess.run(self.command(WORKER, fixture), input="X" * (65 * 1024) + "\n",
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)

    def test_cyclic_form_and_extreme_geometry(self):
        responses = self.run_worker(ROOT / "tests/pdfs/safety/cyclic-acroform.pdf",
                                    [{"id": 1, "page": 0, "width": 612}])
        self.assertIn("png", responses[1])
        responses = self.run_worker(ROOT / "tests/pdfs/safety/extreme-page.pdf",
                                    [{"id": 1, "page": 0, "width": 2400}])
        self.assertIn("too large", responses[1]["error"])
        result = subprocess.run(self.command(WORKER, ROOT / "tests/pdfs/safety/tiny-page.pdf"),
                                input="", capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unsupported page size", json.loads(result.stdout)["error"])

    def test_large_file_and_page_limit(self):
        fixture = self.directory / "large.pdf"
        fixture.write_bytes(many_pages_pdf(padding=20 * 1024 * 1024))
        responses = self.run_worker(fixture, [{"id": 1, "page": 999, "width": 612}])
        self.assertEqual(len(responses[0]["pages"]), 1000)
        self.assertIn("png", responses[1])
        # Linux wait4 includes reaped descendants, including the Bubblewrap worker.
        peak_kib = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        self.assertLess(peak_kib, 384 * 1024)
        fixture.write_bytes(many_pages_pdf(count=2001))
        result = subprocess.run(self.command(WORKER, fixture), input="", text=True,
                                capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("1 to 2000", json.loads(result.stdout)["error"])

    def test_safety_corpus_is_reproducible(self):
        generate(self.directory)
        for name in safety_fixtures():
            self.assertEqual((ROOT / "tests/pdfs/safety" / name).read_bytes(),
                             (self.directory / "tests/pdfs/safety" / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
