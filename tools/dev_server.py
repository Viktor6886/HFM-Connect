#!/usr/bin/env python3
"""Development server for the WebUI: serves webui/ and a MOCK of the device
REST API with synthetic data, so the interface can be checked in a desktop
browser without an ESP32. It is NOT the firmware and proves nothing about
ECU communication.

    python tools/dev_server.py [port]     -> http://localhost:8080/
"""
import json
import math
import pathlib
import random
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_webui  # noqa: E402

T0 = time.time()
STATE = {
    "session": "disconnected", "live": False, "tx": 0, "job": {"id": 0, "job": "none", "state": "idle", "message": ""},
    "ident": False, "dtc": False, "adapt": False, "coding": False, "rec": False, "rows": 0,
    "settings": {"source": 1, "sim_scenario": 0, "experimental_reads": False, "engineer_mode": False, "keep_alive": True,
                 "impulse_enabled": False, "trace_capture": True, "battery_manual_confirm": False, "vehicle_idx": 3,
                 "production_ym": 199405, "vin": "", "mask_vin": True, "ap_ssid": "HFM-Connect-1A2B", "ap_default_pass": True,
                 "sta_ssid": "", "imp_press_ms": 3000, "imp_end_gap_ms": 2500, "vbat_adc": False,
                 "hw": {"kline_tx": 17, "kline_rx": 18, "write_en": 10, "vbat_adc": -1}},
    "trace": [], "seq": 0, "plan": None, "last_result": "",
}
PARAMS = [
    ("rpm", "Обороты двигателя", "об/мин", 0, "20:B7", "25 × raw", "REK", 0, 7000, 0, False),
    ("ect", "Температура ОЖ", "°C", 0, "21:BF", "14-точечная интерполяция", "REK", -40, 130, 1, False),
    ("iat", "Температура всасываемого воздуха", "°C", 0, "21:BE", "8-точечная", "REK", -40, 100, 1, False),
    ("maf", "Расход воздуха (HFM)", "кг/ч", 0, "20:48/49", "(lo+256·hi)/10", "REK", 0, 600, 1, False),
    ("throttle", "Угол дроссельной заслонки", "°", 3, "20:BD", "raw×90/256", "REK", 0, 90, 2, False),
    ("load", "Нагрузка двигателя", "%", 0, "20:C2", "raw/2", "REK", 0, 100, 1, False),
    ("ign_deg", "Угол опережения зажигания", "°ПКВ", 2, "20:C4", "55 − raw×90/256", "REK", -35, 55, 1, False),
    ("o2_mv", "Лямбда-зонд (верхний)", "мВ", 1, "20:23", "4×raw", "REK", -200, 1100, 0, False),
    ("lambda", "Лямбда-регулирование", "%", 1, "20:B9", "390625·raw//1e6 − 50", "GIP", -50, 50, 0, False),
    ("inj_ms", "Время впрыска", "мс", 1, "20:54/55", "((lo+256·hi)//50)/10", "GIP", 0, 20, 1, False),
    ("ac", "Компрессор кондиционера", "", 4, "20:99&01", "бит", "GIP", 0, 1, 0, True),
    ("warmup", "Прогрев", "", 4, "21:69&04", "бит", "GIP", 0, 1, 0, True),
]
NORMS = {"rpm": (600, 800), "maf": (16, 18), "inj_ms": (2.5, 3.8), "ign_deg": (6, 10), "throttle": (0.3, 1.0), "lambda": (-10, 10)}


def tr(phase, tx, rx, par="N", st="OK"):
    STATE["seq"] += 1
    STATE["tx"] += 1
    STATE["trace"].append({"seq": STATE["seq"], "t_us": int((time.time() - T0) * 1e6), "phase": phase, "par": par,
                           "tx": tx, "echo": tx, "rx": rx, "n": 2, "st": st, "risk": "read"})
    STATE["trace"] = STATE["trace"][-1500:]


def live_values():
    t = time.time() - T0
    run = STATE["settings"]["sim_scenario"] != 1
    w = math.sin(t * 2.1) + .5 * math.sin(t * 5.3)
    vals = {"rpm": 750 + 15 * w if run else 0, "ect": 83.2, "iat": 31.0, "maf": 17 + .6 * w if run else 0,
            "throttle": 1.2 + .1 * w, "load": 20 + w, "ign_deg": 10.7 + w, "o2_mv": 450 + 380 * math.sin(t * 3),
            "lambda": 4 * math.sin(t * 3) if run else None, "inj_ms": 3.2 if run else None, "ac": 0, "warmup": 0}
    out = []
    for k, name, unit, cat, src, f, conf, mn, mx, dec, b in PARAMS:
        v = vals.get(k)
        item = {"k": k, "v": None if v is None else round(v, dec), "q": "VALID" if v is not None else "NO_DATA", "norm": "NO_NORM"}
        if k in NORMS:
            lo, hi = NORMS[k]
            item.update(lo=lo, hi=hi, nnote="ХХ, ОЖ >80 °C", nsrc="I07 p.10")
            item["norm"] = "NOT_APPLICABLE" if v is None or not run else ("IN_SPEC" if lo <= v <= hi else ("BELOW" if v < lo else "ABOVE"))
        out.append(item)
    return out


def defs():
    return [{"k": k, "name": n, "unit": u, "cat": c, "src": s, "formula": f, "conf": cf, "ev": "AV", "min": mn, "max": mx,
             "dec": d, "bool": b, "v": None, "q": "NOT_READ", "norm": "NO_NORM"} for k, n, u, c, s, f, cf, mn, mx, d, b in PARAMS]


def status():
    s = STATE["settings"]
    return {"fw": "0.1.0-mock", "uptime_s": int(time.time() - T0), "session_id": "SMOCK0001",
            "source": "simulator" if s["source"] == 1 else "kline", "transport": "sim" if s["source"] == 1 else "kline-uart",
            "session": STATE["session"], "last_error": "OK", "connected_s": int(time.time() - T0), "tx": STATE["tx"],
            "errors": 0, "parity_errors": 0, "frame_errors": 0, "interlock": False, "battery_v": None,
            "ecu": "Bosch HFM-SFI 013 545 13 32, diag 41/00 (эталон HFMreader)" if STATE["ident"] else "ЭБУ не идентифицирован",
            "ecu_resolved": STATE["ident"], "ecu_present": STATE["ident"], "live_active": STATE["live"], "recording": STATE["rec"],
            "record_rows": STATE["rows"], "guard": "IDLE", "trace_last": STATE["seq"], "job": STATE["job"],
            "net": {"ap_ssid": "HFM-Connect-1A2B", "ap_ip": "192.168.4.1"}, "heap": 182000,
            "vehicle": {"idx": 3, "chassis": "W124", "model": "124.032/052/066/092", "name": "E320", "engine": "104.992",
                        "norms": "M104_HFM34_CODED", "connector": "W124_X11_4_16"}}


CAPS = ["Идентификация ЭБУ (F0 00–1B)", "Чтение памяти неисправностей (F1 00–1F)", "Live Data — нативные группы (full8)",
        "Стирание памяти неисправностей", "Запись 9-значного кода комплектации"]


def ecu():
    raw = "2C 42 4D 32 32 38 33 32 30 56 52 30 30 31 33 35 34 35 31 33 33 32 35 92 46 92 41 00"
    return {"ident": {"present": STATE["ident"], "raw": raw if STATE["ident"] else "", "vendor": "BOSCH", "vendor_known": True,
                      "identifier": "M2 28320 VR0", "part": "013 545 13 32", "part_valid": True, "hw_date": "35/92",
                      "sw_date": "46/92", "diag_index": "41/00", "diag_index_bcd": 41, "fingerprint": "0135451332/4100"},
            "profile": {"id": "BOSCH_0135451332_41", "name": "Bosch HFM-SFI 013 545 13 32, diag 41/00 (эталон HFMreader)",
                        "resolved": True, "reference_ecu": True, "bank_mapping": "A_STORED_B_CURRENT", "bank_mapping_conf": "REK",
                        "caps": [{"id": str(i), "name": n, "allowed": i < 3, "conf": ["REK", "REK", "REK", "REK", "BLOCK"][i],
                                  "write": i >= 3, "reason": "" if i < 3 else "Требуется «Режим инженера»"} for i, n in enumerate(CAPS)]},
            "adapt": {"valid": STATE["adapt"], "age_ms": 1000, "raw": {"idle": "83", "lower": "85", "upper": "7E"}, "vdo": False,
                      "idle_kg_h": 0.6, "lower": 1.04, "upper": 0.99},
            "coding": {"valid": STATE["coding"], "coding_byte": "11", "mask_byte": "00", "automatic": True, "gears": 5,
                       "catalyst": True, "usa": False, "efp": False, "diag_module": False, "throttle_taught": True, "screen": 103},
            "vin": {"valid": False}}


def dtc():
    items = [{"bank": "stored", "index": "00", "bit": 6, "pos": 7, "base": 10, "code": 10, "remapped": False,
              "text": "Датчик массового расхода воздуха: обрыв цепи.", "source": "oem", "intermittent": False},
             {"bank": "stored", "index": "02", "bit": 3, "pos": 20, "base": 24, "code": 24, "remapped": False,
              "text": "Верхний (передний) лямбда-зонд: холодный или обрыв цепи.", "source": "oem", "intermittent": False},
             {"bank": "current", "index": "10", "bit": 6, "pos": 7, "base": 10, "code": 10, "remapped": False,
              "text": "Датчик массового расхода воздуха: обрыв цепи.", "source": "oem", "intermittent": False}]
    return {"dtc": {"valid": STATE["dtc"], "bank_mapping": "A_STORED_B_CURRENT", "bank_mapping_conf": "REK", "age_ms": 500,
                    "bank_a": "40 00 08 00 00 00 00 00 00 00 00 00 00 00 00 00", "bank_b": "40 " + "00 " * 14 + "00",
                    "ext_valid": False, "items": items if STATE["settings"]["sim_scenario"] == 3 else []}}


def services():
    return {"services": {"interlock": False, "engineer_mode": STATE["settings"]["engineer_mode"], "battery_manual_confirm": False,
                         "last_result": STATE["last_result"], "purge_on": False,
                         "ops": [{"id": "dtc_clear", "name": "Стирание памяти неисправностей", "allowed": False, "conf": "REK",
                                  "reason": "Рецепт РЕК (HFMreader) для эталонного ЭБУ: требуется «Режим инженера»"},
                                 {"id": "adapt_reset", "name": "Сброс самоадаптаций смеси", "allowed": False, "conf": "REK", "reason": "—"},
                                 {"id": "purge_test", "name": "Тест клапана продувки адсорбера Y58/1", "allowed": False, "conf": "REK", "reason": "—"}],
                         "plan": STATE["plan"] or {"op": "none", "ok": False}}}


def run_job(name, q):
    j = STATE["job"]
    j.update(id=j["id"] + 1, job=name, state="done", message="OK")
    if name == "connect":
        tr("BREAK", "00", "00")
        tr("SYNC", "AA", "55", "S")
        for i in range(28):
            tr("PREFIX", "F0", "0F")
            tr("SELECTOR", f"{i:02X}", "00", "S")
        STATE.update(session="ready", ident=True)
        j["message"] = "Bosch HFM-SFI 013 545 13 32, diag 41/00 (эталон HFMreader)"
    elif name == "disconnect":
        STATE.update(session="disconnected", live=False)
    elif name == "read_dtc":
        STATE["dtc"] = True
        j["message"] = "Прочитано 32 байта F1"
    elif name == "read_adapt":
        STATE["adapt"] = True
    elif name == "read_coding":
        STATE["coding"] = True
    elif name == "live_start":
        STATE["live"] = True
    elif name in ("live_stop", "stop"):
        STATE["live"] = False
    elif name == "svc_prepare":
        STATE["plan"] = {"op": q.get("op", ["dtc_clear"])[0], "ok": False, "token": "", "phrase": "CLEAR DTC 0135451332",
                         "expires_in_ms": 0, "block_reason": "Аппаратный ключ WRITE-EN замкнут",
                         "checks": [{"id": "capability", "text": "Операция разрешена профилем ЭБУ", "result": False},
                                    {"id": "interlock", "text": "Аппаратный ключ WRITE-EN замкнут", "result": False},
                                    {"id": "battery", "text": "Напряжение ≥ 11 В", "result": None}]}
        j.update(state="failed", message="Условия не выполнены")
    return j["id"]


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, obj, code=200, ctype="application/json"):
        body = obj if isinstance(obj, bytes) else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        p = u.path
        if p in ("/", "/index.html"):
            return self.send(build_webui.bundle(), ctype="text/html; charset=utf-8")
        if STATE["live"]:
            for g in ("20", "21"):
                tr("PREFIX", g, f"{int(g, 16) ^ 0xFF:02X}")
        routes = {
            "/api/status": status, "/api/ecu": ecu, "/api/dtc": dtc, "/api/services": services, "/api/settings": lambda: STATE["settings"],
            "/api/files": lambda: {"total": 1441792, "used": 24576, "files": [{"path": "/rec/S1_120.csv", "size": 18432}]},
        }
        if p in routes:
            return self.send(routes[p]())
        if p == "/api/live":
            vals = live_values()
            if "defs" in q:
                d = defs()
                for a, b in zip(d, vals):
                    a.update(b)
                vals = d
            return self.send({"live": {"active": STATE["live"], "seq": int(time.time() * 3), "cycles": 1, "age_ms": 120,
                                       "cycle_ms": 118, "warm_idle": True, "warm_running": True, "values": vals,
                                       "groups": [{"name": n, "prefix": "20", "ok": True, "raw": "1E 03 28"} for n in
                                                  ("rpm_low", "rpm_high_context", "mixture_iat", "airflow", "coolant", "air_mass_low", "air_mass_high")]}})
        if p == "/api/trace":
            after = int(q.get("after", ["0"])[0])
            ev = [e for e in STATE["trace"] if e["seq"] > after][:150]
            return self.send({"last": STATE["seq"], "count": len(STATE["trace"]), "events": ev})
        if p == "/api/vehicles":
            veh = [{"idx": i, "chassis": c, "model": m, "name": n, "engine": e, "from": f, "to": t, "gen": g, "gen_name": g,
                    "norms": "M104_HFM34_CODED", "connector": "W124_X11_4_16", "dm": "", "conf": "ZAV", "source": "I07 (mock)"}
                   for i, (c, m, n, e, f, t, g) in enumerate([
                       ("W124", "124.028", "E280", "104.942", "09/1992", "02/1994", "EARLY_AUTO"),
                       ("W124", "124.028", "E280", "104.942", "03/1994", "", "CODED"),
                       ("W124", "124.032", "E320", "104.992", "09/1992", "02/1994", "EARLY_AUTO"),
                       ("W124", "124.032", "E320", "104.992", "03/1994", "", "CODED"),
                       ("W202", "202.022", "C220", "111.961", "03/1994", "", "CODED")])]
            con = [{"id": "W124_X11_4_16", "name": "X11/4, 16-контактный (W124)", "pins": 16, "conf": "ZAV", "source": "I08 p.53",
                    "warning": "Контакт 16 — цепь 15 (зажигание).", "map": [{"pin": 1, "role": "Масса"}, {"pin": 8, "role": "HFM K-Line"},
                                                                          {"pin": 16, "role": "Цепь 15"}]}]
            return self.send({"vehicles": veh, "connectors": con, "norms": []})
        if p == "/api/report":
            r = {"schema": "hfm.report.v1", "generator": "mock", "disclaimer": "MOCK: синтетические данные dev_server.py",
                 "status": status(), "impulse": {"valid": False, "codes": []}}
            r.update(ecu())
            r.update(dtc())
            r["live"] = {"values": [dict(a, **b) for a, b in zip(defs(), live_values())]}
            r.update(services())
            return self.send(r)
        self.send({"ok": False, "error": "not found"}, 404)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n).decode() if n else ""
        q = urllib.parse.parse_qs(body)
        if u.path == "/api/job":
            return self.send({"ok": True, "id": run_job(q.get("job", [""])[0], q)})
        if u.path == "/api/settings":
            for k, v in q.items():
                cur = STATE["settings"].get(k)
                STATE["settings"][k] = (v[0] in ("1", "true")) if isinstance(cur, bool) else (int(v[0]) if isinstance(cur, int) else v[0])
            return self.send({"ok": True})
        if u.path == "/api/record":
            STATE["rec"] = q.get("on", ["0"])[0] == "1"
            return self.send({"ok": True})
        self.send({"ok": True})


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"MOCK dev server: http://localhost:{port}/  (synthetic data, not the firmware)")
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()


if __name__ == "__main__":
    main()
