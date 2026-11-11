#ifdef ARDUINO
#include "WebApi.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Config.h"
#include "DiagEngine.h"
#include "FtdiLink.h"
#include "JsonWriter.h"
#include "NetManager.h"
#include "OledDisplay.h"
#include "Settings.h"
#include "ImpulseDecoder.h"
#include "Indicator.h"
#include "WebUiAssets.h"

static WebServer server(HFM_HTTP_PORT);

// ---- helpers ---------------------------------------------------------------------
static void chunkSink(const std::string& c, void*) { server.sendContent(c.c_str(), c.size()); }

static void beginChunked(const char* type = "application/json") {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, type, "");
}

static void endChunked(JsonWriter& w) {
  w.flush();
  server.sendContent("");
}

static void sendSmall(JsonWriter& w, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", w.str().c_str());
}

static void sendError(int code, const char* msg) {
  JsonWriter w;
  w.beginObject().kv("ok", false).kv("error", msg).endObject();
  sendSmall(w, code);
}

static const char* conf(Confidence c) { return confidenceCode(c); }

static void writeTri(JsonWriter& w, const char* k, int8_t v) {
  w.key(k);
  if (v < 0) w.null(); else w.value(v != 0);
}

// ---- JSON sections -------------------------------------------------------------------
static void writeIdent(JsonWriter& w, const EngineState& s) {
  const EcuIdent& id = s.ident;
  w.key("ident").beginObject();
  w.kv("present", id.present);
  w.key("raw").hexBytes(id.raw, id.rawLen);
  if (id.present) {
    w.kv("vendor", id.vendor).kv("vendor_known", id.vendorKnown).kv("vendor_code", id.vendorCode);
    w.kv("system_group", (unsigned)id.systemGroup);
    w.kv("identifier", id.identifier).kv("part", id.partDisplay).kv("part_valid", id.partValid);
    if (id.hwWeek > 0) { char t[8]; snprintf(t, sizeof t, "%02d/%02d", id.hwWeek, id.hwYear); w.kv("hw_date", t); }
    else w.kvNull("hw_date");
    if (id.swWeek > 0) { char t[8]; snprintf(t, sizeof t, "%02d/%02d", id.swWeek, id.swYear); w.kv("sw_date", t); }
    else w.kvNull("sw_date");
    w.kv("diag_index", id.diagDisplay);
    w.kv("diag_index_bcd", (int)id.diagIndexBcd);
    w.kv("fingerprint", id.fingerprint);
    w.kv("ext_present", id.extPresent);
    if (id.extPresent) w.key("rom_page").hex(id.romPage).key("desc_addr").hex(id.descAddr);
  }
  w.endObject();
  const prof::EcuProfile& p = s.profile;
  w.key("profile").beginObject();
  w.kv("id", p.id).kv("name", p.name).kv("resolved", p.resolved).kv("reference_ecu", p.referenceEcu);
  w.kv("bank_mapping", dtc::bankMappingCode(p.bankMapping)).kv("bank_mapping_conf", conf(p.bankMappingConf));
  const DtcVariantMap* vm = dtc::selectMap(p.variant);
  w.key("variant").beginObject();
  w.key("diag_raw").hex(p.variant.diagRaw);
  w.kv("vendor", p.variant.manufacturer == 1 ? "B" : "V").kv("cylinders", (unsigned)p.variant.cylinders);
  w.kv("dtc_map", vm ? vm->id : "GENERIC").endObject();
  w.kv("vdo_candidate", p.vdoCandidate).kv("vdo_units", p.vdoUnits).kv("live_plan", s.livePlanVdo ? "vdo" : "bosch");
  w.key("caps").beginArray();
  for (uint8_t f = 0; f < prof::F_COUNT; f++) {
    w.beginObject();
    w.kv("id", prof::featureCode(f)).kv("name", prof::featureName(f));
    w.kv("allowed", p.caps[f].allowed).kv("conf", conf(p.caps[f].conf)).kv("write", prof::featureIsWrite(f));
    w.kv("reason", p.caps[f].reason ? p.caps[f].reason : "");
    // What really happened on this ECU (not only the expected support).
    static const char* const kRun[] = {"never", "ok", "failed"};
    w.kv("run", kRun[s.feat[f].state < 3 ? s.feat[f].state : 0]);
    w.kv("run_age_s", s.feat[f].state ? (uint32_t)((millis() - s.feat[f].tMs) / 1000) : 0u);
    w.endObject();
  }
  w.endArray();
  w.endObject();
}

static void writeAdaptCoding(JsonWriter& w, const EngineState& s) {
  w.key("adapt").beginObject();
  w.kv("valid", s.adapt.valid);
  if (s.adapt.valid) {
    w.kv("age_ms", (uint32_t)(millis() - s.adapt.tMs));
    w.key("raw").beginObject().key("idle").hex(s.adapt.rawIdle).key("lower").hex(s.adapt.rawLower).key("upper").hex(s.adapt.rawUpper).endObject();
    w.kv("vdo", s.profile.vdoUnits);
    w.kv("idle_kg_h", (double)s.adapt.idleKgH, 1).kv("lower", (double)s.adapt.lower, 2).kv("upper", (double)s.adapt.upper, 2);
  }
  w.endObject();
  w.key("coding").beginObject();
  w.kv("valid", s.coding.valid);
  if (s.coding.valid) {
    const hfm::CodingInfo& c = s.coding.info;
    w.key("coding_byte").hex(c.coding).key("mask_byte").hex(c.mask);
    writeTri(w, "automatic", c.automatic);
    w.kv("gears", (int)c.gears);
    writeTri(w, "catalyst", c.catalyst);
    writeTri(w, "usa", c.usa);
    writeTri(w, "efp", c.efp);
    writeTri(w, "diag_module", c.diagModule);
    writeTri(w, "throttle_taught", c.throttleTaught);
    w.kv("screen", (int)c.screen);
  }
  w.endObject();
  w.key("vin").beginObject().kv("valid", s.vin.valid);
  if (s.vin.valid) w.kv("text", AppSettings.maskVin ? "*** (скрыт настройкой)" : s.vin.text);
  w.endObject();
}

static bool vehicleIsM111() {
  int16_t vi = AppSettings.vehicleIdx;
  const prof::VehicleEntry* ve = vi >= 0 ? prof::vehicleByIndex((uint16_t)vi) : nullptr;
  return ve && strncmp(ve->engine, "111", 3) == 0;
}

static void writeDtc(JsonWriter& w, const EngineState& s) {
  const DtcView& d = s.dtc;
  // Effective bank orientation: the factory intermittent-fault rule observed
  // in this readout wins over the profile assumption.
  bool byRule = d.inferred != dtc::BankMapping::Unknown;
  // Banks are named current/stored only when proven for this readout (factory
  // intermittent-fault rule) or by the reference profile (REK); a profile
  // assumption (GIP) keeps the neutral names Bank A / Bank B (REV 1.2 E-06, C-01).
  bool proven = byRule || s.profile.bankMappingConf == Confidence::REK;
  dtc::BankMapping eff = byRule ? d.inferred : (proven ? s.profile.bankMapping : dtc::BankMapping::Unknown);
  const char* effConf = byRule ? "ZAV" : (proven ? conf(s.profile.bankMappingConf) : "GIP");
  const char* effBasis =
      byRule ? "Определено по этому чтению: перемежающиеся неисправности бывают только в сохранённых (OEM 11/3)"
             : (proven ? "Профиль эталонного ЭБУ (РЕК)"
                       : "Назначение банков для этого ЭБУ не подтверждено: показаны банки A и B. Определится "
                         "автоматически, если в чтении появится перемежающаяся неисправность");
  bool m111 = vehicleIsM111();
  w.key("dtc").beginObject();
  w.kv("valid", d.valid);
  w.kv("bank_mapping", dtc::bankMappingCode(eff));
  w.kv("bank_mapping_conf", effConf);
  w.kv("bank_mapping_basis", effBasis);
  w.kv("m111_texts", m111);
  const DtcVariantMap* vm = dtc::selectMap(s.profile.variant);
  w.kv("dtc_map", vm ? vm->id : "GENERIC").kv("cylinders", (unsigned)s.profile.variant.cylinders);
  w.kv("vendor", s.profile.variant.manufacturer == 1 ? "B" : "V");
  w.key("diag_raw").hex(s.profile.variant.diagRaw);
  if (d.valid) {
    w.kv("age_ms", (uint32_t)(millis() - d.tMs));
    w.key("bank_a").hexBytes(d.fm.bankA, 16);
    w.key("bank_b").hexBytes(d.fm.bankB, 16);
    w.kv("ext_valid", d.fm.extValid);
    if (d.fm.extValid) w.key("ext").hexBytes(d.fm.ext, 6);
    w.key("items").beginArray();
    for (uint16_t i = 0; i < d.count; i++) {
      const dtc::Decoded& x = d.items[i];
      w.beginObject();
      w.kv("bank", dtc::bankLabel(x.bank, eff));
      w.key("index").hex(x.index);
      w.kv("bit", (unsigned)x.bit).kv("pos", (unsigned)x.position);
      w.kv("base", (unsigned)x.baseCode).kv("code", (unsigned)x.code).kv("remapped", x.remapped);
      w.kv("map", x.mapId ? x.mapId : "GENERIC");
      w.kv("rule", x.mapId ? "" : dtc::remapRule(x.position, s.profile.variant));
      const char* alt = m111 ? dtc::m111Text(x.code) : nullptr;
      if (x.entry) {
        static const char* const srcNames[] = {"oem", "reference", "reconcile", "unverified", "app_label", "candidate"};
        w.kv("text", alt ? alt : x.entry->ru).kv("text_en", x.entry->en);
        w.kv("source", alt ? "oem_m111" : srcNames[x.entry->source < 6 ? x.entry->source : 3]);
        w.kv("intermittent", (x.entry->flags & DTC_F_INTERMITTENT) != 0);
        w.kv("not_w124", (x.entry->flags & DTC_F_NOT_W124) != 0);
      } else {
        w.kv("text", x.code ? "Код вне каталога — RAW (без догадки)"
                            : "Позиция не используется в заводской карте этого варианта ЭБУ — RAW")
            .kv("source", "unknown");
        w.kv("intermittent", false);
      }
      w.endObject();
    }
    w.endArray();
  }
  w.endObject();
}

static void writeImpulse(JsonWriter& w, const EngineState& s) {
  uint8_t mask = AppSettings.vehicleIdx < 0 ? (uint8_t)IMP_BOTH : (vehicleIsM111() ? (uint8_t)IMP_M111 : (uint8_t)IMP_M104);
  w.key("impulse").beginObject().kv("valid", s.impulse.valid);
  if (s.impulse.valid) {
    w.kv("last_n", (unsigned)s.impulse.result.pulses).kv("complete", s.impulse.result.complete);
    w.kv("glitches", (unsigned)s.impulse.result.glitches).kv("edges", (unsigned)s.impulse.edgeCount);
  }
  w.key("codes").beginArray();
  for (uint8_t i = 0; i < s.impulse.codeCount; i++) {
    uint8_t n = s.impulse.codes[i];
    w.beginObject().kv("n", (unsigned)n);
    w.key("a").beginArray();
    if (n == 1) {
      w.beginObject().kv("code", 1u).kv("text", "Неисправностей не обнаружено").endObject();
    } else {
      const ImpulseCodeEntry* e[2];
      uint8_t k = impulseLookup(n, mask, e, 2);
      for (uint8_t j = 0; j < k; j++)
        for (uint8_t c = 0; c < e[j]->count; c++) {
          const DtcCatalogEntry* ce = dtc::lookup(e[j]->a[c]);
          const char* alt = (mask == IMP_M111) ? dtc::m111Text(e[j]->a[c]) : nullptr;
          w.beginObject().kv("code", (unsigned)e[j]->a[c]).kv("text", alt ? alt : (ce ? ce->ru : "—")).endObject();
        }
    }
    w.endArray().endObject();
  }
  w.endArray().endObject();
}

static void writeLive(JsonWriter& w, const EngineState& s, bool withDefs) {
  const live::Decoded& d = s.live;
  w.key("live").beginObject();
  w.kv("active", s.liveActive).kv("seq", d.seq).kv("cycles", s.liveCycles);
  w.kv("age_ms", d.tMs ? (uint32_t)(millis() - d.tMs) : 0xFFFFFFFFu);
  w.kv("cycle_ms", (unsigned)s.snap.cycleMs);
  w.kv("link_ready", s.session == SessionState::Ready);
  w.kv("warm_idle", d.conditionsWarmIdle).kv("warm_running", d.conditionsWarmRunning);
  int16_t vi = AppSettings.vehicleIdx;
  w.kv("norms_vehicle", vi >= 0);
  // Groups of the address plan actually polled (Bosch full8 or VDO).
  w.kv("plan", s.snap.vdoPlan ? "vdo" : "bosch");
  w.key("groups").beginArray();
  if (s.snap.vdoPlan) {
    for (uint8_t g = 0; g < live::V_COUNT; g++) {
      const live::GroupDef& gd = live::vdoGroup(g);
      w.beginObject().kv("name", gd.name).key("prefix").hex(gd.prefix).kv("ok", s.snap.vdoOk[g]);
      w.key("raw").hexBytes(s.snap.vdo[g], gd.n).endObject();
    }
  } else {
    for (uint8_t g = 0; g < live::G_COUNT; g++) {
      const live::GroupDef& gd = live::group(g);
      w.beginObject().kv("name", gd.name).key("prefix").hex(gd.prefix).kv("ok", s.snap.groupOk[g]);
      w.key("raw").hexBytes(s.snap.g[g], gd.n).endObject();
    }
  }
  w.endArray();
  w.key("values").beginArray();
  for (uint8_t p = 0; p < live::P_COUNT; p++) {
    const live::Value& v = d.v[p];
    const live::ParamDef& pd = live::param(p);
    w.beginObject();
    w.kv("k", pd.key);
    if (withDefs) {
      w.kv("name", pd.name).kv("unit", pd.unit).kv("src", pd.source).kv("formula", pd.formula);
      w.kv("conf", conf(pd.conf)).kv("ev", pd.evidence).kv("cat", (int)pd.cat).kv("bool", pd.isBool);
      w.kv("min", (double)pd.chartMin, 1).kv("max", (double)pd.chartMax, 1).kv("dec", (int)pd.decimals);
    }
    if (v.hasValue) w.kv("v", (double)v.v, pd.decimals); else w.kvNull("v");
    w.kv("q", live::qualityCode(v.q));
    w.kv("norm", live::normStateCode(v.norm));
    if (v.normRef) {
      w.kv("lo", (double)v.normLo, 2).kv("hi", (double)v.normHi, 2).kv("nnote", v.normRef->note).kv("nsrc", v.normRef->source);
    }
    if (v.rawCount) {
      uint8_t r[2] = {v.raw0, v.raw1};
      w.key("raw").hexBytes(r, v.rawCount);
    }
    w.endObject();
  }
  w.endArray();
  w.endObject();
}

static void writeServices(JsonWriter& w, const EngineState& s) {
  w.key("services").beginObject();
  w.kv("interlock", s.interlock).kv("engineer_mode", AppSettings.engineerMode);
  w.kv("battery_manual_confirm", AppSettings.batteryManualConfirm);
  w.kv("last_result", s.lastServiceResult);
  w.kv("purge_on", s.purgeOn);
  w.key("ops").beginArray();
  for (uint8_t o = 0; o < (uint8_t)guard::Op::Count; o++) {
    guard::Op op = (guard::Op)o;
    const prof::Capability& c = s.profile.caps[guard::opFeature(op)];
    w.beginObject().kv("id", guard::opCode(op)).kv("name", guard::opName(op));
    w.kv("allowed", c.allowed).kv("conf", conf(c.conf)).kv("reason", c.reason ? c.reason : "").endObject();
  }
  w.endArray();
  const guard::Plan& p = s.plan;
  w.key("plan").beginObject();
  w.kv("op", guard::opCode(p.op)).kv("ok", p.ok);
  if (p.op != guard::Op::Count) {
    char tok[12];
    snprintf(tok, sizeof tok, "%08lX", (unsigned long)p.token);
    w.kv("token", p.ok ? tok : "");
    w.kv("phrase", p.phrase);
    w.kv("expires_in_ms", p.ok ? (int32_t)(p.expiresMs - millis()) : 0);
    w.kv("block_reason", p.blockReason ? p.blockReason : "");
    w.key("checks").beginArray();
    for (uint8_t i = 0; i < p.nChecks; i++) {
      w.beginObject().kv("id", p.checks[i].id).kv("text", p.checks[i].text);
      w.key("result");
      if (p.checks[i].result < 0) w.null(); else w.value(p.checks[i].result == 1);
      w.endObject();
    }
    w.endArray();
  }
  w.endObject();
  w.endObject();
}

static void writeStatus(JsonWriter& w, const EngineState& s) {
  w.kv("fw", HFM_FW_VERSION).kv("uptime_s", (uint32_t)(millis() / 1000));
  // One link state for the whole UI (D-027).
  w.kv("link", linkPhaseCode(s.link)).kv("link_text", s.linkMsg);
  w.kv("recover_attempts", s.recoverAttempts).kv("recoveries", s.recoveries).kv("resyncs", s.resyncs);
  w.kv("queue", (unsigned)Engine.queued());
  if (s.ident.present) {
    w.kv("ecu_part", s.ident.partDisplay).kv("ecu_identifier", s.ident.identifier);
    w.kv("ecu_diag", s.ident.diagDisplay).kv("ecu_vendor", s.ident.vendorKnown ? s.ident.vendor : "");
  }
  w.kv("session_id", Engine.sessionId());
  w.kv("transport", Engine.transportName());
  w.kv("session", sessionStateCode(s.session)).kv("last_error", linkStatusCode(s.lastError));
  w.kv("connected_s", s.session == SessionState::Ready ? (uint32_t)((millis() - s.connectedAtMs) / 1000) : 0u);
  w.kv("tx", s.txCount).kv("errors", s.errCount);
  w.kv("parity_errors", s.parityErrors).kv("frame_errors", s.frameErrors);
  w.key("adapter").beginObject().kv("host_ready", s.adapter.hostReady).kv("present", s.adapter.present);
  w.kv("configured", s.adapter.configured).kv("chip", s.adapter.chip);
  char vp[12];
  snprintf(vp, sizeof vp, "%04X:%04X", s.adapter.vid, s.adapter.pid);
  w.kv("vid_pid", vp).kv("attach_count", s.adapter.attachCount).kv("usb_errors", s.adapter.usbErrors);
  w.kv("breaks", s.adapter.breaks).kv("overruns", s.adapter.overruns);
  if (s.adapter.usbDevices >= 0) w.kv("usb_devices", (uint32_t)s.adapter.usbDevices); else w.kvNull("usb_devices");
  snprintf(vp, sizeof vp, "%04X:%04X", s.adapter.seenVid, s.adapter.seenPid);
  w.kv("dev_events", s.adapter.deviceEvents).kv("seen_vid_pid", (s.adapter.seenVid || s.adapter.seenPid) ? vp : "");
  w.kv("note", s.adapter.note).kv("phy_otg", s.adapter.phyOtg).kv("port_powered", s.adapter.portPowered);
  w.kv("port_connected", s.adapter.portConnected).kv("dp", (s.adapter.lineState & 1) != 0);
  w.kv("dm", (s.adapter.lineState & 2) != 0);
  // K-Line wake-up: profile used next and what the last attempt saw on the line.
  const kwake::Profile& np = s.adapter.wakeNext;
  w.key("wake").beginObject().kv("auto", AppSettings.wakeAuto).kv("locked", s.adapter.wakeLocked);
  w.kv("attempts", s.adapter.wakeAttempts).kv("next_break_ms", (unsigned)np.breakMs);
  w.kv("next_dtr", (np.modem & kwake::kDtr) != 0).kv("next_rts", (np.modem & kwake::kRts) != 0);
  if (s.adapter.haveWake) {
    const kwake::Diag& d = s.adapter.wake;
    char rx[3 * sizeof d.rx + 1] = {0}, tx[3 * sizeof d.tx + 1] = {0};
    for (uint8_t i = 0; i < d.rxCount; i++) snprintf(rx + 3 * i, 4, i ? " %02X" : "%02X", d.rx[i]);
    for (uint8_t i = 0; i < d.txCount; i++) snprintf(tx + 3 * i, 4, i ? " %02X" : "%02X", d.tx[i]);
    w.key("last").beginObject().kv("break_ms", (unsigned)d.profile.breakMs);
    w.kv("dtr", (d.profile.modem & kwake::kDtr) != 0).kv("rts", (d.profile.modem & kwake::kRts) != 0);
    w.kv("break_actual_ms", (unsigned)d.breakActualMs).kv("break_on_line", d.breakOnRx).kv("echo", d.echo);
    w.kv("ok", d.ok).kv("step", (unsigned)d.step).kv("ecu_byte", (unsigned)d.ecuByte);
    w.kv("aa_at_ms", (unsigned)d.aaAtMs).kv("tx", tx).kv("rx", rx).endObject();
  }
  w.endObject().endObject();
  w.kv("auto_connect", AppSettings.autoConnect).kv("auto_msg", s.autoMsg).kv("auto_attempts", s.autoAttempts);
  w.kv("interlock", s.interlock);
  if (s.batteryV >= 0) w.kv("battery_v", (double)s.batteryV, 2); else w.kvNull("battery_v");
  w.kv("ecu", s.profile.name).kv("ecu_resolved", s.profile.resolved).kv("ecu_present", s.ident.present);
  w.kv("live_active", s.liveActive).kv("recording", s.recording).kv("record_rows", s.recordRows);
  w.kv("guard", guard::stateCode(s.plan.op != guard::Op::Count && s.plan.ok ? guard::State::Prepared : guard::State::Idle));
  w.kv("trace_last", Engine.trace().lastSeq());
  w.key("job").beginObject().kv("id", s.job.id).kv("job", jobCode(s.job.job)).kv("state", jobStateCode(s.job.state));
  w.kv("message", s.job.message).endObject();
  w.key("net").beginObject();
  netStatusJson(w);
  w.endObject();
  w.kv("heap", (uint32_t)ESP.getFreeHeap());
  int16_t vi = AppSettings.vehicleIdx;
  const prof::VehicleEntry* ve = vi >= 0 ? prof::vehicleByIndex((uint16_t)vi) : nullptr;
  w.key("vehicle");
  if (ve) {
    w.beginObject().kv("idx", (int)vi).kv("chassis", ve->chassis).kv("model", ve->model).kv("name", ve->name);
    w.kv("engine", ve->engine).kv("norms", prof::normGroupCode(ve->norms)).kv("connector", ve->connector).endObject();
  } else {
    w.null();
  }
}

// ---- handlers --------------------------------------------------------------------------
// USB diagnostics: devices enumerated on the OTG port (hubs included), root
// port state and the last USB stack messages.
static void hUsb() {
  JsonWriter w(1536);
  w.beginObject();
  FtdiLink* f = Engine.usbLink();
  w.kv("available", f != nullptr);
  if (f) {
    FtdiLink::PortInfo pi = f->portInfo();
    w.kv("host_ready", f->hostReady()).kv("attached", f->attached());
    int nd = f->usbDevices();
    if (nd >= 0) w.kv("usb_devices", (uint32_t)nd); else w.kvNull("usb_devices");
    w.kv("dev_events", f->deviceEvents()).kv("enum_failures", f->enumFailures());
    w.kv("reenum_count", f->reenumCount()).kv("reenum_max", (uint32_t)FtdiLink::kMaxReenum);
    w.key("port").beginObject().kv("phy_otg", pi.phyOtg).kv("powered", pi.powered).kv("connected", pi.connected);
    w.kv("enabled", pi.enabled).kv("dp", (pi.lineState & 1) != 0).kv("dm", (pi.lineState & 2) != 0).endObject();
    FtdiLink::SeenDevice d[FtdiLink::kMaxSeen];
    int n = f->seenDevices(d, FtdiLink::kMaxSeen);
    w.key("devices").beginArray();
    for (int i = 0; i < n; i++) {
      char vp[12];
      snprintf(vp, sizeof vp, "%04X:%04X", d[i].vid, d[i].pid);
      static const char* kSpeed[] = {"low", "full", "high"};
      w.beginObject().kv("addr", (uint32_t)d[i].addr).kv("vid_pid", vp).kv("class", (uint32_t)d[i].cls);
      w.kv("speed", d[i].speed < 3 ? kSpeed[d[i].speed] : "?").kv("behind_hub", d[i].behindHub);
      w.kv("hub_port", (uint32_t)d[i].hubPort).endObject();
    }
    w.endArray();
    static char lines[FtdiLink::kLogLines][FtdiLink::kLogLen];
    int nl = f->usbLog(lines, FtdiLink::kLogLines);
    w.key("log").beginArray();
    for (int i = 0; i < nl; i++) w.value(lines[i]);
    w.endArray();
  }
  w.endObject();
  sendSmall(w);
}

static void hUsbReset() {
  FtdiLink* f = Engine.usbLink();
  if (!f) return sendError(409, "USB host not used");
  f->requestReenumerate();
  JsonWriter w(64);
  w.beginObject().kv("ok", true).endObject();
  sendSmall(w);
}

static void hStatus() {
  JsonWriter w(1024);
  Engine.lock();
  w.beginObject();
  writeStatus(w, Engine.state());
  w.endObject();
  Engine.unlock();
  sendSmall(w);
}

static void hEcu() {
  JsonWriter w(4096);
  Engine.lock();
  w.beginObject();
  writeIdent(w, Engine.state());
  writeAdaptCoding(w, Engine.state());
  w.endObject();
  Engine.unlock();
  sendSmall(w);
}

static void hDtc() {
  JsonWriter w(4096);
  Engine.lock();
  w.beginObject();
  writeDtc(w, Engine.state());
  w.endObject();
  Engine.unlock();
  sendSmall(w);
}

static void hLive() {
  bool defs = server.hasArg("defs");
  JsonWriter w(defs ? 8192 : 3072);
  Engine.lock();
  w.beginObject();
  writeLive(w, Engine.state(), defs);
  w.endObject();
  Engine.unlock();
  sendSmall(w);
}

static void hServices() {
  JsonWriter w(2048);
  Engine.lock();
  w.beginObject();
  writeServices(w, Engine.state());
  w.endObject();
  Engine.unlock();
  sendSmall(w);
}

static void hVehicles() {
  beginChunked();
  JsonWriter w(2048);
  w.setSink(chunkSink, nullptr);
  w.beginObject();
  w.key("vehicles").beginArray();
  uint16_t n;
  const prof::VehicleEntry* v = prof::vehicles(&n);
  for (uint16_t i = 0; i < n; i++) {
    char from[10] = "", to[10] = "";
    if (v[i].from) snprintf(from, sizeof from, "%02lu/%04lu", (unsigned long)(v[i].from % 100), (unsigned long)(v[i].from / 100));
    if (v[i].to) snprintf(to, sizeof to, "%02lu/%04lu", (unsigned long)(v[i].to % 100), (unsigned long)(v[i].to / 100));
    w.beginObject().kv("idx", (unsigned)i).kv("chassis", v[i].chassis).kv("model", v[i].model).kv("name", v[i].name);
    w.kv("engine", v[i].engine).kv("from", from).kv("to", to).kv("gen", prof::generationCode(v[i].gen));
    w.kv("gen_name", prof::generationName(v[i].gen)).kv("norms", prof::normGroupCode(v[i].norms));
    w.kv("connector", v[i].connector).kv("dm", v[i].dm).kv("conf", conf(v[i].conf)).kv("source", v[i].source);
    w.endObject();
  }
  w.endArray();
  w.key("connectors").beginArray();
  const prof::ConnectorProfile* c = prof::connectors(&n);
  for (uint16_t i = 0; i < n; i++) {
    w.beginObject().kv("id", c[i].id).kv("name", c[i].name).kv("pins", (unsigned)c[i].pins);
    w.kv("conf", conf(c[i].conf)).kv("source", c[i].source).kv("warning", c[i].warning);
    w.key("map").beginArray();
    for (uint8_t k = 0; k < c[i].mapLen; k++) w.beginObject().kv("pin", (unsigned)c[i].map[k].pin).kv("role", c[i].map[k].role).endObject();
    w.endArray().endObject();
  }
  w.endArray();
  w.key("norms").beginArray();
  const prof::Norm* nr = prof::norms(&n);
  for (uint16_t i = 0; i < n; i++) {
    w.beginObject().kv("param", nr[i].param).kv("group", prof::normGroupCode(nr[i].group));
    w.kv("lo", (double)nr[i].lo, 2).kv("hi", (double)nr[i].hi, 2).kv("note", nr[i].note).kv("source", nr[i].source).endObject();
  }
  w.endArray();
  w.endObject();
  endChunked(w);
}

static void hTrace() {
  uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
  uint16_t max = server.hasArg("max") ? (uint16_t)server.arg("max").toInt() : 200;
  if (max > 600) max = 600;
  beginChunked();
  JsonWriter w(2048);
  w.setSink(chunkSink, nullptr);
  w.beginObject().kv("last", Engine.trace().lastSeq()).kv("count", (unsigned)Engine.trace().count());
  w.key("events");
  Engine.trace().writeJson(w, after, max);
  w.endObject();
  endChunked(w);
}

static void hTraceJsonl() {
  server.sendHeader("Content-Disposition", "attachment; filename=\"hfm_trace.jsonl\"");
  beginChunked("application/x-ndjson");
  Engine.lock();
  char profile[32];
  strlcpy(profile, Engine.state().profile.id, sizeof profile);
  Engine.unlock();
  TraceEvent buf[32];
  uint32_t after = 0;
  std::string out;
  for (;;) {
    uint16_t n = Engine.trace().copySince(after, buf, 32);
    if (!n) break;
    out.clear();
    for (uint16_t i = 0; i < n; i++) TraceLog::writeJsonl(out, buf[i], Engine.sessionId(), Engine.transportName(), profile);
    server.sendContent(out.c_str(), out.size());
    after = buf[n - 1].seq;
  }
  server.sendContent("");
}

static void hReport() {
  server.sendHeader("Content-Disposition", server.hasArg("dl") ? "attachment; filename=\"hfm_report.json\"" : "inline");
  beginChunked();
  JsonWriter w(4096);
  w.setSink(chunkSink, nullptr);
  Engine.lock();
  const EngineState& s = Engine.state();
  w.beginObject();
  w.kv("schema", "hfm.report.v1").kv("generator", HFM_FW_NAME " " HFM_FW_VERSION);
  w.kv("disclaimer", "Программные испытания и реконструированный протокол (РЕК). Значения не являются заводской "
                     "трассой; см. уровень подтверждения у каждого поля.");
  w.key("status").beginObject();
  writeStatus(w, s);
  w.endObject();
  writeIdent(w, s);
  writeAdaptCoding(w, s);
  writeDtc(w, s);
  writeLive(w, s, true);
  writeServices(w, s);
  writeImpulse(w, s);
  w.endObject();
  Engine.unlock();
  endChunked(w);
}

static void hJobGet() {
  uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
  JobInfo j;
  Engine.lock();
  bool have = Engine.jobInfo(id, j);
  Engine.unlock();
  if (!have) return sendError(404, "unknown job");
  JsonWriter w(512);
  w.beginObject().kv("id", j.id).kv("job", jobCode(j.job)).kv("state", jobStateCode(j.state));
  w.kv("message", j.message).endObject();
  sendSmall(w);
}

static void hJob() {
  String j = server.arg("job");
  static const struct { const char* name; Job job; } map[] = {
      {"connect", Job::Connect},       {"disconnect", Job::Disconnect}, {"identify", Job::Identify},
      {"read_dtc", Job::ReadDtc},      {"read_adapt", Job::ReadAdapt},  {"read_coding", Job::ReadCoding},
      {"read_vin", Job::ReadVin},      {"live_start", Job::LiveStart},  {"live_stop", Job::LiveStop},
      {"svc_prepare", Job::SvcPrepare},{"svc_execute", Job::SvcExecute},{"purge_on", Job::PurgeOn},
      {"purge_off", Job::PurgeOff},    {"stop", Job::Stop},             {"impulse_read", Job::ImpulseRead},
      {"ack_fault", Job::AckFault},
  };
  Job job = Job::None;
  for (auto& m : map)
    if (j == m.name) job = m.job;
  if (job == Job::None) return sendError(400, "unknown job");
  uint32_t arg = 0;
  if (server.hasArg("op")) {
    String op = server.arg("op");
    for (uint8_t o = 0; o < (uint8_t)guard::Op::Count; o++)
      if (op == guard::opCode((guard::Op)o)) arg = o;
  }
  String text;
  if (server.hasArg("token")) {
    text = server.arg("token");
    text += "|";
    text += server.arg("phrase");
  }
  uint32_t id = Engine.submit(job, arg, text.c_str());
  JsonWriter w;
  w.beginObject().kv("ok", id != 0).kv("id", id);
  if (!id) w.kv("error", "Очередь операций заполнена — повторите через несколько секунд");
  w.endObject();
  sendSmall(w, id ? 200 : 409);
}

static void hSettingsGet() {
  JsonWriter w(1024);
  const Settings& s = AppSettings;
  w.beginObject();
  w.kv("break_ms", (unsigned)s.breakMs).kv("post_break_ms", (unsigned)s.postBreakMs);
  w.kv("byte_timeout_ms", (unsigned)s.byteTimeoutMs);
  w.kv("ftdi_modem", (unsigned)s.ftdiModem).kv("wake_auto", s.wakeAuto);
  w.kv("experimental_reads", s.experimentalReads).kv("engineer_mode", s.engineerMode);
  w.kv("auto_connect", s.autoConnect).kv("keep_alive", s.keepAlive).kv("impulse_enabled", s.impulseEnabled).kv("trace_capture", s.traceCapture);
  w.kv("battery_manual_confirm", s.batteryManualConfirm);
  w.kv("vehicle_idx", (int)s.vehicleIdx).kv("production_ym", s.productionYm);
  w.kv("vin", s.maskVin && s.vin[0] ? "***" : s.vin).kv("mask_vin", s.maskVin);
  w.kv("ap_ssid", netApSsid()).kv("ap_default_pass", strcmp(s.apPass, HFM_AP_DEFAULT_PASS) == 0);
  w.kv("sta_ssid", s.staSsid);
  w.kv("imp_press_ms", (unsigned)s.impPressMs).kv("imp_end_gap_ms", (unsigned)s.impEndGapMs);
  w.kv("vbat_adc", HFM_VBAT_ADC_PIN >= 0);
  static const char* const kThemes[] = {"auto", "light", "dark"};
  w.kv("ui_theme", kThemes[s.uiTheme % 3]).kv("live_sel", s.liveSel).kv("live_cat", s.liveCat);
  w.kv("code9", s.code9);
  w.kv("sound_level", (unsigned)s.soundLevel).kv("light_level", (unsigned)s.lightLevel);
  w.kv("dtc_on_connect", s.dtcOnConnect);
  {
    OledStatus o = oledStatus();
    char adr[8];
    snprintf(adr, sizeof adr, "0x%02X", o.address);
    w.key("oled").beginObject().kv("built", o.built).kv("on", s.oledOn).kv("type", (unsigned)s.oledType);
    w.kv("flip", s.oledFlip).kv("level", (unsigned)s.oledLevel).kv("present", o.present);
    w.kv("address", o.present ? adr : "").kv("frames", o.frames).endObject();
  }
  w.kv("buzzer_fitted", HFM_BUZZER_PIN >= 0).kv("led_fitted", HFM_LED_R_PIN >= 0);
  w.kv("wake_learned", s.wakeLearnedBreak != 0);
  w.endObject();
  sendSmall(w);
}

static bool argBool(const char* k, bool cur) {
  if (!server.hasArg(k)) return cur;
  String v = server.arg(k);
  return v == "1" || v == "true" || v == "on";
}

static void hSettingsPost() {
  Settings& s = AppSettings;
  const uint16_t oldBrk = s.breakMs;
  const uint8_t oldModem = s.ftdiModem;
  const bool oldAuto = s.wakeAuto;
  if (server.hasArg("break_ms")) s.breakMs = (uint16_t)constrain(server.arg("break_ms").toInt(), 25, 500);
  if (server.hasArg("post_break_ms")) s.postBreakMs = (uint16_t)constrain(server.arg("post_break_ms").toInt(), 25, 2000);
  if (server.hasArg("byte_timeout_ms")) s.byteTimeoutMs = (uint16_t)constrain(server.arg("byte_timeout_ms").toInt(), 100, 3000);
  if (server.hasArg("ftdi_modem")) s.ftdiModem = (uint8_t)(server.arg("ftdi_modem").toInt() & 3);
  s.wakeAuto = argBool("wake_auto", s.wakeAuto);
  // A changed wake-up profile replaces the one learned on the vehicle.
  if (s.breakMs != oldBrk || s.ftdiModem != oldModem || s.wakeAuto != oldAuto) {
    s.wakeLearnedBreak = 0;
    s.wakeLearnedModem = 0;
  }
  if (server.hasArg("ui_theme")) {
    String t = server.arg("ui_theme");
    s.uiTheme = t == "light" ? 1 : (t == "dark" ? 2 : 0);
  }
  if (server.hasArg("live_sel")) strlcpy(s.liveSel, server.arg("live_sel").c_str(), sizeof s.liveSel);
  if (server.hasArg("sound_level")) s.soundLevel = (uint8_t)constrain(server.arg("sound_level").toInt(), 0, 3);
  if (server.hasArg("light_level")) s.lightLevel = (uint8_t)constrain(server.arg("light_level").toInt(), 0, 3);
  s.dtcOnConnect = argBool("dtc_on_connect", s.dtcOnConnect);
  indicatorSetLevels(s.soundLevel, s.lightLevel);
  s.oledOn = argBool("oled_on", s.oledOn);
  s.oledFlip = argBool("oled_flip", s.oledFlip);
  if (server.hasArg("oled_type")) s.oledType = server.arg("oled_type").toInt() ? 1 : 0;
  if (server.hasArg("oled_level")) s.oledLevel = (uint8_t)constrain(server.arg("oled_level").toInt(), 1, 3);
  oledConfigure(s.oledOn, s.oledType, s.oledFlip, s.oledLevel);
  if (server.hasArg("live_cat")) strlcpy(s.liveCat, server.arg("live_cat").c_str(), sizeof s.liveCat);
  if (server.hasArg("code9")) {
    String c = server.arg("code9");
    char d[10] = {0};
    uint8_t k = 0;
    for (unsigned i = 0; i < c.length() && k < 9; i++)
      if (c[i] >= '0' && c[i] <= '9') d[k++] = c[i];
    strlcpy(s.code9, d, sizeof s.code9);
  }
  s.experimentalReads = argBool("experimental_reads", s.experimentalReads);
  s.engineerMode = argBool("engineer_mode", s.engineerMode);
  s.keepAlive = argBool("keep_alive", s.keepAlive);
  s.autoConnect = argBool("auto_connect", s.autoConnect);
  s.impulseEnabled = argBool("impulse_enabled", s.impulseEnabled);
  s.traceCapture = argBool("trace_capture", s.traceCapture);
  s.batteryManualConfirm = argBool("battery_manual_confirm", s.batteryManualConfirm);
  s.maskVin = argBool("mask_vin", s.maskVin);
  if (server.hasArg("vehicle_idx")) s.vehicleIdx = (int16_t)server.arg("vehicle_idx").toInt();
  if (server.hasArg("production_ym")) s.productionYm = strtoul(server.arg("production_ym").c_str(), nullptr, 10);
  if (server.hasArg("vin")) strlcpy(s.vin, server.arg("vin").c_str(), sizeof s.vin);
  if (server.hasArg("imp_press_ms")) s.impPressMs = (uint16_t)constrain(server.arg("imp_press_ms").toInt(), 2000, 4000);
  if (server.hasArg("imp_end_gap_ms")) s.impEndGapMs = (uint16_t)constrain(server.arg("imp_end_gap_ms").toInt(), 1000, 8000);
  bool netChanged = false;
  if (server.hasArg("ap_pass") && server.arg("ap_pass").length() >= 8) {
    strlcpy(s.apPass, server.arg("ap_pass").c_str(), sizeof s.apPass);
    netChanged = true;
  }
  if (server.hasArg("sta_ssid")) { strlcpy(s.staSsid, server.arg("sta_ssid").c_str(), sizeof s.staSsid); netChanged = true; }
  if (server.hasArg("sta_pass")) { strlcpy(s.staPass, server.arg("sta_pass").c_str(), sizeof s.staPass); netChanged = true; }
  s.save();
  Engine.reresolveProfile();
  JsonWriter w;
  w.beginObject().kv("ok", true).kv("net_restart", netChanged).endObject();
  sendSmall(w);
  if (netChanged) netApply();
}

static void hRecord() {
  bool on = argBool("on", false);
  bool ok = true;
  if (on) ok = Engine.startRecording(); else Engine.stopRecording();
  JsonWriter w;
  w.beginObject().kv("ok", ok).endObject();
  sendSmall(w, ok ? 200 : 500);
}

static void listDir(JsonWriter& w, const char* dir) {
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    w.beginObject().kv("path", (String(dir) + "/" + f.name()).c_str()).kv("size", (uint32_t)f.size()).endObject();
  }
}

static void hFiles() {
  JsonWriter w(1024);
  w.beginObject();
  w.kv("total", (uint32_t)LittleFS.totalBytes()).kv("used", (uint32_t)LittleFS.usedBytes());
  w.key("files").beginArray();
  listDir(w, "/rec");
  listDir(w, "/reports");
  w.endArray().endObject();
  sendSmall(w);
}

static bool safePath(const String& p) {
  return (p.startsWith("/rec/") || p.startsWith("/reports/")) && p.indexOf("..") < 0;
}

static void hFile() {
  String p = server.arg("path");
  if (!safePath(p) || !LittleFS.exists(p)) return sendError(404, "not found");
  File f = LittleFS.open(p, "r");
  String name = p.substring(p.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, p.endsWith(".csv") ? "text/csv" : "application/json");
  f.close();
}

static void hFileDelete() {
  String p = server.arg("path");
  bool ok = safePath(p) && LittleFS.remove(p);
  JsonWriter w;
  w.beginObject().kv("ok", ok).endObject();
  sendSmall(w, ok ? 200 : 404);
}

static void hReportSave() {
  // Saves the client-provided report JSON (body) to /reports.
  String body = server.arg("plain");
  if (body.length() < 2 || body.length() > 60000) return sendError(400, "bad body");
  LittleFS.mkdir("/reports");
  String name = server.hasArg("name") ? server.arg("name") : String("report");
  name.replace("/", "_");
  name.replace("..", "_");
  String path = "/reports/" + name + "_" + String(millis() / 1000) + ".json";
  File f = LittleFS.open(path, "w");
  bool ok = f && f.print(body) == body.length();
  if (f) f.close();
  JsonWriter w;
  w.beginObject().kv("ok", ok).kv("path", path.c_str()).endObject();
  sendSmall(w, ok ? 200 : 500);
}

// Reference catalogue (DTC cards, Live Data cards, scenarios): static, gzip.
static void hRef() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.send_P(200, "application/json; charset=utf-8", (const char*)WEBUI_REF_GZ, WEBUI_REF_GZ_LEN);
}

static void hIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", (const char*)WEBUI_INDEX_GZ, WEBUI_INDEX_GZ_LEN);
}

static void hNotFound() {
  if (server.uri().startsWith("/api/")) return sendError(404, "not found");
  // Captive portal: redirect everything else to the app.
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// Settings: sound / light check (does not touch the ECU).
static void hIndicatorTest() {
  String what = server.arg("what");
  if (what == "light") indicatorTestLight();
  else if (what == "dtc") indicatorPlay(server.arg("faults") == "1" ? Sound::DtcPresent : Sound::DtcNone);
  else indicatorPlay(Sound::Test);
  JsonWriter w;
  w.beginObject().kv("ok", true).endObject();
  sendSmall(w);
}

static void hReboot() {
  JsonWriter w;
  w.beginObject().kv("ok", true).endObject();
  sendSmall(w);
  delay(300);
  ESP.restart();
}

void webBegin() {
  server.on("/", HTTP_GET, hIndex);
  server.on("/index.html", HTTP_GET, hIndex);
  server.on("/ref.json", HTTP_GET, hRef);
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/usb", HTTP_GET, hUsb);
  server.on("/api/usb/reset", HTTP_POST, hUsbReset);
  server.on("/api/ecu", HTTP_GET, hEcu);
  server.on("/api/dtc", HTTP_GET, hDtc);
  server.on("/api/live", HTTP_GET, hLive);
  server.on("/api/services", HTTP_GET, hServices);
  server.on("/api/vehicles", HTTP_GET, hVehicles);
  server.on("/api/trace", HTTP_GET, hTrace);
  server.on("/api/trace.jsonl", HTTP_GET, hTraceJsonl);
  server.on("/api/report", HTTP_GET, hReport);
  server.on("/api/report/save", HTTP_POST, hReportSave);
  server.on("/api/job", HTTP_POST, hJob);
  server.on("/api/job", HTTP_GET, hJobGet);
  server.on("/api/settings", HTTP_GET, hSettingsGet);
  server.on("/api/settings", HTTP_POST, hSettingsPost);
  server.on("/api/record", HTTP_POST, hRecord);
  server.on("/api/files", HTTP_GET, hFiles);
  server.on("/api/file", HTTP_GET, hFile);
  server.on("/api/file/delete", HTTP_POST, hFileDelete);
  server.on("/api/reboot", HTTP_POST, hReboot);
  server.on("/api/indicator/test", HTTP_POST, hIndicatorTest);
  server.onNotFound(hNotFound);
  server.begin();
}

void webLoop() { server.handleClient(); }
#endif
