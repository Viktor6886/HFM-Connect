"""Development mock of the HFM Connect HTTP API for viewing the WebUI.

SYNTHETIC DATA ONLY - not a vehicle capture, not part of the firmware (D-016).
Usage: python tools/dev_server.py [port]   ->  http://127.0.0.1:8080/
"""
import json, math, sys, time, os
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

WEB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "webui")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_webui  # noqa: E402  (reference catalogue exactly as embedded in the firmware)
REF = build_webui.reference()
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
T0 = time.time()
JOBS = {}
NEXT = [1]
SET = {"break_ms": 177, "post_break_ms": 214, "byte_timeout_ms": 750, "ftdi_modem": 0, "wake_auto": True,
       "experimental_reads": True, "engineer_mode": False, "auto_connect": True, "keep_alive": True,
       "impulse_enabled": False, "trace_capture": True, "battery_manual_confirm": False, "vehicle_idx": 2,
       "production_ym": 199301, "vin": "***", "mask_vin": True, "ap_ssid": "HFM Connect", "ap_default_pass": True,
       "sta_ssid": "", "imp_press_ms": 3000, "imp_end_gap_ms": 2500, "imp_clear_ms": 7000, "require_write_key": False,
       "vbat_adc": False, "ui_theme": "auto",
       "live_sel": "rpm,maf,o2_mv,lambda", "live_cat": "all", "code9": "", "wake_learned": True,
       "sound_level": 2, "light_level": 2, "dtc_on_connect": True, "buzzer_fitted": True, "led_fitted": True,
       "oled": {"built": True, "on": True, "type": 0, "flip": False, "level": 2, "present": True, "address": "0x3C", "frames": 120}}
STATE = {"live": False, "plan": {}, "result": ""}

PARAMS = [("rpm", "Обороты двигателя", "об/мин", 0, 0, 7000, 0, False, 600, 800),
          ("ect", "Температура ОЖ", "°C", 0, -40, 130, 1, False, None, None),
          ("maf", "Расход воздуха (HFM)", "кг/ч", 0, 0, 600, 1, False, 16, 18),
          ("throttle", "Угол дроссельной заслонки", "°", 3, 0, 90, 2, False, 1.0, 1.8),
          ("o2_mv", "Лямбда-зонд (верхний)", "мВ", 1, -200, 1100, 0, False, -200, 1000),
          ("lambda", "Лямбда-регулирование", "%", 1, -50, 50, 0, False, -10, 10),
          ("inj_ms", "Время впрыска", "мс", 1, 0, 20, 1, False, 2.5, 3.8),
          ("ign_deg", "Угол опережения зажигания", "°ПКВ", 2, -35, 55, 1, False, 6, 10),
          ("speed", "Скорость автомобиля", "км/ч", 5, 0, 250, 0, False, None, None),
          ("idle_contact", "Контакт ХХ (CTP)", "", 5, 0, 1, 0, True, None, None),
          ("resonance", "Резонансная заслонка Y22/6", "", 4, 0, 1, 0, True, None, None)]


def live(defs):
    t = time.time() - T0
    vals = []
    for k, name, unit, cat, mn, mx, dec, b, lo, hi in PARAMS:
        v = {"rpm": 760 + 20 * math.sin(t), "ect": 86.0, "maf": 21.4 + math.sin(t), "throttle": 1.2, "o2_mv": 450 + 300 * math.sin(3 * t),
             "lambda": -9.2 + 0.3 * math.sin(t), "inj_ms": 3.1, "ign_deg": 8.5, "speed": None, "idle_contact": 1, "resonance": 0}[k]
        o = {"k": k, "v": None if v is None else round(v, dec), "q": "NOT_SUPPORTED" if v is None else "VALID"}
        if lo is not None:
            o.update({"lo": lo, "hi": hi, "nnote": "ХХ, P/N, ОЖ >80 °C", "nsrc": "I07 p.2",
                      "norm": "BELOW" if v < lo else "ABOVE" if v > hi else "IN_SPEC"})
        else:
            o["norm"] = "NO_NORM"
        if defs:
            o.update({"name": name, "unit": unit, "src": "20:B7", "formula": "f", "conf": "GIP" if k in ("lambda", "inj_ms", "speed") else "REK", "ev": "AV", "cat": cat,
                      "bool": b, "min": mn, "max": mx, "dec": dec})
        vals.append(o)
    return {"live": {"active": STATE["live"], "seq": int(t * 3), "cycles": 5, "age_ms": 120, "cycle_ms": 310,
                     "link_ready": True, "warm_idle": True, "warm_running": True, "norms_vehicle": True, "plan": "bosch", "plan_echo": False,
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


CAPS = [("ident", "Идентификация ЭБУ", True, "REK", "ok", False, "F0 00–1B"),
        ("dtc_read", "Чтение памяти неисправностей", True, "REK", "ok", False, "F1 00–1F"),
        ("live_native", "Текущие параметры двигателя (Live Data)", True, "GIP", "ok", False, "Bosch 20/21"),
        ("adapt_read", "Чтение самоадаптаций смеси", True, "REK", "unsupported", False, "24:90/92/93"),
        ("coding_read", "Чтение кодировки комплектации", True, "REK", "unsupported", False, "27:02 + 24:F0"),
        ("vin_read", "Чтение VIN", True, "GIP", "unsupported", False, "F9 00–0D"),
        ("dtc_clear", "Стирание памяти неисправностей", True, "REK", "never", True, "10 00 · F3 FF (×2)"),
        ("adapt_reset", "Сброс самоадаптаций смеси", True, "REK", "never", True, "CF FF · C1 FC → E0 00 · CF 00 · C0 FC"),
        ("version_coding_write", "Запись 9-значного кода комплектации", False, "BLOCK", "never", True, "Variantencodierung"),
        ("throttle_learn", "Обучение упора дроссельной заслонки", False, "BLOCK", "never", True, "Variantencodierung")]


def ecu():
    return {"ident": {"present": True, "raw": "27 56 4D 30", "vendor": "VDO", "vendor_known": True, "vendor_code": "27 56", "system_group": 1,
                      "identifier": "M0 22146 ZR0", "part": "013 545 08 32", "part_valid": True, "hw_date": "38/91", "sw_date": "34/92",
                      "diag_index": "40/00", "diag_index_bcd": 40, "fingerprint": "0135450832/4000", "ext_present": False},
            "profile": {"id": "VDO_HFM_40", "name": "VDO HFM 013 545 08 32, diag 40/00", "resolved": True, "reference_ecu": False,
                        "bank_mapping": "A_CURRENT_B_STORED", "bank_mapping_conf": "ZAV",
                        "bank_text": "Банк A (F1 00–0F) — текущие, банк B (F1 10–1F) — сохранённые",
                        "bank_basis": "Определено по последнему чтению: периодические (перемежающиеся) неисправности бывают только в банке сохранённых (заводская документация Engine 104/111, 11/3)",
                        "variant": {"diag_raw": "40", "vendor": "V", "cylinders": 6, "dtc_map": "C8EE"},
                        "vdo_candidate": False, "vdo_units": True, "live_plan": "bosch",
                        "caps": [{"id": a, "name": b, "allowed": c, "conf": d, "write": w, "tech": t,
                                  "reason": ("Доступно: пароль точки доступа, предупреждение и проверка условий перед выполнением" if c else "Недоступно: последовательность байтов команды не восстановлена ни из одного источника"),
                                  "run": e, "run_age_s": 95} for a, b, c, d, e, w, t in CAPS]},
            "adapt": {"valid": False, "unsupported": True},
            "coding": {"valid": False, "unsupported": True}, "vin": {"valid": False, "unsupported": True, "not_stored": False}}


def dtc():
    return {"dtc": {"valid": True, "bank_mapping": "A_CURRENT_B_STORED", "bank_mapping_conf": "ZAV",
                    "bank_text": "Банк A (F1 00–0F) — текущие, банк B (F1 10–1F) — сохранённые",
                    "bank_mapping_basis": "Определено по последнему чтению: периодические (перемежающиеся) неисправности бывают только в банке сохранённых (заводская документация Engine 104/111, 11/3)",
                    "m111_texts": False, "dtc_map": "C8EE", "cylinders": 6, "vendor": "V", "diag_raw": "40", "age_ms": 70000, "auto": True,
                    "bank_a": "40 00 08 00 00 00 00 00 00 00 00 00 00 00 00 00", "bank_b": "40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", "ext_valid": False,
                    "items": [{"bank": "current", "index": "00", "bit": 6, "pos": 7, "base": 10, "code": 10, "remapped": False, "map": "C8EE", "rule": "", "text": "Датчик массового расхода воздуха: обрыв цепи.", "source": "oem", "intermittent": False},
                              {"bank": "stored", "index": "17", "bit": 1, "pos": 58, "base": 75, "code": 75, "remapped": False, "map": "C8EE", "rule": "", "text": "Реле подогрева впуска PSV (K3/1): электрическая неисправность, характер не уточнён.", "source": "candidate", "intermittent": True}]},
            "impulse": {"valid": True, "last_n": 2, "complete": True, "glitches": 0, "edges": 5,
                        "codes": [{"n": 2, "a": [{"code": 2, "text": "Датчик температуры ОЖ"}, {"code": 3, "text": "Датчик температуры ОЖ: обрыв"}]}]},
            "impulse_enabled": bool(SET.get("impulse_enabled"))}


OPS = [("dtc_clear", "Стирание памяти неисправностей"), ("adapt_reset", "Сброс самоадаптаций смеси"),
       ("purge_test", "Проверка клапана продувки адсорбера Y58/1"), ("impulse_clear", "Импульсное стирание кода")]


def services():
    return {"services": {"interlock": False, "require_write_key": False, "pw_lock_s": 0,
                         "ops": [{"id": i, "name": n, "allowed": True, "conf": "REK", "reason": "Доступно"} for i, n in OPS],
                         "last_result": STATE["result"], "purge_on": False, "battery_manual_confirm": False, "plan": STATE["plan"]}}


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
            body = open(os.path.join(WEB, p[1:]), "rb").read().replace(b"__REF_VERSION__", b"dev")
            return self.send(body, ctype="text/javascript" if p.endswith("js") else "text/css")
        if p == "/ref.json":
            return self.send(REF)
        api = {"/api/status": status, "/api/ecu": ecu, "/api/dtc": dtc, "/api/settings": lambda: SET,
               "/api/services": services,
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
            r.update(ecu()); r.update(dtc()); r.update(live(True)); r["impulse"] = {"codes": []}; r["adapt"] = ecu()["adapt"]
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
            ok, msg = True, "Выполнено: " + f.get("job", "")
            if f.get("job") == "svc_prepare":
                STATE["plan"] = {"op": f.get("op"), "ok": True, "token": "0BADF00D", "expires_in_ms": 60000, "block_reason": "",
                                 "checks": [{"id": "capability", "text": "Операция разрешена профилем ЭБУ", "result": True},
                                            {"id": "session", "text": "Связь с ЭБУ по K-Line есть", "result": True},
                                            {"id": "battery", "text": "Напряжение бортсети не ниже 11 В (измерено или подтверждено вами)", "result": True},
                                            {"id": "engine_stopped", "text": "Зажигание включено, двигатель заглушен (по оборотам или подтверждено вами)", "result": f.get("confirm") == "1"}]}
                if f.get("confirm") != "1":
                    STATE["plan"]["ok"] = False
                    STATE["plan"]["block_reason"] = "Зажигание включено, двигатель заглушен (по оборотам или подтверждено вами)"
            if f.get("job") == "svc_execute":
                if f.get("password") != "12345678":
                    ok, msg = False, "Неверный пароль. Нужен пароль Wi-Fi точки доступа HFM Connect"
                else:
                    STATE["plan"] = {}
                    STATE["result"] = "Память неисправностей стёрта: записей было 2, при повторном чтении — 0"
            JOBS[i] = {"id": i, "job": f.get("job"), "state": "done" if ok else "failed", "message": msg}
            return self.send({"ok": True, "id": i})
        if u.path == "/api/settings":
            for k, v in f.items():
                SET[k] = v if k in ("ui_theme", "live_sel", "live_cat", "code9", "sta_ssid") else (int(v) if k in ("sound_level", "light_level") else SET.get(k))
            return self.send({"ok": True})
        self.send({"ok": True})


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
