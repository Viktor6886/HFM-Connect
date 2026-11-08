#!/usr/bin/env python3
"""Cross-check tables embedded in the firmware against the protocol data files.

  * HfmDecode.cpp kCoolantTable / kIatTable / kBitV1Old  vs data/protocol/lookup_tables.json
  * HfmDtc.cpp kBase and remap tables                 vs data/protocol/variant_remaps.json
  * DtcCatalog.h                                      vs regenerated output of gen_dtc_catalog.py

Exit code 1 on any mismatch (used by CI).
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "HFMConnect"
DATA = ROOT / "data" / "protocol"


def c_array(src: str, name: str):
    m = re.search(name + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit(f"array {name} not found")
    return [int(x, 0) for x in re.findall(r"-?0x[0-9A-Fa-f]+|-?\d+", m.group(1))]


def pairs(src: str, name: str):
    m = re.search(name + r"\[\]\s*=\s*\{(.*?)\};", src, re.S)
    return {int(a): int(b) for a, b in re.findall(r"\{(\d+),\s*(\d+)\}", m.group(1))}


def main() -> int:
    bad = 0
    lt = json.loads((DATA / "lookup_tables.json").read_text(encoding="utf-8"))["tables"]
    dec = (FW / "HfmDecode.cpp").read_text(encoding="utf-8")
    for cname, jname in (("kCoolantTable", "coolant"), ("kIatTable", "intake_air")):
        a = c_array(dec, cname)
        if a != lt[jname]:
            print(f"MISMATCH {cname} vs {jname}")
            bad += 1
    bv = c_array(dec, "kBitV1Old")
    if bv + [0] * (32 - len(bv)) != lt["bit_v1_old"]:
        print("MISMATCH kBitV1Old")
        bad += 1

    vr = json.loads((DATA / "variant_remaps.json").read_text(encoding="utf-8"))
    dsrc = (FW / "HfmDtc.cpp").read_text(encoding="utf-8")
    if c_array(dsrc, "kBase") != vr["base_numbers"]:
        print("MISMATCH kBase")
        bad += 1
    for cname, jname in (("kGroupO", "_GROUP_O_REMAP"), ("kDiag49BoschH", "_DIAG49_BOSCH_H_REMAP"),
                         ("kDiag49Plus", "_DIAG49_PLUS_REMAP"), ("kDiag50to55", "_DIAG50_55_REMAP")):
        want = {int(k): v for k, v in vr["remaps"][jname]["map"].items()}
        if pairs(dsrc, cname) != want:
            print(f"MISMATCH {cname}")
            bad += 1

    cat = FW / "DtcCatalog.h"
    before = cat.read_bytes()
    subprocess.run([sys.executable, str(ROOT / "tools" / "gen_dtc_catalog.py")], check=True, capture_output=True)
    if cat.read_bytes() != before:
        print("DtcCatalog.h was stale (regenerated)")
        bad += 1
    print("tables OK" if not bad else f"{bad} mismatch(es)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
