"""Development mock of the HFM Connect HTTP API for viewing the WebUI.

SYNTHETIC DATA ONLY - not a vehicle capture, not part of the firmware (D-016).
Usage: python tools/dev_server.py [port]   ->  http://127.0.0.1:8080/
"""
import json, math, sys, time, os
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

WEB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "webui")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
T0 = time.time()
JOBS = {}
NEXT = [1]
SET = {"break_ms": 177, "post_break_ms": 214, "byte_timeout_ms": 750, "ftdi_modem": 0, "wake_auto": True,
       "experimental_reads": True, "engineer_mode": False, "auto_connect": True, "keep_alive": True,
       "impulse_enabled": False, "trace_capture": True, "battery_manual_confirm": False, "vehicle_idx": 2,
       "production_ym": 199301, "vin": "***", "mask_vin": True, "ap_ssid": "HFM Connect", "ap_default_pass": True,
       "sta_ssid": "", "imp_press_ms": 3000, "imp_end_gap_ms": 2500, "vbat_adc": False, "ui_theme": "auto",
       "live_sel": "rpm,maf,o2_mv,lambda", "live_cat": "all", "code9": "", "wake_learned": True}
STATE = {"live": False}

PARAMS = [("rpm", "Обороты двигателя", "об/мин", 0, 0, 7000, 0, False, 600, 800),
          ("ect", "Температура ОЖ", "°C", 0, -40, 130, 1, False, None, None),
          ("maf", "Расход воздуха (HFM)", "кг/ч", 0, 0, 600, 1, False, 16, 18),
          ("throttle", "Угол дроссельной заслонки", "°", 3, 0, 90, 2, False, 1.0, 1.8),
          ("o2_mv", "Лямбда-зонд (верхний)", "мВ", 1, -200, 1100, 0, False, -200, 1000),
          ("lambda", "Лямбда-регулирование", "%", 1, -50, 50, 0, False, -10, 10),
          ("inj_ms", "Время впрыска", "мс", 1, 0, 20, 1, False, 2.5, 3.8),
          ("ign_deg", "Угол опережения зажигания", "°ПКВ", 2, -35, 55, 1, False, 6, 10),
          ("idle_contact", "Контакт ХХ (CTP)", "", 5, 0, 1, 0, True, None, None),
          ("resonance", "Резонансная заслонка Y22/6", "", 4, 0, 1, 0, True, None, None)]


def live(defs):
    t = time.time() - T0
    vals = []
    for k, name, unit, cat, mn, mx, dec, b, lo, hi in PARAMS:
        v = {"rpm": 760 + 20 * math.sin(t), "ect": 86.0, "maf": 21.4 + math.sin(t), "throttle": 1.2, "o2_mv": 450 + 300 * math.sin(3 * t),
             "lambda": -14 + 2 * math.sin(t), "inj_ms": 3.1, "ign_deg": 8.5, "idle_contact": 1, "resonance": 0}[k]
        o = {"k": k, "v": round(v, dec), "q": "VALID"}
        if lo is not None:
            o.update({"lo": lo, "hi": hi, "nnote": "ХХ, P/N, ОЖ >80 °C", "nsrc": "I07 p.2",
                      "norm": "BELOW" if v < lo else "ABOVE" if v > hi else "IN_SPEC"})
        else:
            o["norm"] = "NO_NORM"
        if defs:
            o.update({"name": name, "unit": unit, "src": "20:B7", "formula": "f", "conf": "REK", "ev": "AV", "cat": cat,
                      "bool": b, "min": mn, "max": mx, "dec": dec})
        vals.append(o)
    return {"live": {"active": STATE["live"], "seq": int(t * 3), "cycles": 5, "age_ms": 120, "cycle_ms": 310,
                     "link_ready": True, "warm_idle": True, "warm_running": True, "norms_vehicle": True, "plan": "bosch",
                     "groups": [{"name": "rpm_low", "prefix": "20", "ok": True, "raw": "4C"}, {"name": "airflow", "prefix": "20", "ok": True, "raw": "1E 04 1E 50 7E 80"}],
                     "values": vals}}


def status():
    return {"fw": "0.5.0", "uptime_s": int(time.time() - T0) + 3600, "link": "ready", "link_text": "", "recover_attempts": 0,
            "recoveries": 1, "resyncs": 2, "queue": 0, "ecu_part": "013 545 08 32", "ecu_identifier": "M0 22146 ZR0",
            "ecu_diag": "40/00", "ecu_vendor": "BOSCH", "session": "ready", "last_error": "OK", "connected_s": 754,
            "adapter": {"present": True, "chip": "FT232R", "configured": True}, "auto_connect": True, "auto_msg": "",
            "interlock": False, "battery_v": None, "ecu": "Bosch HFM-SFI 013 545 08 32, diag 40/00", "ecu_resolved": True,
            "ecu_present": True, "live_active": STATE["live"], "recording": False, "record_rows": 0, "guard": "IDLE",
            "trace_last": 10, "job": {"id": 1, "job": "none", "state": "idle", "message": ""},
            "vehicle": {"idx": 2, "chassis": "W124", "model": "124.032", "name": "E320", "engine": "104.992", "norms": "M104_HFM34_EARLY", "connector": "W124_X11_4_16"}}


CAPS = [("ident", "Идентификация ЭБУ (F0 00–1B)", True, "REK", "ok"), ("dtc_read", "Чтение памяти неисправностей", True, "REK", "ok"),
        ("live_native", "Live Data — нативные группы", True, "REK", "ok"), ("adapt_read", "Чтение самоадаптаций", True, "REK", "never"),
        ("dtc_clear", "Стирание памяти неисправностей", False, "BLOCK", "never")]


def ecu():
    return {"ident": {"present": True, "raw": "27 42 4D 30", "vendor": "BOSCH", "vendor_known": True, "vendor_code": "27 42", "system_group": 1,
                      "identifier": "M0 22146 ZR0", "part": "013 545 08 32", "part_valid": True, "hw_date": "38/91", "sw_date": "34/92",
                      "diag_index": "40/00", "diag_index_bcd": 40, "fingerprint": "0135450832/4000", "ext_present": False},
            "profile": {"id": "GENERIC_BOSCH_HFM_40", "name": "Bosch HFM-SFI 013 545 08 32, diag 40/00", "resolved": True, "reference_ecu": False,
                        "bank_mapping": "A_STORED_B_CURRENT", "bank_mapping_conf": "GIP", "vdo_candidate": False, "vdo_units": False, "live_plan": "bosch",
                        "caps": [{"id": a, "name": b, "allowed": c, "conf": d, "write": False, "reason": "" if c else "Заблокировано: нет проверенной трассы",
                                  "run": e, "run_age_s": 95} for a, b, c, d, e in CAPS]},
            "adapt": {"valid": True, "age_ms": 30000, "raw": {"idle": "83", "lower": "85", "upper": "7E"}, "vdo": False, "idle_kg_h": 0.6, "lower": 1.04, "upper": 0.98},
            "coding": {"valid": False}, "vin": {"valid": False}}


def dtc():
    return {"dtc": {"valid": True, "bank_mapping": "A_STORED_B_CURRENT", "bank_mapping_conf": "GIP", "bank_mapping_basis": "Предположение профиля",
                    "m111_texts": False, "dtc_map": "B104", "cylinders": 6, "vendor": "B", "diag_raw": "40", "age_ms": 70000,
                    "bank_a": "40 00 08 00 00 00 00 00 00 00 00 00 00 00 00 00", "bank_b": "40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", "ext_valid": False,
                    "items": [{"bank": "stored", "index": "00", "bit": 6, "pos": 7, "base": 7, "code": 10, "remapped": False, "map": "B104", "text": "Датчик расхода воздуха", "source": "oem", "intermittent": False},
                              {"bank": "current", "index": "10", "bit": 6, "pos": 7, "base": 7, "code": 10, "remapped": False, "map": "B104", "text": "Датчик расхода воздуха", "source": "oem", "intermittent": True}]}}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, obj, code=200, ctype="application/json"):
        body = obj if isinstance(obj, (bytes, bytearray)) else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        p = u.path
        if p in ("/", "/index.html"):
            return self.send(open(os.path.join(WEB, "index.html"), "rb").read(), ctype="text/html; charset=utf-8")
        if p in ("/app.js", "/app.css"):
            return self.send(open(os.path.join(WEB, p[1:]), "rb").read(), ctype="text/javascript" if p.endswith("js") else "text/css")
        api = {"/api/status": status, "/api/ecu": ecu, "/api/dtc": dtc, "/api/settings": lambda: SET,
               "/api/services": lambda: {"services": {"interlock": False, "ops": [{"id": "purge_test", "name": "Тест Y58/1", "allowed": False, "conf": "BLOCK", "reason": "Заблокировано"}], "last_result": "", "purge_on": False, "battery_manual_confirm": False, "plan": {}}},
               "/api/vehicles": lambda: {"vehicles": [{"idx": 2, "chassis": "W124", "model": "124.032", "name": "E320", "engine": "104.992", "from": "09/1992", "to": "02/1994", "gen": "EARLY_AUTO", "gen_name": "HFM 3.4 ранний", "norms": "M104_HFM34_EARLY", "connector": "W124_X11_4_16", "dm": "", "conf": "ZAV", "source": "I07"}], "connectors": [], "norms": []},
               "/api/trace": lambda: {"last": 3, "count": 3, "events": [] if q.get("after", ["0"])[0] != "0" else [{"seq": i, "t_us": 1000 * i, "phase": "SELECTOR", "par": "S", "tx": "B7", "echo": "B7", "rx": "1E", "n": 2, "st": "OK"} for i in range(1, 4)]},
               "/api/files": lambda: {"total": 1000000, "used": 2000, "files": []}}
        if p == "/api/live":
            return self.send(live("defs" in q))
        if p == "/api/job":
            j = JOBS.get(int(q.get("id", ["0"])[0]))
            return self.send(j) if j else self.send({"ok": False, "error": "unknown job"}, 404)
        if p == "/api/report":
            r = {"schema": "hfm.report.v1", "generator": "HFM Connect 0.5.0", "disclaimer": "Программные испытания", "status": status()}
            r.update(ecu()); r.update(dtc()); r.update(live(True)); r["impulse"] = {"codes": []}
            return self.send(r)
        if p in api:
            return self.send(api[p]())
        self.send({"ok": False, "error": "not found"}, 404)

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(n).decode() if n else ""
        f = {k: v[0] for k, v in parse_qs(body).items()}
        if u.path == "/api/job":
            i = NEXT[0]; NEXT[0] += 1
            if f.get("job") == "live_start": STATE["live"] = True
            if f.get("job") == "live_stop": STATE["live"] = False
            JOBS[i] = {"id": i, "job": f.get("job"), "state": "done", "message": "Выполнено: " + f.get("job", "")}
            return self.send({"ok": True, "id": i})
        if u.path == "/api/settings":
            for k, v in f.items():
                SET[k] = v if k in ("ui_theme", "live_sel", "live_cat", "code9", "sta_ssid") else SET.get(k)
            return self.send({"ok": True})
        self.send({"ok": True})


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
