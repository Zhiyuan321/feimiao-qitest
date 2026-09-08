#!/usr/bin/env python3
"""Reproduce a small, provenance-checked OpenMS BSA regression fixture.

This is a test-data converter, NOT a general mzML importer. No network requests,
peak filtering, invented scans, intensity normalization, or time resampling.
The original file stays outside source delivery; only the bounded excerpt ships
with tests. Python float64/math.fsum is the independent numeric reference.
"""
import argparse
import base64
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

COMMIT = "25b3b040a4541a6725b01a5749f5aa958fa9449d"
SHA256 = "dc9ed61d595328d4ef2f1de47d21f41b83e2eae7c9145e1d9b88e910c8cec2f7"
SOURCE = f"https://raw.githubusercontent.com/OpenMS/OpenMS/{COMMIT}/share/OpenMS/examples/BSA/BSA1.mzML"
NS = {"m": "http://psi.hupo.org/ms/mzml"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scans", type=int, default=64)
    args = parser.parse_args()
    if not 2 <= args.scans <= 5000 or args.source.stat().st_size > 20_000_000:
        raise ValueError("Fixture size exceeds the explicit limit")
    raw = args.source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise ValueError("Source hash changed; review provenance before updating")
    root = ET.fromstring(raw)
    scans = []
    for spectrum in root.findall(".//m:spectrum", NS)[:args.scans]:
        cv = {p.attrib["accession"]: p.attrib for p in spectrum.findall(".//m:cvParam", NS)}
        level = int(cv["MS:1000511"]["value"])
        time = cv["MS:1000016"]
        seconds = float(time["value"])
        if time["unitAccession"] == "UO:0000031":
            seconds *= 60.0
        elif time["unitAccession"] != "UO:0000010":
            raise ValueError("Unsupported time unit")
        arrays = {}
        for array in spectrum.findall(".//m:binaryDataArray", NS):
            codes = {p.attrib["accession"] for p in array.findall("m:cvParam", NS)}
            if "MS:1000576" not in codes:
                raise ValueError("Fixture requires uncompressed binary arrays")
            fmt = "d" if "MS:1000523" in codes else "f" if "MS:1000521" in codes else None
            if fmt is None:
                raise ValueError("Unsupported numeric encoding")
            payload = base64.b64decode("".join(array.findtext("m:binary", "", NS).split()), validate=True)
            key = "mz" if "MS:1000514" in codes else "intensity" if "MS:1000515" in codes else None
            if key is None or key in arrays:
                raise ValueError("Unexpected or duplicate binary array")
            arrays[key] = struct.unpack("<" + fmt * (len(payload) // struct.calcsize(fmt)), payload)
        if len(arrays["mz"]) != len(arrays["intensity"]) or len(arrays["mz"]) != int(spectrum.attrib["defaultArrayLength"]):
            raise ValueError("Inconsistent array lengths")
        points = list(zip(arrays["mz"], arrays["intensity"]))
        if not points or level not in (1, 2) or not math.isfinite(seconds) or seconds < 0:
            raise ValueError("Invalid scan")
        if scans and seconds <= scans[-1]["time_s"]:
            raise ValueError("Non-increasing scan time; no silent sorting")
        previous_mz = 0.0
        for mz, intensity in points:
            if not math.isfinite(mz) or not math.isfinite(intensity) or mz <= previous_mz or intensity < 0:
                raise ValueError("Invalid peak; no silent filtering")
            previous_mz = mz
        scans.append({"time_s": seconds, "ms_level": level, "id": spectrum.attrib["id"], "points": points,
                      "source_header_tic": float(cv["MS:1000285"]["value"])})
    target = max(scans[0]["points"], key=lambda p: p[1])[0]
    expected = []
    for scan in scans:
        if scan["ms_level"] != 1:
            continue
        values = [p[1] for p in scan["points"]]
        expected.append({"time_s": scan["time_s"], "tic": math.fsum(values), "bpc": max(values),
                         "eic": math.fsum(y for x, y in scan["points"] if abs(x - target) <= 0.5)})
    integrals = {}
    for kind in ("tic", "bpc", "eic"):
        area = math.fsum((b["time_s"] - a["time_s"]) * (a[kind] + b[kind]) / 2 for a, b in zip(expected, expected[1:]))
        baseline = (expected[-1]["time_s"] - expected[0]["time_s"]) * (expected[0][kind] + expected[-1][kind]) / 2
        integrals[kind] = {"raw": area, "endpoint_baseline": area - baseline}
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / "openms_bsa.scan.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["time_s", "ms_level", "mz", "intensity"])
        for scan in scans:
            writer.writerows((scan["time_s"], scan["ms_level"], mz, y) for mz, y in scan["points"])
    manifest = {"source": SOURCE, "source_sha256": SHA256, "scan_count": len(scans),
                "point_count": sum(len(s["points"]) for s in scans), "selection": "first N scans, unmodified arrays",
                "first_scan_id": scans[0]["id"], "last_scan_id": scans[-1]["id"],
                "csv_sha256": hashlib.sha256((args.output / "openms_bsa.scan.csv").read_bytes()).hexdigest(),
                "target_mz": target, "tolerance_da": 0.5, "ms_level": 1,
                "source_header_tic_first": scans[0]["source_header_tic"],
                "note": "Recomputed from stored centroid arrays. Source header TIC differs; do not treat it as the same observable. No concentration ground truth.",
                "trace": expected, "integrals": integrals}
    (args.output / "expected.json").write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({k: manifest[k] for k in ("scan_count", "point_count", "csv_sha256", "integrals")}, indent=2))


if __name__ == "__main__":
    main()
