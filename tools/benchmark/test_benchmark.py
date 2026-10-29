"""Data integrity and aggregation checks; no GPU or plotting packages required."""

import argparse
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import analyze
import run


class BenchmarkTests(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(__file__).resolve().parents[2] / "out/benchmark-tests"
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=temporary_root)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def recording(self, name="first", backend="dx12", values=(1, 2, 3, 4)):
        directory = self.root / name
        directory.mkdir()
        metrics = {"frameTimeMs": list(values), "cpuRenderMs": [.5] * len(values),
                   "gpuWaitMs": [.5] * len(values), "gpuPassSumMs": [.1] * len(values)}
        rows = [{"schemaVersion": 1, "frame": i, "elapsedMs": (i + 1) * 10,
                 "metricsMs": {key: data[i] for key, data in metrics.items()}, "gpuPassesMs": {"Draw": .1}}
                for i in range(len(values))]
        raw = directory / "frames.jsonl"
        raw.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
        report = {"schemaVersion": 2, "project": str(self.root / "Pbr.rlproject"), "backend": backend,
                  "width": 1280, "height": 720, "quality": "medium", "view": "final", "validation": False,
                  "pipeline": "forward", "submission": "direct",
                  "buildConfiguration": "Release", "adapter": "Fixture GPU", "fixedCamera": True,
                  "driver": "fixture",
                  "configurationMayChange": False, "adapterIdentity": {"vendorId": 1, "deviceId": 2, "software": False},
                  "benchmark": {"complete": True, "sampleSchemaVersion": 1, "samplesFile": "frames.jsonl",
                                "sampleCount": len(values), "wallTimeMs": len(values) * 10 + 1, "throughputFps": 90,
                                "timingMode": "headless-serial", "requestedFrames": 0, "requestedSeconds": .001,
                                "requestedWarmupFrames": 5, "requestedWarmupSeconds": 2,
                                "warmupFrames": 5, "warmupMs": 2001,
                                "metricsMs": {name: {key: value for key, value in analyze.distribution(data).items()
                                                     if key in ("samples", "meanMs", "minMs", "maxMs")}
                                              for name, data in metrics.items()}}}
        path = directory / "report.json"
        analyze.write_json(path, report)
        return path, report, raw, rows

    def test_percentiles(self):
        stats = analyze.distribution([4, 1, 3, 2])
        self.assertEqual(stats["p50Ms"], 2.5)
        self.assertAlmostEqual(stats["p95Ms"], 3.85)
        self.assertEqual(analyze.distribution([7])["p99Ms"], 7)

    def test_aggregation_equal_weight_runs_and_round_trip(self):
        a, _, _, _ = self.recording()
        b, _, _, _ = self.recording("second", values=(10,))
        result = analyze.analyze([a, b], self.root / "analysis")
        self.assertEqual(result["aggregates"][0]["frameMeanMs"], 6.25)
        self.assertEqual(result["aggregates"][0]["runs"], 2)
        self.assertTrue((self.root / "analysis/metrics.csv").read_text().startswith("case,backend"))
        self.assertEqual(analyze.read_json(self.root / "analysis/analysis.json"), result)

    def test_truncated_stream(self):
        path, _, raw, rows = self.recording()
        raw.write_text("\n".join(json.dumps(row) for row in rows[:-1]) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Truncated"):
            analyze.load_recording(path)

    def test_bad_frame_identity_and_nonfinite_values(self):
        path, _, raw, rows = self.recording()
        for bad in ("identity", "nan", "negative"):
            changed = copy.deepcopy(rows)
            if bad == "identity":
                changed[1]["frame"] = 3
            else:
                changed[1]["metricsMs"]["frameTimeMs"] = float("nan") if bad == "nan" else -1
            raw.write_text("".join(json.dumps(row) + "\n" for row in changed), encoding="utf-8")
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                analyze.load_recording(path)

    def test_summary_disagreement(self):
        path, report, _, _ = self.recording()
        report["benchmark"]["metricsMs"]["frameTimeMs"]["meanMs"] = 900
        analyze.write_json(path, report)
        with self.assertRaisesRegex(ValueError, "disagree"):
            analyze.load_recording(path)

    def test_unmet_measurement_budget(self):
        path, report, _, _ = self.recording()
        report["benchmark"]["requestedSeconds"] = 10
        analyze.write_json(path, report)
        with self.assertRaisesRegex(ValueError, "budget"):
            analyze.load_recording(path)

    def test_cpu_diagnostic_totals_and_report(self):
        path, report, raw, rows = self.recording()
        for row in rows:
            row["metricsMs"]["rhiBufferCreateMs"] = .25
        raw.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
        report["benchmark"]["cpuProfiling"] = True
        report["benchmark"]["metricsMs"]["rhiBufferCreateMs"] = {
            "samples": 4, "meanMs": .25, "minMs": .25, "maxMs": .25}
        report["benchmark"]["cpuOperations"] = {"rhiBufferCreateMs": {"calls": 8, "totalMs": 1}}
        analyze.write_json(path, report)
        analyze.analyze([path], self.root / "analysis")
        self.assertIn("rhiBufferCreateMs | dx12 | 0.2500 | 2.00", (self.root / "analysis/report.md").read_text())
        report["benchmark"]["cpuOperations"]["rhiBufferCreateMs"]["totalMs"] = 2
        analyze.write_json(path, report)
        with self.assertRaisesRegex(ValueError, "CPU operation total"):
            analyze.load_recording(path)

    def test_mismatched_configuration(self):
        a, _, _, _ = self.recording()
        b, report, _, _ = self.recording("second", "vulkan")
        for key, value in (("width", 640), ("validation", True), ("buildConfiguration", "Debug")):
            changed = copy.deepcopy(report)
            changed[key] = value
            analyze.write_json(b, changed)
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "Incomparable"):
                analyze.analyze([a, b], self.root / "analysis")
        changed = copy.deepcopy(report)
        changed["benchmark"]["cpuProfiling"] = True
        analyze.write_json(b, changed)
        with self.assertRaisesRegex(ValueError, "Incomparable"):
            analyze.analyze([a, b], self.root / "analysis")

    def test_failed_suite_and_duplicate_reports(self):
        path, _, _, _ = self.recording()
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            analyze.analyze([path, path], self.root / "analysis")
        manifest = self.root / "manifest.json"
        analyze.write_json(manifest, {"schemaVersion": 1, "kind": "renderlab.benchmark-suite", "complete": True,
                                     "runs": [{"id": "failed", "status": "failed", "exitCode": 1}]})
        with self.assertRaisesRegex(ValueError, "Failed suite"):
            analyze.collect_inputs([manifest])

    def test_geometry_submission_and_pipeline_must_match(self):
        a, _, _, _ = self.recording()
        b, _, _, _ = self.recording("second", "vulkan")
        for key, value in (("submission", "gpu-culled"), ("pipeline", "deferred")):
            report = analyze.read_json(b)
            report.pop("submission", None)
            report.pop("pipeline", None)
            report[key] = value
            analyze.write_json(b, report)
            with self.assertRaisesRegex(ValueError, "Incomparable"):
                analyze.analyze([a, b], self.root / f"analysis-{key}")

    def test_runner_preserves_geometry_submission(self):
        config, data, args = self.suite()
        data["defaults"] = {"pipeline": "deferred", "submission": "gpu-culled"}
        analyze.write_json(config, data)
        _, _, _, cases = run.load_suite(config, args)
        self.assertEqual(cases[0]["submission"], "gpu-culled")
        with patch("run.execute", return_value=7), self.assertRaises(ValueError):
            run.run_suite(config, self.root / "output", args)
        command = analyze.read_json(self.root / "output/manifest.json")["runs"][0]["command"]
        self.assertEqual(command[command.index("--submission") + 1], "gpu-culled")
        data["defaults"]["submission"] = "invalid"
        analyze.write_json(config, data)
        with self.assertRaisesRegex(ValueError, "pipeline/submission"):
            run.load_suite(config, args)

    def test_suite_provenance_must_match(self):
        a, _, _, _ = self.recording()
        b, _, _, _ = self.recording("second", "vulkan")
        manifests = []
        for i, (path, backend) in enumerate(((a, "dx12"), (b, "vulkan"))):
            manifest = self.root / f"suite-{i}.json"
            analyze.write_json(manifest, {"schemaVersion": 1, "kind": "renderlab.benchmark-suite", "complete": True,
                "executableSha256": str(i), "engineShadersSha256": "same",
                "cases": [{"name": "pbr", "inputs": {"manifest": "same", "assets": "same"}}],
                "runs": [{"case": "pbr", "repeat": 0, "backend": backend, "status": "passed", "exitCode": 0,
                          "report": path.relative_to(self.root).as_posix()}]})
            manifests.append(manifest)
        with self.assertRaisesRegex(ValueError, "Incomparable"):
            analyze.analyze(manifests, self.root / "analysis")

    def suite(self):
        executable = self.root / "bin/player.exe"
        executable.parent.mkdir()
        executable.write_bytes(b"fixture")
        (executable.parent / "shaders").mkdir()
        (self.root / "Assets").mkdir()
        analyze.write_json(self.root / "Pbr.rlproject", {"assets": "Assets"})
        config = self.root / "suite.json"
        data = {"schemaVersion": 1, "executable": "bin/player.exe", "repeats": 1,
                "cases": [{"name": "pbr", "project": "Pbr.rlproject"}]}
        analyze.write_json(config, data)
        args = argparse.Namespace(exe=None, backends=None, repeats=None, seconds=None, warmup_seconds=None, validation=None, plots=False)
        return config, data, args

    def test_runner_journals_child_failure(self):
        config, _, args = self.suite()
        output = self.root / "output"
        with patch("run.execute", return_value=7), self.assertRaisesRegex(ValueError, "exited 7"):
            run.run_suite(config, output, args)
        manifest = analyze.read_json(output / "manifest.json")
        self.assertFalse(manifest["complete"])
        self.assertEqual(manifest["runs"][0]["status"], "failed")
        self.assertEqual(manifest["runs"][0]["exitCode"], 7)
        self.assertFalse((output / "analysis/report.md").exists())

    def test_runner_rejects_invalid_config_and_existing_output(self):
        config, data, args = self.suite()
        data["defaults"] = {"unknown": 12}
        analyze.write_json(config, data)
        with self.assertRaisesRegex(ValueError, "Unknown"):
            run.load_suite(config, args)
        data.pop("defaults")
        analyze.write_json(config, data)
        with self.assertRaises(FileExistsError):
            run.run_suite(config, self.root, args)
        args.seconds = float("nan")
        with self.assertRaisesRegex(ValueError, "seconds"):
            run.load_suite(config, args)

    def test_runner_does_not_publish_changed_inputs(self):
        config, data, args = self.suite()
        data["backends"] = ["dx12"]
        data["defaults"] = {"seconds": .001}
        analyze.write_json(config, data)
        fixture, _, raw, _ = self.recording()
        output = self.root / "changed-source"

        def child(command, cwd, console, timeout):
            report_path = Path(command[command.index("--profile") + 1])
            report_path.write_bytes(fixture.read_bytes())
            report_path.with_name("frames.jsonl").write_bytes(raw.read_bytes())
            capability_path = Path(command[command.index("--report") + 1])
            analyze.write_json(capability_path, {"validationErrors": 0})
            (self.root / "bin/player.exe").write_bytes(b"changed during measurement")
            return 0

        with patch("run.execute", side_effect=child), self.assertRaisesRegex(ValueError, "changed during suite"):
            run.run_suite(config, output, args)
        manifest = analyze.read_json(output / "manifest.json")
        self.assertFalse(manifest["complete"])
        self.assertIn("not comparable", manifest["error"])
        self.assertFalse((output / "analysis/report.md").exists())


if __name__ == "__main__":
    unittest.main()
