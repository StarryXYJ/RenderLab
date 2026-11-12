"""Compare path tracing checkpoints against an independent linear RGB PFM reference."""

import argparse
import array
import json
import math
import statistics
import sys
from pathlib import Path


def read_pfm(path):
    with path.open("rb") as stream:
        if stream.readline().strip() != b"PF":
            raise ValueError(f"{path}: expected RGB PFM")
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        if width <= 0 or height <= 0 or abs(scale) != 1:
            raise ValueError(f"{path}: unsupported PFM dimensions or scale")
        values = array.array("f")
        values.frombytes(stream.read())
    if len(values) != width * height * 3:
        raise ValueError(f"{path}: unexpected PFM pixel data length")
    if (scale < 0) != (sys.byteorder == "little"):
        values.byteswap()
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{path}: nonfinite HDR pixel")
    return (width, height), values


def compare(values, reference):
    count = len(reference)
    mse = math.fsum((value - target) ** 2 for value, target in zip(values, reference)) / count
    mean = math.fsum(values) / count
    outliers = sum(value > 4 * target + 0.1 for value, target in zip(values, reference))
    return {"mse": mse, "mean": mean, "outlier_channels": outliers}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path, help="independent reference-mean.pfm")
    parser.add_argument("baseline", type=Path, help="directory containing seed-N-Sspp.pfm")
    parser.add_argument("candidate", type=Path, help="directory containing seed-N-Sspp.pfm")
    parser.add_argument("--seeds", type=int, default=12, help="consecutive seeds beginning at 1")
    parser.add_argument("--spp", type=int, nargs="+", default=[64, 256, 1024, 4096])
    parser.add_argument("--output", type=Path, help="optional JSON output path")
    args = parser.parse_args()
    if args.seeds < 1 or any(spp < 1 for spp in args.spp):
        parser.error("seeds and sample counts must be positive")
    size, reference = read_pfm(args.reference)
    output = {"width": size[0], "height": size[1], "seeds": args.seeds,
              "reference_mean": math.fsum(reference) / len(reference), "samples": []}
    for spp in args.spp:
        rows = []
        for seed in range(1, args.seeds + 1):
            name = f"seed-{seed}-{spp}spp.pfm"
            measurements = []
            for folder in (args.baseline, args.candidate):
                image_size, image = read_pfm(folder / name)
                if image_size != size:
                    raise ValueError(f"{folder / name}: reference size mismatch")
                measurements.append(compare(image, reference))
            rows.append({"seed": seed, "baseline": measurements[0], "candidate": measurements[1]})
        summary = {}
        for label in ("baseline", "candidate"):
            summary[label] = {
                "mean_mse": statistics.mean(row[label]["mse"] for row in rows),
                "median_mse": statistics.median(row[label]["mse"] for row in rows),
                "max_mse": max(row[label]["mse"] for row in rows),
                "mean_radiance": statistics.mean(row[label]["mean"] for row in rows),
                "mean_outlier_channels": statistics.mean(
                    row[label]["outlier_channels"] for row in rows
                ),
            }
        output["samples"].append({"spp": spp, "summary": summary,
                                  "candidate_worse_seeds": sum(
                                      row["candidate"]["mse"] > row["baseline"]["mse"]
                                      for row in rows), "per_seed": rows})
    result = json.dumps(output, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding="utf-8", newline="\n")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
