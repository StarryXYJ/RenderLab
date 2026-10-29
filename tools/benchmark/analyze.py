"""Analyze completed RenderLab recordings. Python 3.10+, plotting is optional."""

import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def read_json(path):
    with Path(path).open(encoding="utf-8") as stream:
        return json.load(stream)


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(path)


def finite(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
        raise ValueError(f"Invalid nonnegative timing: {value!r}")
    return float(value)


def percentile(ordered, fraction):
    """Linear interpolation at (N-1)*fraction, including one-sample recordings."""
    position = (len(ordered) - 1) * fraction
    low = math.floor(position)
    high = math.ceil(position)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def distribution(values):
    ordered = sorted(values)
    return {"samples": len(values), "meanMs": statistics.fmean(values),
            "minMs": ordered[0], "maxMs": ordered[-1],
            "p50Ms": percentile(ordered, .5), "p95Ms": percentile(ordered, .95),
            "p99Ms": percentile(ordered, .99),
            "stdevMs": statistics.stdev(values) if len(values) > 1 else 0}


def load_recording(path):
    path = Path(path).resolve()
    report = read_json(path)
    if report.get("schemaVersion") != 2 or report.get("benchmark", {}).get("complete") is not True:
        raise ValueError(f"Not a completed benchmark v2 report: {path}")
    benchmark = report["benchmark"]
    if benchmark.get("sampleSchemaVersion") != 1 or benchmark.get("timingMode") != "headless-serial":
        raise ValueError(f"Unsupported sample schema or timing mode: {path}")
    metrics, timestamps = {}, []
    samples_path = path.parent / benchmark["samplesFile"]
    with samples_path.open(encoding="utf-8") as stream:
        for index, line in enumerate(stream):
            row = json.loads(line)
            if row.get("schemaVersion") != 1 or row.get("frame") != index:
                raise ValueError(f"Out-of-order/unsupported raw frame {index}: {samples_path}")
            elapsed = finite(row["elapsedMs"])
            if timestamps and elapsed < timestamps[-1]:
                raise ValueError(f"Non-monotonic timestamp in {samples_path}")
            timestamps.append(elapsed)
            for field, prefix in (("metricsMs", ""), ("gpuPassesMs", "gpu/")):
                for name, value in row[field].items():
                    if not name or (not prefix and name.startswith("gpu/")):
                        raise ValueError("Empty metric name or reserved gpu/ prefix")
                    metrics.setdefault(prefix + name, []).append(finite(value))
            for name in ("frameTimeMs", "cpuRenderMs", "gpuWaitMs", "gpuPassSumMs"):
                if name not in row["metricsMs"]:
                    raise ValueError(f"Missing required {name} at frame {index}")
            if row["metricsMs"]["frameTimeMs"] <= 0:
                raise ValueError("Frame time must be positive")
            if not math.isclose(sum(row["gpuPassesMs"].values()), row["metricsMs"]["gpuPassSumMs"], rel_tol=1e-8, abs_tol=1e-9):
                raise ValueError(f"GPU pass ranges disagree with sum at frame {index}")
    if not timestamps or len(timestamps) != benchmark["sampleCount"] or timestamps[-1] > finite(benchmark["wallTimeMs"]):
        raise ValueError(f"Truncated or inconsistent recording: {path}")
    if ((benchmark["requestedFrames"] and len(timestamps) != benchmark["requestedFrames"]) or
        benchmark["wallTimeMs"] < finite(benchmark["requestedSeconds"]) * 1000 or
        benchmark["warmupFrames"] < benchmark["requestedWarmupFrames"] or
        finite(benchmark["warmupMs"]) < finite(benchmark["requestedWarmupSeconds"]) * 1000):
        raise ValueError(f"Measurement/warmup budget was not completed: {path}")
    summaries = {name: distribution(values) for name, values in metrics.items()}
    if set(benchmark["metricsMs"]) != {name for name in summaries if not name.startswith("gpu/")}:
        raise ValueError(f"Raw metric identities disagree with summary: {path}")
    for name, expected in benchmark["metricsMs"].items():
        actual = summaries.get(name)
        if actual is None or actual["samples"] != expected["samples"] or any(
            not math.isclose(actual[key], finite(expected[key]), rel_tol=1e-8, abs_tol=1e-9)
            for key in ("meanMs", "minMs", "maxMs")
        ):
            raise ValueError(f"Raw samples disagree with summary {name}: {path}")
    for name, operation in benchmark.get("cpuOperations", {}).items():
        if name not in metrics or not math.isclose(math.fsum(metrics[name]), finite(operation["totalMs"]), rel_tol=1e-8, abs_tol=1e-6):
            raise ValueError(f"CPU operation total disagrees with frames: {name}")
    return report, summaries, metrics, timestamps


def collect_inputs(inputs):
    entries = []
    for source in inputs:
        source = Path(source).resolve()
        data = read_json(source)
        if data.get("kind") == "renderlab.benchmark-suite":
            if data.get("schemaVersion") != 1 or data.get("complete") is not True or not data.get("runs"):
                raise ValueError(f"Incomplete/unsupported suite: {source}")
            for run in data["runs"]:
                if run.get("status") != "passed" or run.get("exitCode") != 0:
                    raise ValueError(f"Failed suite run: {run.get('id')}")
                case = next(case for case in data["cases"] if case["name"] == run["case"])
                provenance = {"executableSha256": data["executableSha256"],
                              "engineShadersSha256": data["engineShadersSha256"], "inputs": case["inputs"]}
                entries.append((source.parent / run["report"], run["case"], run["repeat"], run["backend"], provenance))
        else:
            entries.append((source, Path(data["project"]).stem, len(entries), data["backend"], None))
    return entries


def compatible_key(report):
    identity = report["adapterIdentity"]
    benchmark = report["benchmark"]
    return (str(Path(report["project"]).resolve()), report["width"], report["height"], report["quality"],
            report["view"], report["validation"], report["buildConfiguration"], report["adapter"],
            identity["vendorId"], identity["deviceId"], identity["software"],
            benchmark["timingMode"], benchmark["requestedFrames"], benchmark["requestedSeconds"],
            benchmark["requestedWarmupFrames"], benchmark["requestedWarmupSeconds"],
            benchmark.get("cpuProfiling", False), report.get("pipeline", "forward"),
            report.get("submission", "direct"), report.get("recordingThreads", 1), report.get("asyncCompute", False))


def ensure_plotting():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise ValueError("Plots require: python -m pip install -r tools/benchmark/requirements.txt") from error
    return plt


def plot_range_bars(output, filename, runs, names, labels, xlabel, title):
    plt = ensure_plotting()
    figure, axis = plt.subplots(figsize=(10, max(3, len(names) * .6)), layout="constrained")
    backends = sorted({run["backend"] for run in runs})
    for backend_index, backend in enumerate(backends):
        values = [statistics.fmean([run["metrics"][name]["meanMs"] for run in runs if run["backend"] == backend and name in run["metrics"]])
                  if any(run["backend"] == backend and name in run["metrics"] for run in runs) else math.nan for name in names]
        height = .8 / len(backends)
        axis.barh([i + (backend_index + .5) * height - .4 for i in range(len(names))], values, height=height, label=backend)
    axis.set(yticks=range(len(names)), yticklabels=labels, xlabel=xlabel, title=title)
    axis.legend()
    axis.grid(axis="x", alpha=.25)
    for extension in ("png", "svg"):
        figure.savefig(output / f"{filename}.{extension}", dpi=150)
    plt.close(figure)


def plot_case(output, case_index, runs):
    plt = ensure_plotting()
    prefix = f"case-{case_index}"
    figure, axes = plt.subplots(1, 2, figsize=(12, 4), layout="constrained")
    for run in runs:
        values = run["raw"]["frameTimeMs"]
        stride = max(1, math.ceil(len(values) / 2000))
        label = f"{run['backend']} #{run['repeat'] + 1}"
        style = ("-", "--", ":")[run["repeat"] % 3]
        color = {"dx12": "#2563eb", "vulkan": "#ea580c"}.get(run["backend"])
        axes[0].plot([t / 1000 for t in run["timestamps"][::stride]], values[::stride], linewidth=.7, linestyle=style, color=color, label=label)
        ordered = sorted(values)
        axes[1].plot(ordered[::stride], [(i + 1) / len(ordered) * 100 for i in range(0, len(ordered), stride)], linestyle=style, color=color, label=label)
    axes[0].set(xlabel="Measurement elapsed (s)", ylabel="Serial frame latency (ms)", title="Frame time (display downsampled)")
    axes[1].set(xlabel="Serial frame latency (ms)", ylabel="Cumulative frames (%)", title="Empirical distribution (all samples analyzed)")
    axes[0].set_yscale("log")
    axes[1].set_xscale("log")
    for axis in axes:
        axis.grid(alpha=.25)
        axis.legend(fontsize=8)
    for extension in ("png", "svg"):
        figure.savefig(output / f"{prefix}-frames.{extension}", dpi=150)
    plt.close(figure)
    passes = sorted({name for run in runs for name in run["metrics"] if name.startswith("gpu/")})
    plot_range_bars(output, prefix + "-passes", runs, passes, [name[4:] for name in passes],
                    "GPU pass mean (ms), mean of runs", "GPU ranges (exclude barriers, wait and Present)")
    images = [f"{prefix}-frames.png", f"{prefix}-passes.png"]
    operations = sorted({name for run in runs for name in run["metadata"]["benchmark"].get("cpuOperations", {})})
    if operations:
        plot_range_bars(output, prefix + "-cpu", runs, operations,
                        [name.removeprefix("rhi").removesuffix("Ms") for name in operations],
                        "CPU operation mean (ms per frame), mean of runs", "RHI CPU diagnostic scopes")
        images.append(f"{prefix}-cpu.png")
    return images


def analyze(inputs, output, plots=False):
    output = Path(output).resolve()
    runs, configurations, drivers, seen = [], {}, {}, set()
    for path, case, repeat, expected_backend, provenance in collect_inputs(inputs):
        path = Path(path).resolve()
        if path in seen:
            raise ValueError(f"Duplicate recording: {path}")
        seen.add(path)
        report, metrics, raw, timestamps = load_recording(path)
        if not report["fixedCamera"] or report["configurationMayChange"] or report["backend"] != expected_backend:
            raise ValueError(f"Run configuration changed or backend mismatch: {path}")
        key = (compatible_key(report), json.dumps(provenance, sort_keys=True))
        if case in configurations and configurations[case] != key:
            raise ValueError(f"Incomparable configurations/adapters for case {case}")
        configurations[case] = key
        driver_key = (case, report["backend"])
        if driver_key in drivers and drivers[driver_key] != report["driver"]:
            raise ValueError(f"Driver changed between runs of {case}/{report['backend']}")
        drivers[driver_key] = report["driver"]
        runs.append({"case": case, "backend": report["backend"], "repeat": repeat, "report": str(path),
                     "metadata": report, "provenance": provenance, "metrics": metrics, "raw": raw, "timestamps": timestamps})
    if not runs:
        raise ValueError("No completed recordings")
    output.mkdir(parents=True, exist_ok=True)
    aggregates = []
    for case, backend in sorted({(run["case"], run["backend"]) for run in runs}):
        selected = [run for run in runs if (run["case"], run["backend"]) == (case, backend)]
        means = [run["metrics"]["frameTimeMs"]["meanMs"] for run in selected]
        aggregates.append({"case": case, "backend": backend, "runs": len(selected),
                           "frameMeanMs": statistics.fmean(means),
                           "runMeanStdevMs": statistics.stdev(means) if len(means) > 1 else None,
                           "frameP95MeanMs": statistics.fmean(run["metrics"]["frameTimeMs"]["p95Ms"] for run in selected),
                           "cpuMeanMs": statistics.fmean(run["metrics"]["cpuRenderMs"]["meanMs"] for run in selected),
                           "gpuPassSumMeanMs": statistics.fmean(run["metrics"]["gpuPassSumMs"]["meanMs"] for run in selected),
                           "throughputFps": statistics.fmean(run["metadata"]["benchmark"]["throughputFps"] for run in selected)})
    result = {"schemaVersion": 1, "kind": "renderlab.benchmark-analysis", "aggregates": aggregates,
              "runs": [{k: v for k, v in run.items() if k not in ("raw", "timestamps")} for run in runs]}
    write_json(output / "analysis.json", result)
    fields = ("case", "backend", "repeat", "metric", "samples", "meanMs", "minMs", "maxMs", "p50Ms", "p95Ms", "p99Ms", "stdevMs")
    with (output / "metrics.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for run in runs:
            for name, metric in run["metrics"].items():
                writer.writerow({"case": run["case"], "backend": run["backend"], "repeat": run["repeat"], "metric": name, **metric})
    lines = ["# RenderLab benchmark analysis", "", "Serial headless frames: render/submit through GPU completion. No Present or CPU/GPU frame overlap.",
             "GPU pass sums exclude barriers and query resolution; throughput includes sample collection/output overhead.",
             "Each aggregate is an equal-weight mean of runs. P95 is the mean of per-run P95s; stdev measures variation between run means.", "",
             "| Case | Backend | Runs | Frame mean ms | Frame P95 ms | Run mean stdev ms | CPU ms | GPU pass sum ms | Wall FPS |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for item in aggregates:
        deviation = f"{item['runMeanStdevMs']:.4f}" if item["runMeanStdevMs"] is not None else "n/a"
        lines.append(f"| {item['case']} | {item['backend']} | {item['runs']} | {item['frameMeanMs']:.4f} | {item['frameP95MeanMs']:.4f} | {deviation} | {item['cpuMeanMs']:.4f} | {item['gpuPassSumMeanMs']:.4f} | {item['throughputFps']:.1f} |")
    for index, case in enumerate(sorted(configurations)):
        selected = [run for run in runs if run["case"] == case]
        metadata = selected[0]["metadata"]
        lines.extend(["", f"## {case}", "", f"{metadata['adapter']}; {metadata['buildConfiguration']}; validation={metadata['validation']}; {metadata['width']}x{metadata['height']}; quality={metadata['quality']}; view={metadata['view']}."])
        lines.extend(["", "| Backend / repeat | Frames | Mean ms | P50 ms | P95 ms | P99 ms |",
                      "| --- | ---: | ---: | ---: | ---: | ---: |"])
        for run in selected:
            metric = run["metrics"]["frameTimeMs"]
            lines.append(f"| {run['backend']} / {run['repeat'] + 1} | {metric['samples']} | {metric['meanMs']:.4f} | {metric['p50Ms']:.4f} | {metric['p95Ms']:.4f} | {metric['p99Ms']:.4f} |")
        operations = sorted({name for run in selected for name in run["metadata"]["benchmark"].get("cpuOperations", {})})
        if operations:
            lines.extend(["", "CPU diagnostic scopes include driver/OS CPU wall time; FenceWait excludes Collect retirement.",
                          "", "| Operation | Backend | Mean ms/frame | Calls/frame |", "| --- | --- | ---: | ---: |"])
            for name in operations:
                for backend in sorted({run["backend"] for run in selected}):
                    matching = [run for run in selected if run["backend"] == backend and name in run["metadata"]["benchmark"].get("cpuOperations", {})]
                    if matching:
                        mean_ms = statistics.fmean(run["metrics"][name]["meanMs"] for run in matching)
                        calls = statistics.fmean(run["metadata"]["benchmark"]["cpuOperations"][name]["calls"] / run["metadata"]["benchmark"]["sampleCount"] for run in matching)
                        lines.append(f"| {name} | {backend} | {mean_ms:.4f} | {calls:.2f} |")
        pair = {item["backend"]: item for item in aggregates if item["case"] == case}
        if "dx12" in pair and "vulkan" in pair:
            delta = (pair["vulkan"]["frameMeanMs"] / pair["dx12"]["frameMeanMs"] - 1) * 100
            lines.extend(["", f"Vulkan serial frame time relative to DX12: {delta:+.2f}% (lower is faster)."])
        if any(item["runMeanStdevMs"] is not None and item["runMeanStdevMs"] / item["frameMeanMs"] > .1 for item in pair.values()):
            lines.extend(["", "Observed between-run variation exceeds 10% of the mean. Inspect individual runs and repeat on an otherwise idle machine before attributing the difference to backend design."])
        if metadata["validation"] or metadata["buildConfiguration"] != "Release":
            lines.extend(["", "Correctness/debug run: validation or Debug overhead is included; this is not a Release performance baseline."])
        if plots:
            for filename in plot_case(output, index, selected):
                lines.extend(["", f"![{filename}]({filename})"])
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="Completed suite manifests or v2 run reports")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plots", action="store_true", help="Export PNG/SVG with matplotlib")
    args = parser.parse_args()
    try:
        if args.plots:
            ensure_plotting()
        analyze(args.inputs, args.output, args.plots)
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f"Analysis failed: {error}\n")
    print(args.output / "report.md")


if __name__ == "__main__":
    main()
