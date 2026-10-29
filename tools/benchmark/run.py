"""Run scene/backend benchmark suites sequentially and retain reproducible artifacts."""

import argparse
from datetime import datetime, timezone
import hashlib
import math
from pathlib import Path
import re
import subprocess
import time

import analyze


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_hash(root):
    if not root.is_dir():
        raise ValueError(f"Missing source directory: {root}")
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def project_hash(path):
    manifest = analyze.read_json(path)
    assets = (path.parent / manifest["assets"]).resolve()
    if not assets.is_dir():
        raise ValueError(f"Missing project assets: {assets}")
    return {"manifest": sha256(path), "assets": tree_hash(assets)}


def bounded(value, low, high, name, integer=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high or (integer and not isinstance(value, int)):
        raise ValueError(f"{name} requires {'integer ' if integer else ''}{low}..{high}")
    return value


def load_suite(config_path, args):
    config = analyze.read_json(config_path)
    allowed = {"schemaVersion", "executable", "backends", "repeats", "defaults", "cases"}
    if config.get("schemaVersion") != 1 or set(config) - allowed:
        raise ValueError("Unsupported suite schema or unknown top-level settings")
    executable = (args.exe if args.exe else config_path.parent / config["executable"]).resolve()
    if not executable.is_file():
        raise ValueError(f"Build Release first; executable missing: {executable}")
    repeats = bounded(args.repeats if args.repeats is not None else config.get("repeats", 3), 1, 100, "repeats", True)
    backends = args.backends or config.get("backends", ["dx12", "vulkan"])
    if not backends or len(set(backends)) != len(backends) or set(backends) - {"dx12", "vulkan"}:
        raise ValueError("Backends must be distinct dx12/vulkan values")
    defaults = {"seconds": 10, "warmupSeconds": 2, "warmupFrames": 5, "width": 1280, "height": 720,
                "quality": "medium", "view": "final", "validation": "off", "cpuProfiling": False,
                "pipeline": "forward", "submission": "direct", "recordingThreads": 1, "asyncCompute": False}
    if set(config.get("defaults", {})) - set(defaults):
        raise ValueError("Unknown benchmark defaults")
    defaults.update(config.get("defaults", {}))
    cases, names = [], set()
    for entry in config["cases"]:
        if set(entry) - (set(defaults) | {"name", "project"}):
            raise ValueError("Unknown case setting")
        case = {**defaults, **entry}
        name = case["name"]
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name) or name in names:
            raise ValueError("Case names must be unique ASCII path components")
        names.add(name)
        if args.seconds is not None:
            case["seconds"] = args.seconds
        if args.warmup_seconds is not None:
            case["warmupSeconds"] = args.warmup_seconds
        if args.validation is not None:
            case["validation"] = args.validation
        if getattr(args, "cpu_profile", None) is not None:
            case["cpuProfiling"] = args.cpu_profile
        if not isinstance(case["cpuProfiling"], bool):
            raise ValueError("cpuProfiling requires a boolean")
        if not isinstance(case["asyncCompute"], bool):
            raise ValueError("asyncCompute requires a boolean")
        if case["pipeline"] not in ("forward", "deferred", "raytracing") or case["submission"] not in ("direct", "indexed-direct", "cpu-indirect", "gpu-culled"):
            raise ValueError("Invalid pipeline/submission")
        bounded(case["seconds"], .001, 3600, "seconds")
        bounded(case["warmupSeconds"], 0, 3600, "warmupSeconds")
        bounded(case["warmupFrames"], 0, 10000, "warmupFrames", True)
        bounded(case["recordingThreads"], 1, 16, "recordingThreads", True)
        for key in ("width", "height"):
            bounded(case[key], 1, 16384, key, True)
        if case["quality"] not in ("scene", "low", "medium", "high") or case["view"] not in ("final", "hdr", "shadow", "environment", "brdf") or case["validation"] not in ("on", "off"):
            raise ValueError("Invalid quality/view/validation (benchmark validation is explicit on/off)")
        project = (config_path.parent / case["project"]).resolve()
        if not project.is_file():
            raise ValueError(f"Missing project: {project}")
        case["project"] = str(project)
        case["inputs"] = project_hash(project)
        cases.append(case)
    if not cases:
        raise ValueError("Suite needs at least one case")
    return executable, backends, repeats, cases


def workspace_state():
    root = Path(__file__).resolve().parents[2]
    try:
        revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(["git", "-C", str(root), "status", "--porcelain", "--untracked-files=no"], text=True))
        return {"sourceRevision": revision, "trackedChanges": dirty, "note": "Workspace state; executable SHA-256 identifies the tested binary."}
    except (OSError, subprocess.CalledProcessError):
        return None


def execute(command, cwd, console, timeout):
    with console.open("wb") as log:
        process = subprocess.Popen(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        try:
            return process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            process.kill()
            process.wait()
            raise


def validate_case(report, case, backend):
    expected = {key: case[key] for key in ("width", "height", "quality", "view", "pipeline", "submission")}
    expected.update(backend=backend, validation=case["validation"] == "on")
    if report.get("recordingThreads", 1) != case.get("recordingThreads", 1):
        raise ValueError("Recorded settings disagree with requested recordingThreads")
    if report.get("asyncCompute", False) != case.get("asyncCompute", False):
        raise ValueError("Recorded settings disagree with requested asyncCompute")
    budgets = {"requestedFrames": 0, "requestedSeconds": case["seconds"],
               "requestedWarmupFrames": case["warmupFrames"], "requestedWarmupSeconds": case["warmupSeconds"]}
    if (any(report[key] != value for key, value in expected.items()) or
        any(report["benchmark"][key] != value for key, value in budgets.items()) or
        report["benchmark"].get("cpuProfiling", False) != case.get("cpuProfiling", False) or
        Path(report["project"]).resolve() != Path(case["project"])):
        raise ValueError("Recorded settings disagree with the requested suite case")


def run_suite(config_path, output, args):
    executable, backends, repeats, cases = load_suite(config_path, args)
    if args.plots:
        analyze.ensure_plotting()
    output.mkdir(parents=True, exist_ok=False)
    engine_hash = tree_hash(executable.parent / "shaders")
    manifest = {"schemaVersion": 1, "kind": "renderlab.benchmark-suite", "complete": False,
                "startedAtUtc": datetime.now(timezone.utc).isoformat(), "config": str(config_path),
                "executable": str(executable), "executableSha256": sha256(executable),
                "engineShadersSha256": engine_hash, "workspace": workspace_state(),
                "cases": cases, "backends": backends, "repeats": repeats, "runs": []}
    manifest_path = output / "manifest.json"
    analyze.write_json(manifest_path, manifest)
    for case in cases:
        for repeat in range(repeats):
            # Reverse each repetition to reduce systematic first/second-backend bias.
            for backend in backends if repeat % 2 == 0 else list(reversed(backends)):
                run_id = f"{case['name']}-{repeat + 1:02d}-{backend}"
                directory = output / run_id
                directory.mkdir()
                report = directory / "report.json"
                capability = directory / "device.json"
                command = [str(executable), "--headless", "--backend", backend, "--project", case["project"],
                           "--validation", case["validation"], "--benchmark-seconds", str(case["seconds"]),
                           "--warmup-seconds", str(case["warmupSeconds"]), "--warmup-frames", str(case["warmupFrames"]),
                           "--width", str(case["width"]), "--height", str(case["height"]),
                           "--quality", case["quality"], "--view", case["view"],
                           "--pipeline", case["pipeline"], "--submission", case["submission"],
                           "--recording-threads", str(case["recordingThreads"]),
                           "--profile", str(report), "--samples", str(directory / "frames.jsonl"),
                           "--output", str(directory / "capture"), "--report", str(capability), "--log", str(directory / "player.log")]
                if case["cpuProfiling"]:
                    command.append("--cpu-profile")
                if case["asyncCompute"]:
                    command.append("--async-compute")
                item = {"id": run_id, "case": case["name"], "repeat": repeat, "backend": backend,
                        "sequence": len(manifest["runs"]), "command": command, "cwd": str(executable.parent),
                        "report": report.relative_to(output).as_posix(), "status": "running"}
                manifest["runs"].append(item)
                analyze.write_json(manifest_path, manifest)
                print(f"[{item['sequence'] + 1}/{len(cases) * repeats * len(backends)}] {run_id}: warmup {case['warmupSeconds']}s, measure {case['seconds']}s", flush=True)
                started = time.perf_counter()
                try:
                    item["exitCode"] = execute(command, executable.parent, directory / "console.log", case["seconds"] + case["warmupSeconds"] + 600)
                    if item["exitCode"] != 0:
                        raise ValueError(f"Player exited {item['exitCode']}; see {directory / 'console.log'}")
                    recorded, _, _, _ = analyze.load_recording(report)
                    validate_case(recorded, case, backend)
                    if analyze.read_json(capability)["validationErrors"] != 0:
                        raise ValueError("Backend or validation mismatch")
                    item["status"] = "passed"
                except (ValueError, OSError, KeyError, TypeError, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
                    item["status"] = "failed"
                    item["error"] = str(error) or type(error).__name__
                    raise
                finally:
                    item["processSeconds"] = time.perf_counter() - started
                    analyze.write_json(manifest_path, manifest)
    if sha256(executable) != manifest["executableSha256"] or tree_hash(executable.parent / "shaders") != engine_hash or any(project_hash(Path(case["project"])) != case["inputs"] for case in cases):
        manifest["error"] = "Executable or source assets changed during suite; results are not comparable"
        analyze.write_json(manifest_path, manifest)
        raise ValueError(manifest["error"])
    manifest["complete"] = True
    manifest["finishedAtUtc"] = datetime.now(timezone.utc).isoformat()
    analyze.write_json(manifest_path, manifest)
    analyze.analyze([manifest_path], output / "analysis", args.plots)
    print(output / "analysis/report.md", flush=True)
    return manifest_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--exe", type=Path, help="Override executable (paths relative to current directory)")
    parser.add_argument("--output", type=Path, help="New output directory, must not already exist")
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--warmup-seconds", type=float)
    parser.add_argument("--repeats", type=int)
    parser.add_argument("--validation", choices=("on", "off"))
    parser.add_argument("--cpu-profile", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--backends", nargs="+", choices=("dx12", "vulkan"))
    parser.add_argument("--plots", action="store_true")
    args = parser.parse_args()
    output = (args.output or Path("out/benchmarks") / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")).resolve()
    try:
        run_suite(args.config.resolve(), output, args)
    except (ValueError, OSError, KeyError, TypeError, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        parser.exit(1, f"Suite failed: {error or type(error).__name__}\nArtifacts: {output}\n")


if __name__ == "__main__":
    main()
