#!/usr/bin/env python3
"""Import the HFM-SFI reference package (DTC / Live Data catalogue) into the project.

    python tools/import_reference.py <path>/02_HFM_Connect_DTC_LiveData_catalog.json

Writes data/reference/hfm_reference.json (normalised, only the fields the firmware
and WebUI use) and updates the RU texts / statuses of data/protocol/dtc.csv for the
codes the package defines more precisely. Run tools/gen_dtc_catalog.py and
tools/build_webui.py afterwards.

The package is a compiled catalogue (REV 1.1-1.2 tables, factory manuals, factory
tester context rules). Its evidence levels are kept per entry; nothing here is
treated as verified on a vehicle (ecu_verified is false for every code).
"""
import csv
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "reference" / "hfm_reference.json"
CSV = ROOT / "data" / "protocol" / "dtc.csv"

# Package live-data entries -> project parameter keys (firmware live::ParamDef.key).
LIVE_KEY = {
    "engine_speed": "rpm", "hht_HFM_R006": "rpm", "hht_HFM_R031": "rpm", "hht_HFM_R168": "rpm",
    "coolant_temperature": "ect", "hht_HFM_R001": "ect", "hht_HFM_R045": "ect", "hht_HFM_R062": "ect",
    "o2_sensor_voltage": "o2_alt", "hht_HFM_R008": "o2_mv", "hht_HFM_R044": "o2_mv",
    "throttle_angle": "throttle", "hht_HFM_R005": "throttle", "hht_HFM_R039": "throttle",
    "engine_load": "load", "ignition_angle": "ign_deg", "hht_HFM_R012": "ign_deg",
    "air_mass": "maf", "hht_HFM_R007": "maf", "intake_air_temperature": "iat", "hht_HFM_R013": "iat",
    "injection_time": "inj_ms", "lambda_control": "lambda",
    "adapt_idle": "adapt_idle", "adapt_lower": "adapt_lower", "adapt_upper": "adapt_upper",
    "hht_HFM_R002": "idle_target", "hht_HFM_R028": "idle_target",
    "hht_HFM_R015": "starter", "hht_HFM_R016": "poststart", "hht_HFM_R017": "warmup",
    "hht_HFM_R018": "preheat", "hht_HFM_R019": "air_pump", "hht_HFM_R021": "ac",
    "hht_HFM_R022": "idle_contact", "hht_HFM_R023": "full_load", "hht_HFM_R024": "camshaft",
    "hht_HFM_R025": "resonance", "hht_HFM_R026": "decel_cut",
    "hht_HFM_R043": "speed", "hht_HFM_R172": "speed", "hht_HFM_R046": "o2_rear",
}

# Factory norm rows -> project norm group (prof::normGroupCode) and parameter key.
def norm_group(profile: str) -> str:
    if profile.startswith("M104.995"):
        return "M104_995_HFM41"
    if profile.startswith("M111.961"):
        return "M111_961"
    if "до 02/1994" in profile:
        return "M104_HFM34_EARLY"
    if "с 03/1994" in profile:
        return "M104_HFM34_CODED"
    return ""


NORM_KEY = {
    "RPM ХХ": "rpm", "Длительность впрыска": "inj_ms", "Масса воздуха": "maf",
    "Лямбда-коррекция on-off": "lambda", "Адаптация ХХ Bosch": "adapt_idle", "Адаптация ХХ VDO": "adapt_idle_vdo",
    "Фактор частичной нагрузки": "adapt_mult", "Угол зажигания": "ign_deg", "Угол заслонки": "throttle",
    "Температура масла": "oil_temp", "Напряжение ДМРВ": "maf_voltage", "Коэффициент продувки адсорбера": "purge_duty",
}

# D-017: repository texts do not name third-party programs as the basis of the software.
NEUTRAL = [("HFMScan", "ПК-тестер"), ("HFMReader", "ПК-программа"), ("HHTwin", "заводской тестер HHT"),
           ("HHT-WIN", "HHT")]


def neutral(s):
    if isinstance(s, str):
        for a, b in NEUTRAL:
            s = s.replace(a, b)
        return s.strip()
    if isinstance(s, list):
        return [neutral(x) for x in s]
    if isinstance(s, dict):
        return {k: neutral(v) for k, v in s.items()}
    return s


STATUS_LEVEL = {  # package definition_status -> project evidence level
    "DOCUMENTED_CATALOG_TEXT": "REK",
    "FACTORY_DOCUMENTED_GENERIC_COMPONENT": "ZAV",
    "FACTORY_DOCUMENTED_MEANING": "ZAV",
    "HHT_CONTEXT_RECONSTRUCTED_NOT_ECU_VERIFIED": "GIP",
}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = pathlib.Path(sys.argv[1])
    raw = src.read_bytes()
    d = json.loads(raw.decode("utf-8"))
    sha = hashlib.sha256(raw).hexdigest()
    special = d.get("dtc_verification_summary", {}).get("special_warnings", {})

    def warn_for(code: int) -> str:
        out = []
        for k, v in special.items():
            if "-" in k:
                a, b = (int(x) for x in k.split("-"))
                if a <= code <= b:
                    out.append(v)
            elif int(k) == code:
                out.append(v)
        return " ".join(out)

    dtc = {}
    rules_used = set()
    for x in d["dtc_hfm"]:
        code = int(x["code"])
        app = x["applicability_and_sources"]
        m = re.search(r"HHT-правила:\s*([0-9,\s]+)", app)
        rules = [int(r) for r in re.findall(r"\d+", m.group(1))] if m else []
        rules += [int(r) for r in re.findall(r"HHT #(\d+)", app) if int(r) not in rules]
        rules_used.update(rules)
        dtc[str(code)] = {
            "t": x["title_ru"].rstrip("."),
            "c": x["possible_causes"],
            "k": x["checks"],
            "o": "" if x["related_live"] in ("—", "-") else x["related_live"],
            "cat": x["category"],
            "app": app,
            "ev": x["definition_evidence"],
            "st": x["definition_status"],
            "lvl": STATUS_LEVEL.get(x["definition_status"], "GIP"),
            "u": bool(x["uncertain"]),
            "gate": bool(x["requires_variant_gate"]),
            "w": warn_for(code),
            "r": rules,
        }
    rules = {}
    for r in d["dtc_hht_context_rules"]:
        n = int(r["rule_no"])
        if n in rules_used:
            rules[str(n)] = {"ds": r["ds"], "mb": r["mb_range"], "ab": r["range_ab"], "comp": r["component"],
                             "en": r["text_en"]}
    live = []
    for x in d["live_data"]:
        live.append({
            "key": LIVE_KEY.get(x["key"], ""), "id": x["key"], "name": x["name_en"], "route": x["route"],
            "cmd": x["cmd"], "sel": x["selector_recipe"], "f": x["formula"], "unit": x["unit"],
            "cond": x["conditions"], "status": x["source_status"], "ref": x["reference"], "val": x["validation"],
            "mean": x["meaning_ru"], "exp": x["expected_behavior"], "bad": x["abnormal_value_causes"],
            "conflict": x["formula_conflict_warning"],
        })
    missing = sorted({x["id"] for x in live if not x["key"]})
    if missing:
        print("live entries without a project key:", missing)
    norms = []
    for n in d["factory_norms"]:
        norms.append({"group": norm_group(n["ecu_profile"]), "profile": n["ecu_profile"], "hfm": n["hfm_version"],
                      "key": NORM_KEY.get(n["parameter"], ""), "param": n["parameter"], "range": n["range_text"],
                      "unit": n["unit"], "cond": n["conditions"], "src": n["source"]})
    out = {
        "meta": {"title": "Справочник HFM-SFI M104/M111 (DTC, Live Data, сценарии)", "package": d["version"],
                 "package_sha256": sha, "warning": d["warning"],
                 "notices": [f"{n['topic']}: {n['finding']}" for n in d.get("source_verification_notices", [])]},
        "dtc": dtc,
        "hht_rules": rules,
        "live": live,
        "norms": norms,
        "scenarios": [{"id": s["id"], "symptom": s["symptom"], "inputs": s["inputs"], "actions": s["ui_actions"]}
                      for s in d["guided_diagnostics"]],
        "quality": d["data_quality_rules"],
        "analog": [{"code": a["code"], "t": a["name_ru"], "k": a["check"], "src": a["source"]} for a in d["dtc_analog"]],
        "dm": [{"code": a["code"], "t": a["title"], "c": a["possible_causes"], "k": a["checks"], "src": a["source"]}
               for a in d["dtc_dm"]],
        "cc": [{"code": a["code"], "t": a["description"], "c": a["causes"], "k": a["checks"], "src": a["source_page"]}
               for a in d["cc_isc_dtc"]],
        "transport": [{"id": a["id"], "t": a["message"], "c": a["causes"], "k": a["checks"]}
                      for a in d["transport_errors"]],
        "sources": [{"id": r["id"], "name": r["name"], "scope": r["scope"], "ev": r["evidence_class"]}
                    for r in d["references"]],
    }
    # A few rows of the package carry page-footer fragments of the source PDF.
    for s in out["scenarios"]:
        s["inputs"] = re.sub(r"\s*HFM-SFI / Mercedes-Benz.*$", "", s["inputs"])
        s["actions"] = re.sub(r"\s*HFM-SFI / Mercedes-Benz.*$", "", s["actions"])
    out = neutral(out)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(out, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(dtc)} DTC, {len(rules)} HHT rules, {len(live)} live, "
          f"{len(norms)} norms, {len(out['scenarios'])} scenarios")

    # dtc.csv: precise texts for codes that only had a placeholder / generic meaning.
    rows = list(csv.DictReader(CSV.open(encoding="utf-8-sig")))
    fields = list(rows[0].keys())
    changed = 0
    for r in rows:
        x = dtc.get(str(int(r["code"])))
        if not x:
            continue
        if x["st"] == "FACTORY_DOCUMENTED_MEANING" and r["catalog_status"] == "reconciliation_required":
            r["description_ru"] = x["t"] + "."
            r["catalog_status"] = "oem_supported"
            changed += 1
        elif x["st"] == "HHT_CONTEXT_RECONSTRUCTED_NOT_ECU_VERIFIED" and r["catalog_status"] == "unverified":
            r["description_ru"] = x["t"] + "."
            r["catalog_status"] = "hht_candidate"
            changed += 1
    with CSV.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    print(f"dtc.csv: {changed} texts updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
