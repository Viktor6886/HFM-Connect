'use strict';
/* HFM Connect WebUI — vanilla JS, no external resources (works offline). */

// ---------------------------------------------------------------- helpers
const $ = (s, r = document) => r.querySelector(s);
const esc = v => String(v == null ? '' : v).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const fmt = (v, d = 0) => v == null || isNaN(v) ? '—' : Number(v).toFixed(d);
const sleep = ms => new Promise(r => setTimeout(r, ms));
// Browser cache only (instant theme before the device answers); the settings
// themselves are stored on the ESP32-S3.
const LS = {
  get(k, d) { try { const v = localStorage.getItem('hfm.' + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem('hfm.' + k, JSON.stringify(v)); } catch (e) {} }
};
const CONF = {ZAV: 'заводские данные', REK: 'по протоколу', GIP: 'не подтверждено', VAL: 'проверено на ЭБУ', BLOCK: 'недоступно'};
const CONF_HINT = {
  ZAV: 'Указано в заводской документации Mercedes-Benz', REK: 'Восстановлено из протокола обмена (спецификация REV 1.1, заводской и ПК-тестер), не заводской документ',
  GIP: 'Предположение: адрес или формула не подтверждены на реальном ЭБУ', VAL: 'Проверено записью обмена на конкретном ЭБУ', BLOCK: 'Нет подтверждённой команды — функция заблокирована'
};
const badge = c => `<span class="badge b-${esc(c)}" title="${esc(CONF_HINT[c] || '')}">${esc(CONF[c] || c)}</span>`;
const VENDOR = {BOSCH: 'Bosch', VDO: 'VDO', SIEMENS: 'Siemens'};
const MONTHS = ['январь', 'февраль', 'март', 'апрель', 'май', 'июнь', 'июль', 'август', 'сентябрь', 'октябрь', 'ноябрь', 'декабрь'];
// "38/91" (week/year of manufacture, BCD in the ECU) -> readable text.
function weekDate(t) {
  const m = /^(\d{1,2})\/(\d{2})$/.exec(t || '');
  if (!m) return t || '—';
  const w = +m[1], y = +m[2] < 50 ? 2000 + +m[2] : 1900 + +m[2];
  const d = new Date(y, 0, 1 + (w - 1) * 7 + 3);
  return `${w}-я неделя ${y} г. (${MONTHS[d.getMonth()]} ${d.getFullYear()})`;
}
function diagText(di) {
  const m = /^([0-9A-F]{2})\/([0-9A-F]{2})$/.exec(di || '');
  if (!m) return di || '—';
  const v = parseInt(m[1], 10);
  const range = v >= 40 && v <= 59 ? 'HFM-SFI (диапазон 40–59)' : v >= 60 && v <= 79 ? 'PMS (60–79) — другая система' : 'вне известных диапазонов';
  return `${m[1]}/${m[2]} — версия диагностики ЭБУ ${m[1]}: ${range}; ${m[2]} — подверсия`;
}
const DTC_MAP_TEXT = {
  C86E: '4 цилиндра, ранние ЭБУ (Bosch и VDO)', C8EE: '6 цилиндров, ранние ЭБУ (Bosch и VDO)',
  C96E: '4 цилиндра, VDO, поздние', CB9E: '4 цилиндра, VDO, самые поздние', CA9E: '4 цилиндра, Bosch, поздние',
  CA06: '6 цилиндров, VDO, поздние', CB1E: '6 цилиндров, Bosch, поздние',
  GENERIC: 'общая таблица — выберите автомобиль, чтобы применить заводскую карту для 4 или 6 цилиндров'
};
const details = (sum, html) => html ? `<details class="tech"${S.settings && S.settings.engineer_mode ? ' open' : ''}><summary>${esc(sum)}</summary>${html}</details>` : '';
const ago = ms => ms == null || ms > 4e9 ? '' : ms < 60000 ? Math.max(1, Math.round(ms / 1000)) + ' с назад' : ms < 3600000 ? Math.round(ms / 60000) + ' мин назад' : Math.round(ms / 3600000) + ' ч назад';
const agoMin = ms => ms == null || ms > 4e9 ? '' : ms < 60000 ? 'только что' : ago(ms);
const dur = s => { s = s | 0; const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), x = s % 60; return (h ? h + ' ч ' : '') + (h || m ? m + ' мин ' : '') + x + ' с'; };

// Sets innerHTML only when it changed (no needless re-rendering).
function setHTML(el, html) {
  if (typeof el === 'string') el = $(el);
  if (!el) return false;
  if (el._h === html) return false;
  el.innerHTML = html;
  el._h = html;
  return true;
}

// Requests always time out (Wi-Fi of a phone can drop silently).
async function api(path, opts = {}, timeoutMs = 6000) {
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const r = await fetch('/api/' + path, Object.assign({cache: 'no-store', signal: ctl.signal}, opts));
    const ct = r.headers.get('content-type') || '';
    const data = ct.includes('json') ? await r.json() : await r.text();
    if (!r.ok) throw new Error((data && data.error) || ('HTTP ' + r.status));
    return data;
  } catch (e) {
    if (e.name === 'AbortError') throw new Error('Нет ответа от устройства');
    throw e;
  } finally {
    clearTimeout(t);
  }
}
const post = (path, params = {}) => api(path, {method: 'POST', body: new URLSearchParams(params)});

// One request per key at a time: a slow answer is never overlapped.
const inflight = {};
function once(key, fn) {
  if (inflight[key]) return inflight[key];
  const p = Promise.resolve().then(fn).finally(() => { delete inflight[key]; });
  inflight[key] = p;
  return p;
}

function toast(msg, err = false, ms = 3200) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'toast' + (err ? ' err' : '');
  clearTimeout(toast._t);
  toast._t = setTimeout(() => t.classList.add('hidden'), ms);
}

// Runs a device job and waits for ITS result (jobs are queued on the device).
async function job(name, params = {}, btn, quiet = false) {
  let label;
  if (btn) { label = btn.innerHTML; btn.disabled = true; btn.innerHTML = '<span class="spin"></span>'; }
  try {
    const r = await post('job', Object.assign({job: name}, params));
    const t0 = Date.now();
    while (Date.now() - t0 < 90000) {
      await sleep(300);
      let j;
      try { j = await api('job?id=' + r.id, {}, 4000); } catch (e) { continue; }  // Wi-Fi hiccup: keep waiting
      if (j.state === 'done' || j.state === 'failed') {
        if (j.message && !(quiet && j.state === 'done')) toast(j.message, j.state === 'failed');
        return j.state === 'done';
      }
    }
    toast('Операция выполняется дольше обычного — результат появится на странице', true);
    return false;
  } catch (e) {
    toast(e.message, true);
    return false;
  } finally {
    if (btn) { btn.disabled = false; btn.innerHTML = label; }
    pollStatus();
  }
}

function sheet(html) {
  $('#sheetCard').innerHTML = html;
  $('#sheet').classList.remove('hidden');
}
function closeSheet() { $('#sheet').classList.add('hidden'); }
$('#sheet').addEventListener('click', e => { if (e.target.id === 'sheet') closeSheet(); });

// ---------------------------------------------------------------- icons
const I = {
  home: '<path d="M3 11l9-7 9 7v9a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"/>',
  car: '<path d="M5 16l1.5-5A2 2 0 0 1 8.4 9.6h7.2a2 2 0 0 1 1.9 1.4L19 16M4 16h16v3H4zM7 19v2M17 19v2"/>',
  plug: '<path d="M9 2v6M15 2v6M6 8h12v3a6 6 0 0 1-12 0zM12 17v5"/>',
  chip: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/>',
  warn: '<path d="M12 3l10 18H2zM12 10v5M12 18v.5"/>',
  wave: '<path d="M2 12h3l3-7 4 14 3-9 2 2h5"/>',
  sliders: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
  bolt: '<path d="M13 2L4 14h7l-1 8 9-12h-7z"/>',
  flag: '<path d="M5 21V4M5 4h11l-2 4 2 4H5"/>',
  doc: '<path d="M6 2h9l5 5v15H6zM14 2v6h6M9 13h8M9 17h8"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
  book: '<path d="M4 4h6a3 3 0 0 1 3 3v13a2 2 0 0 0-2-2H4zM20 4h-6a3 3 0 0 0-3 3v13a2 2 0 0 1 2-2h7z"/>',
  pulse: '<path d="M3 12h4l2-5 4 10 2-5h6"/><circle cx="12" cy="12" r="10"/>',
  more: '<circle cx="5" cy="12" r="1.6"/><circle cx="12" cy="12" r="1.6"/><circle cx="19" cy="12" r="1.6"/>'
};
const svg = n => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${I[n]}</svg>`;

const PAGES = [
  {id: 'home', title: 'Главная', icon: 'home', color: '#007aff'},
  {id: 'vehicles', title: 'Автомобили', icon: 'car', color: '#5856d6'},
  {id: 'connect', title: 'Подключение', icon: 'plug', color: '#34c759'},
  {id: 'ecu', title: 'Идентификация ЭБУ', icon: 'chip', color: '#30b0c7'},
  {id: 'dtc', title: 'Ошибки', icon: 'warn', color: '#ff3b30'},
  {id: 'live', title: 'Live Data', icon: 'wave', color: '#ff9500'},
  {id: 'adapt', title: 'Адаптации', icon: 'sliders', color: '#af52de'},
  {id: 'actuators', title: 'Исполнительные тесты', icon: 'bolt', color: '#ff2d55'},
  {id: 'guide', title: 'Диагностика по симптомам', icon: 'pulse', color: '#ff2d55'},
  {id: 'codes', title: 'Справочник кодов', icon: 'book', color: '#5856d6'},
  {id: 'commission', title: 'Ввод в эксплуатацию', icon: 'flag', color: '#34c759'},
  {id: 'reports', title: 'Отчёты', icon: 'doc', color: '#8e8e93'},
  {id: 'settings', title: 'Настройки', icon: 'gear', color: '#8e8e93'}
];
const TABS = ['home', 'connect', 'dtc', 'live', 'more'];

// ---------------------------------------------------------------- state
const S = {status: null, ecu: null, dtc: null, live: null, defs: null, vehicles: null, services: null, settings: null,
  hist: {}, sel: LS.get('liveSel', ['rpm', 'maf', 'o2_mv', 'lambda']), cat: LS.get('liveCat', 'all'), freeze: false,
  traceLast: 0, page: 'home', timers: [], token: 0};
const NET = {fails: 0, online: true};

// ---------------------------------------------------------------- reference catalogue (/ref.json)
// Built from data/reference/hfm_reference.json by tools/build_webui.py; static, cached by version.
const REF_VERSION = '__REF_VERSION__';
let refP = null;
function getRef() {
  if (!refP) {
    refP = fetch('/ref.json?v=' + REF_VERSION, {cache: 'force-cache'}).then(r => {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    }).catch(e => { refP = null; throw e; });
  }
  return refP;
}
const LVL = {ZAV: 'ZAV', REK: 'REK', GIP: 'GIP'};
const DEF_STATUS = {
  DOCUMENTED_CATALOG_TEXT: 'Текст каталога REV 1.1 (реконструкция)',
  FACTORY_DOCUMENTED_GENERIC_COMPONENT: 'Заводское руководство: компонент',
  FACTORY_DOCUMENTED_MEANING: 'Заводское руководство: значение кода',
  HHT_CONTEXT_RECONSTRUCTED_NOT_ECU_VERIFIED: 'Предварительная расшифровка по контекстному правилу заводского тестера HHT'
};
const MAP_DIAG = {lt_I: "diag < 'I'", I_to_P: "'I' ≤ diag < 'P'", ge_I: "diag ≥ 'I'", ge_P: "diag ≥ 'P'"};
const mapText = (ref, id) => {
  const m = ref && ref.maps[id];
  return m ? `${id} (${m.cyl} цил., ${m.vendor.split('').map(v => v === 'B' ? 'Bosch' : 'VDO').join('/')}, ${MAP_DIAG[m.diag] || m.diag})` : id;
};

// ---------------------------------------------------------------- expandable cards
// A small "карточка …" link under an item toggles a card right below it. Cards are
// closed by default; the open ones are remembered while the page is re-rendered.
const CARDS = {};
S.open = new Set();
function xcard(id, label, body) {
  CARDS[id] = body;
  const open = S.open.has(id);
  let inner = '';
  if (open) { try { inner = body(); } catch (e) { inner = ''; } }
  return `<button type="button" class="xlink${open ? ' on' : ''}" data-x="${esc(id)}">${esc(label)}</button>` +
    `<div class="xcard${open ? ' open' : ''}" data-xb="${esc(id)}"><div><div class="xin">${inner}</div></div></div>`;
}
function xsec(title, html) { return html ? `<h4>${esc(title)}</h4><div>${html}</div>` : ''; }
function xtext(s) { return s ? `<p>${esc(s)}</p>` : ''; }
document.addEventListener('click', e => {
  const b = e.target.closest('.xlink');
  if (!b) return;
  // Only the small link opens the card; the row / tile itself keeps its own action.
  e.stopPropagation();
  e.preventDefault();
  const id = b.dataset.x;
  const box = b.parentElement.querySelector(`.xcard[data-xb="${CSS.escape(id)}"]`);
  if (!box) return;
  const open = !box.classList.contains('open');
  if (open) {
    S.open.add(id);
    try { box.querySelector('.xin').innerHTML = (CARDS[id] || (() => ''))(); } catch (x) {}
  } else {
    S.open.delete(id);
  }
  box.classList.toggle('open', open);
  b.classList.toggle('on', open);
  const t = b.closest('.tile');
  if (t) t.classList.toggle('open', open);
}, true);

// ---- DTC card
function dtcCard(x, d) {
  const ref = S.ref, r = ref && ref.dtc[x.code];
  if (!ref) return '<p class="muted">Справочник загружается…</p>';
  if (!r) return `<p>Кода ${esc(x.code)} нет в справочнике: показан исходный бит без расшифровки (правило: не придумывать описание).</p>`;
  const badges = `${badge(LVL[r.lvl] || 'GIP')}${r.st === 'HHT_CONTEXT_RECONSTRUCTED_NOT_ECU_VERIFIED' ? '<span class="badge b-GIP">предварительно</span>' : ''}<span class="badge b-gray">на ЭБУ не проверено</span>`;
  let applic = '';
  if (d) {
    if (!d.dtc_map || d.dtc_map === 'GENERIC') applic = 'Карта варианта не выбрана (не известно число цилиндров — выберите автомобиль): код получен по базовой таблице позиций' + (x.rule ? `, применено правило ремапа ${x.rule}` : '') + '.';
    else if (r.maps.includes(d.dtc_map)) applic = `Код входит в заводскую карту вашего варианта: ${mapText(ref, d.dtc_map)}.`;
    else applic = `В заводской карте вашего варианта (${mapText(ref, d.dtc_map)}) этого кода нет.`;
  }
  const chain = x.pos ? `F1 ${esc(x.index)}, бит ${x.bit} → позиция ${x.pos} → ${x.map && x.map !== 'GENERIC' ? 'карта ' + esc(x.map) : 'базовая таблица' + (x.rule ? ' → ремап ' + esc(x.rule) : '')} → код ${String(x.code).padStart(3, '0')}` : '';
  const rules = (r.r || []).map(n => ref.hht_rules[n] ? `<div class="small"><b>№${n}</b> ${esc(ref.hht_rules[n].comp)} · ${esc(ref.hht_rules[n].en)}</div>` : '').join('');
  return `<div class="flex wrap">${badges}</div>
    ${xsec('Расшифровка', xtext(r.t))}
    ${r.w ? `<div class="note warn small mt">${esc(r.w)}</div>` : ''}
    ${xsec('Возможные причины', xtext(r.c))}
    ${xsec('Порядок проверки', xtext(r.k))}
    ${xsec('Проявления: что наблюдать в Live Data', r.o ? xtext(r.o) + `<button class="btn sm sec mt" onclick="go('live')">Открыть Live Data</button>` : '')}
    ${xsec('Применимость к вашему ЭБУ', xtext(applic) + (r.maps.length ? `<p class="small muted">Код есть в картах: ${r.maps.map(m => esc(mapText(ref, m))).join(', ')}.</p>` : '<p class="small muted">Код не входит ни в одну заводскую карту вариантов.</p>'))}
    ${xsec('Цепочка декодирования', chain ? `<p class="mono">${chain}</p>` : '')}
    ${xsec('Контекстные правила HHT (исходный текст)', rules ? rules + '<p class="small muted">Символы «-//-», «⌐¬» — артефакты источника, не указание полярности.</p>' : '')}
    ${xsec('Источник и достоверность', `<p class="small">${esc(DEF_STATUS[r.st] || r.st)}. ${esc(r.ev)}</p><p class="small muted">${esc(r.app)}</p>`)}`;
}

// ---- Live Data parameter card
const ROUTE = {native_group: 'нативная группа full8', hht_control_recipe: 'рецепт заводского тестера', hht_experimental_recipe: 'расширенный рецепт заводского тестера'};
function paramCard(k) {
  const ref = S.ref;
  if (!ref) return '<p class="muted">Справочник загружается…</p>';
  const defs = Object.fromEntries((S.defs || []).map(d => [d.k, d]));
  const d = defs[k] || {};
  const es = ref.live.filter(e => e.key === k);
  const p = es[0] || {};
  const st = S.status, veh = st && st.vehicle;
  const normKey = k === 'adapt_lower' || k === 'adapt_upper' ? 'adapt_mult' : k;
  const norms = ref.norms.filter(n => n.key === normKey || (k === 'adapt_idle' && n.key === 'adapt_idle_vdo'));
  const ng = veh && veh.norms;
  const L = S.live, plan = L && L.plan;
  const normRows = norms.length ? `<table class="kv">${norms.map(n => `<tr${n.group === ng ? ' class="cur"' : ''}><td>${esc(n.profile)}</td><td>${esc(n.range)} ${esc(n.unit)}<div class="small muted">${esc(n.cond)} · ${esc(n.src)}</div></td></tr>`).join('')}</table>
    <p class="small muted">${ng ? 'Выделена строка вашего двигателя.' : 'Выберите автомобиль, чтобы применялась норма вашего двигателя.'} Нормы действуют только при указанных условиях.</p>` : '<p class="small muted">Заводской нормы для этого параметра нет: отклонение не подсвечивается.</p>';
  const routes = es.map(e => `<div class="small mt"><b>${esc(ROUTE[e.route] || e.route)}</b> · ${esc(e.cmd)} · ${esc(e.sel)}<div class="mono">${esc(e.f)}${e.unit ? ' [' + esc(e.unit) + ']' : ''}</div><div class="muted">${esc(e.status)} · ${esc(e.ref)}${e.conflict ? ' · ' + esc(e.conflict) : ''}</div></div>`).join('');
  const sel = S.sel.includes(k);
  return `<div class="flex wrap">${badge(d.conf || 'GIP')}<span class="badge b-gray">на ЭБУ не проверено</span></div>
    <div class="pnow" data-pk="${esc(k)}">${paramNow(k)}</div>
    ${xsec('Что означает', xtext(p.mean))}
    ${xsec('Ожидаемое поведение', xtext(p.exp))}
    ${xsec('Причины отклонений', xtext(p.bad))}
    ${xsec('Заводские нормы', normRows)}
    ${xsec('Как читает устройство', `<p class="small">${esc(d.src || '')} · <span class="mono">${esc(d.formula || '')}</span>${plan === 'vdo' ? '<br>Ваш ЭБУ опрашивается по плану VDO (12/22/23): масштаб VDO не подтверждён, преобразование как у Bosch.' : ''}</p>`)}
    ${xsec('Способы получения в источниках', routes)}
    <div class="btns mt"><button class="btn sm sec" data-chart="${esc(k)}">${sel ? 'Убрать с графика' : 'Показать на графике'}</button></div>`;
}
function paramNow(k) {
  if (k.startsWith('adapt_')) return '';
  const L = S.live, v = L && L.values.find(x => x.k === k);
  const d = (S.defs || []).find(x => x.k === k) || {};
  if (!v || v.v == null) return '<p class="small muted">Сейчас: нет значения (опрос не запущен или параметр не читается у этого ЭБУ).</p>';
  const val = d.bool ? (v.v ? 'ВКЛ' : 'выкл') : fmt(v.v, d.dec) + ' ' + (d.unit || '');
  const stale = !(L.age_ms < STALE_MS);
  const p = S.ref && S.ref.live.find(e => e.key === k);
  let t;
  if (stale) t = `Сейчас: ${esc(val)} — данные устарели (${esc(ago(L.age_ms))}), оценка не выполняется.`;
  else if (v.norm === 'BELOW' || v.norm === 'ABOVE') t = `<b style="color:var(--red)">Сейчас: ${esc(val)} — ${v.norm === 'BELOW' ? 'ниже' : 'выше'} нормы ${fmt(v.lo, d.dec)}…${fmt(v.hi, d.dec)}.</b>${p && p.bad ? ' Возможные причины: ' + esc(p.bad) : ''}`;
  else if (v.norm === 'IN_SPEC') t = `Сейчас: ${esc(val)} — в норме.`;
  else if (v.norm === 'NOT_APPLICABLE') t = `Сейчас: ${esc(val)} — условия проверки нормы не выполнены (${esc(v.nnote || 'прогретый ХХ')}).`;
  else t = `Сейчас: ${esc(val)}.`;
  return `<p class="small">${t}</p>`;
}
document.addEventListener('click', e => {
  const b = e.target.closest('[data-chart]');
  if (!b) return;
  e.stopPropagation();
  const k = b.dataset.chart, i = S.sel.indexOf(k);
  if (i >= 0) S.sel.splice(i, 1); else { S.sel.push(k); if (S.sel.length > 4) S.sel.shift(); }
  b.textContent = S.sel.includes(k) ? 'Убрать с графика' : 'Показать на графике';
  saveLiveSel();
  if (typeof renderLive === 'function') renderLive();
}, true);

// ---- transport (HFM Connect) error card for the current link problem
function transportId(s) {
  if (!s || s.link === 'ready' || s.link === 'paused' || s.link === 'idle') return '';
  if (s.link === 'no_adapter') return 'HFM-TR-001';
  const w = s.adapter && s.adapter.wake && s.adapter.wake.last;
  switch (s.last_error) {
    case 'NO_SYNC': return w && w.echo ? 'HFM-TR-005' : 'HFM-TR-004';
    case 'BAD_ACK': return 'HFM-TR-006';
    case 'ECHO_MISMATCH': return 'HFM-TR-007';
    case 'TIMEOUT': return s.recoveries || s.link === 'recovering' ? 'HFM-TR-011' : 'HFM-TR-008';
    case 'LINK_DOWN': return 'HFM-TR-002';
    default: return '';
  }
}
function transportCard(id) {
  const t = S.ref && S.ref.transport.find(x => x.id === id);
  if (!t) return '';
  return xcard('tr-' + id, 'карточка ошибки', () => `<div class="flex wrap"><span class="badge b-gray">${esc(t.id)}</span><span class="badge b-gray">ошибка HFM Connect, не код ЭБУ</span></div>
    ${xsec('Что произошло', xtext(t.t))}${xsec('Возможные причины', xtext(t.c))}${xsec('Что проверить', xtext(t.k))}`);
}

// ---------------------------------------------------------------- theme
function applyTheme(t) {
  if (t) LS.set('theme', t);
  t = LS.get('theme', 'auto');
  if (t === 'auto') document.documentElement.removeAttribute('data-theme');
  else document.documentElement.setAttribute('data-theme', t);
}
applyTheme();

// Settings stored on the device (theme, chart parameters, …).
async function loadSettings(force) {
  if (S.settings && !force) return S.settings;
  S.settings = await api('settings');
  applyTheme(S.settings.ui_theme);
  if (S.settings.live_sel != null) {
    S.sel = S.settings.live_sel.split(',').filter(Boolean).slice(0, 4);
    LS.set('liveSel', S.sel);
  }
  if (S.settings.live_cat) { S.cat = S.settings.live_cat; LS.set('liveCat', S.cat); }
  return S.settings;
}
let saveSelT = 0;
function saveLiveSel() {
  LS.set('liveSel', S.sel);
  LS.set('liveCat', S.cat);
  clearTimeout(saveSelT);
  saveSelT = setTimeout(() => post('settings', {live_sel: S.sel.join(','), live_cat: S.cat}).catch(() => {}), 600);
}

// ---------------------------------------------------------------- navigation
function buildNav() {
  $('#sidebar').innerHTML = `<div class="brand">HFM Connect<small>Bosch HFM-SFI · M104 / M111</small></div>` +
    PAGES.map(p => `<button data-p="${p.id}">${svg(p.icon)}<span>${p.title}</span></button>`).join('');
  $('#tabbar').innerHTML = TABS.map(id => {
    const p = PAGES.find(x => x.id === id) || {id: 'more', title: 'Ещё', icon: 'more'};
    return `<button data-p="${p.id}">${svg(p.icon)}<span>${p.id === 'live' ? 'Live' : p.title}</span></button>`;
  }).join('');
  document.querySelectorAll('[data-p]').forEach(b => b.addEventListener('click', () => go(b.dataset.p)));
  $('#backBtn').addEventListener('click', () => go('more'));
  window.addEventListener('scroll', () => $('#navbar').classList.toggle('scrolled', window.scrollY > 30));
  window.addEventListener('hashchange', () => { const h = location.hash.slice(1); if (h && h !== S.page) go(h, false); });
}

function go(id, push = true) {
  S.timers.forEach(clearTimeout);
  S.timers = [];
  S.token++;
  S.page = id;
  if (push) history.replaceState(null, '', '#' + id);
  const inMore = !TABS.includes(id);
  document.querySelectorAll('#tabbar [data-p]').forEach(b => b.classList.toggle('on', b.dataset.p === id || (inMore && b.dataset.p === 'more')));
  document.querySelectorAll('#sidebar [data-p]').forEach(b => b.classList.toggle('on', b.dataset.p === id));
  $('#backBtn').classList.toggle('hidden', !inMore || id === 'more');
  const p = PAGES.find(x => x.id === id);
  $('#navTitleSmall').textContent = p ? p.title : 'Ещё';
  window.scrollTo(0, 0);
  const fn = R[id] || R.home;
  Promise.resolve().then(fn).catch(e => toast(e.message, true));
}

function page(title, body) {
  $('#content').innerHTML = `<div class="page"><h1 class="large">${esc(title)}</h1>${body}</div>`;
}

// Page refresh loop: sequential (never overlapping), stops when the page
// changes, slower while the browser tab is hidden.
function loop(fn, ms) {
  const token = S.token;
  const tick = async () => {
    if (token !== S.token) return;
    try { await fn(); } catch (e) {}
    if (token === S.token) S.timers.push(setTimeout(tick, document.hidden ? Math.max(ms, 3000) : ms));
  };
  tick();
}

// ---------------------------------------------------------------- status bar
function pollStatus() {
  return once('status', async () => {
    try {
      S.status = await api('status', {}, 4000);
      NET.fails = 0;
      NET.online = true;
    } catch (e) {
      if (++NET.fails >= 2) NET.online = false;
    }
    refreshStatusUi();
  });
}
function statusLoop() { pollStatus().finally(() => setTimeout(statusLoop, document.hidden ? 3000 : 1000)); }
document.addEventListener('visibilitychange', () => { if (!document.hidden) pollStatus(); });
window.addEventListener('online', () => pollStatus());

const LINK = {
  ready: ['ok', 'ЭБУ на связи'], connecting: ['warn', 'подключение…'], recovering: ['warn', 'восстановление связи…'],
  no_adapter: ['err', 'нет адаптера'], paused: ['', 'не подключено'], idle: ['', 'не подключено'], error: ['err', 'нет связи с ЭБУ']
};
function setBanner(cls, text) {
  const ban = $('#banner');
  setHTML(ban, text ? `<div>${esc(text)}</div>` : '');
  ban.className = 'banner ' + cls + (text ? '' : ' hidden');
}
function refreshStatusUi() {
  const s = S.status, pill = $('#linkPill');
  if (!NET.online || !s) {
    pill.className = 'pill err';
    pill.textContent = 'нет Wi-Fi связи с устройством';
    setBanner('warn', 'Нет связи с HFM Connect по Wi-Fi. Значения на экране не обновляются; обновление продолжится автоматически после восстановления связи.');
    return;
  }
  const m = LINK[s.link] || ['', s.link];
  pill.className = 'pill ' + m[0];
  pill.textContent = (s.live_active && s.link === 'ready' ? '● ' : '') + m[1];
  if (s.link === 'recovering') setBanner('warn', s.link_text || 'Связь с ЭБУ прервалась — восстанавливаю автоматически');
  else if (s.link === 'error' || s.link === 'no_adapter') setBanner('err', s.link_text);
  else setBanner('', '');
}

// ---------------------------------------------------------------- shared fragments
// rows: [label, value, isHtml]
function kvTable(rows) {
  return `<table class="kv">${rows.map(r => `<tr><td>${esc(r[0])}</td><td>${r[2] ? r[1] : esc(r[1])}</td></tr>`).join('')}</table>`;
}

function needEcu() {
  return `<div class="note info">ЭБУ ещё не идентифицирован. Подключение выполняется автоматически; можно также открыть «Подключение» и нажать «Подключить».</div>
    <div class="btns mt"><button class="btn" onclick="go('connect')">Открыть подключение</button></div>`;
}

function connectButtons(s) {
  return s && s.link === 'ready'
    ? `<button class="btn sec" onclick="job('disconnect',{},this)">Отключить</button>`
    : `<button class="btn" onclick="job('connect',{},this)">Подключить</button>`;
}

// ---------------------------------------------------------------- pages
const R = {};

R.more = () => {
  page('Ещё', `<div class="list">${PAGES.filter(p => !TABS.includes(p.id)).map(p =>
    `<div class="row tap chev" onclick="go('${p.id}')"><div class="icon" style="background:${p.color}">${svg(p.icon).replace('stroke="currentColor"', 'stroke="#fff"')}</div><div class="grow t">${p.title}</div></div>`).join('')}</div>
    <p class="foot">HFM Connect работает автономно: веб-интерфейс хранится в ESP32-S3 и не требует Интернета.</p>`);
};

R.home = () => {
  page('HFM Connect', `<div id="homeCards"></div>
    <h2 class="section">Быстрые действия</h2><div class="btns" id="homeActions"></div>
    <p class="foot" id="homeFoot"></p>`);
  loop(async () => {
    const s = S.status;
    if (!s) { setHTML('#homeCards', '<div class="note danger">Нет связи с устройством.</div>'); return; }
    try { S.dtc = (await api('dtc')).dtc; } catch (e) {}
    const d = S.dtc, v = s.vehicle;
    const n = d && d.valid ? d.items.length : null;
    const ecuSub = [s.ecu_identifier, VENDOR[s.ecu_vendor] || '', s.ecu_diag ? 'diag ' + s.ecu_diag : ''].filter(Boolean).join(' · ');
    setHTML('#homeCards', `
      <div class="card pad stat tap" onclick="go('ecu')"><div class="k">ЭБУ</div>${s.ecu_part
        ? `<div class="n" style="font-size:24px">${esc(s.ecu_part)}</div><div class="small muted">${esc(ecuSub)}</div>`
        : '<div style="font-weight:600;margin-top:4px">не идентифицирован</div>'}</div>
      <div class="grid c2 mt">
        <div class="card pad stat tap" onclick="go('dtc')"><div class="k">Память неисправностей</div><div class="n">${n == null ? '—' : n}</div>
          <div class="small muted">${n == null ? 'не прочитана' : (n ? 'записей' : 'неисправностей нет') + ' · ' + agoMin(d.age_ms)}</div></div>
        <div class="card pad stat tap" onclick="go('vehicles')"><div class="k">Автомобиль</div><div style="font-weight:600;margin-top:4px">${v ? esc(`${v.chassis} ${v.name}`) : 'не выбран'}</div>
          ${v ? `<div class="small muted">M${esc(v.engine)}</div>` : ''}</div>
      </div>`);
    setHTML('#homeActions', `${connectButtons(s)}
      <button class="btn sec" onclick="go('dtc')">Ошибки</button>
      <button class="btn sec" onclick="go('live')">Live Data</button>
      <button class="btn sec" onclick="go('reports')">Отчёт</button>`);
    setHTML('#homeFoot', `Прошивка ${esc(s.fw)} · работает ${esc(dur(s.uptime_s))}`);
  }, 2000);
};

R.vehicles = async () => {
  page('Автомобили', '<div id="vehBox"><div class="note">Загрузка базы…</div></div>');
  if (!S.vehicles) S.vehicles = await api('vehicles');
  await loadSettings(true);
  if (!$('#vehBox')) return;
  const V = S.vehicles.vehicles, cur = S.settings.vehicle_idx;
  const groups = {};
  V.forEach(v => (groups[v.chassis] = groups[v.chassis] || []).push(v));
  const sel = V.find(v => v.idx === cur);
  const conn = sel && S.vehicles.connectors.find(c => c.id === sel.connector);
  const ym = S.settings.production_ym ? String(S.settings.production_ym) : '';
  $('#vehBox').innerHTML = `
    ${sel ? `<div class="card pad">
      <div class="flex between"><div><div style="font-size:22px;font-weight:700">${esc(sel.chassis)} ${esc(sel.name)}</div>
      <div class="muted">${esc(sel.model)} · двигатель ${esc(sel.engine)}</div></div>${badge(sel.conf)}</div>
      ${kvTable([['Поколение', sel.gen_name], ['Период (по источнику)', (sel.from || '?') + ' — ' + (sel.to || 'не указано')],
        ['Нормативы', sel.norms], ['Модуль DM (отдельный N59/1)', sel.dm || 'нет/не указано'], ['Источник', sel.source]])}
      </div>
      ${conn ? connectorCard(conn) : ''}` : '<div class="note info">Выберите автомобиль: это задаёт нормативы Live Data, ожидаемый разъём и подсказки. Команды ЭБУ разрешаются только по идентификации самого блока.</div>'}
    <h2 class="section">Дата выпуска автомобиля</h2>
    <div class="card pad"><div class="flex"><input class="field" id="ymIn" type="month" value="${ym ? ym.slice(0, 4) + '-' + ym.slice(4) : ''}">
      <button class="btn sm" id="ymSave">Сохранить</button></div>
      <p class="small muted" style="margin:8px 0 0">Переход на кодируемые ЭБУ: январь–март 1994 (MBNA 07.5/1). Дата — вспомогательный признак; поколение определяется по ID ЭБУ.</p></div>
    ${Object.keys(groups).map(ch => `<h2 class="section">${esc(ch)}</h2><div class="list">${groups[ch].map(v => `
      <div class="row tap" data-v="${v.idx}"><div class="grow"><div class="t">${esc(v.name)} <span class="muted small">${esc(v.model)}</span></div>
      <div class="s">M${esc(v.engine)} · ${esc(v.from || '?')}–${esc(v.to || '…')} · ${esc(v.gen)}</div></div>${badge(v.conf)}${v.idx === cur ? '<span style="color:var(--accent);font-weight:700">✓</span>' : ''}</div>`).join('')}</div>`).join('')}
    <h2 class="section">Справочник проверок датчиков (OEM, Engine 104 23/4–23/5)</h2>
    <div class="card pad">${sensorTables()}</div>`;
  document.querySelectorAll('[data-v]').forEach(r => r.addEventListener('click', async () => {
    try { await post('settings', {vehicle_idx: r.dataset.v}); toast('Автомобиль выбран'); } catch (e) { toast(e.message, true); }
    R.vehicles();
  }));
  $('#ymSave').addEventListener('click', async () => {
    const v = $('#ymIn').value.replace('-', '');
    try { await post('settings', {production_ym: v || 0}); toast('Дата сохранена'); } catch (e) { toast(e.message, true); }
  });
};

function sensorTables() {
  const ect = [[20, 3.5, 2500], [30, 3.1, 1700], [40, 2.7, 1170], [50, 2.3, 830], [60, 1.9, 600], [70, 1.5, 435], [80, 1.2, 325], [90, 1.0, 245], [100, 0.8, 185]];
  const iat = [[10, 3.2, 9670], [20, 2.6, 6060], [30, 2.1, 3900], [40, 1.6, 2600], [50, 1.2, 1760], [60, 0.9, 1220], [70, 0.7, 860], [80, 0.5, 620]];
  const t = (title, rows) => `<div class="small muted mt">${title}</div><table class="kv"><tr><td>°C</td><td>U, В (зажигание ВКЛ) · R, Ом (разъём 2 N3/4 снят)</td></tr>${rows.map(r => `<tr><td>${r[0]}</td><td class="mono">${r[1].toFixed(1)} В · ${r[2]} Ом</td></tr>`).join('')}</table>`;
  return t('Датчик температуры ОЖ B11/3 (±5 %)', ect) + t('Датчик температуры воздуха B17 (±5 %)', iat) +
    `<p class="small muted">Внешние измерения мультиметром по заводской программе; это не данные K-Line ${badge('ZAV')}</p>`;
}

function connectorCard(c) {
  const used = new Set(c.map.map(m => m.pin));
  const pins = c.pins ? Array.from({length: c.pins}, (_, i) => `<div class="${used.has(i + 1) ? 'used' : ''}">${i + 1}</div>`).join('') : '';
  return `<h2 class="section">Диагностический разъём</h2><div class="card pad">
    <div class="flex between"><b>${esc(c.name)}</b>${badge(c.conf)}</div>
    ${pins ? `<div class="pins mt">${pins}</div>` : ''}
    ${c.map.length ? kvTable(c.map.map(m => ['Контакт ' + m.pin, m.role])) : ''}
    <div class="note warn mt">${esc(c.warning)}</div><p class="small muted">Источник: ${esc(c.source)}</p></div>`;
}

// ---- connection
function autoText(s) {
  if (!s.auto_connect) return 'выключено в настройках';
  return {
    ready: 'включено · связь установлена', recovering: `восстановление связи${s.recover_attempts ? ' · попытка ' + s.recover_attempts : ''}`,
    connecting: 'поиск ЭБУ…', paused: 'приостановлено кнопкой «Отключить»', no_adapter: 'ожидание адаптера', error: 'ошибка', idle: 'включено'
  }[s.link] || 'включено';
}

R.connect = () => {
  page('Подключение', `
    <div class="card pad"><div class="btns" id="connBtns"></div><div class="small muted mt" id="connMsg"></div></div>
    <h2 class="section">Состояние канала</h2><div class="card pad" id="linkBox"></div>
    <h2 class="section">Порядок подключения</h2>
    <div class="list steps">
      <div class="step"><div class="num"></div><div><b>Зажигание выключено.</b> Подключите кабель к разъёму X11/4 (W124: 16-конт., контакт 8 — HFM; 38-конт.: контакт 4 — HFM). <span class="muted">Контакт 16 на W124 X11/4 — цепь 15 (зажигание), не OBD-II.</span></div></div>
      <div class="step"><div class="num"></div><div><b>Адаптер.</b> USB K-Line адаптер (FTDI) — в разъём USB (OTG) ESP32-S3 напрямую или через USB-хаб с внешним питанием. Масса — контакт 1, питание адаптера +12 В — по схеме кузова.</div></div>
      <div class="step"><div class="num"></div><div><b>Зажигание ВКЛ, двигатель не запущен.</b> Лампа CHECK ENGINE может гореть во время сеанса — это штатная индикация связи (MBUSA P-SI-14.00/14).</div></div>
      <div class="step"><div class="num"></div><div><b>Подключение автоматическое.</b> При обрыве связь восстанавливается сама, опрос Live Data продолжается. Кнопка «Отключить» приостанавливает автоподключение до нажатия «Подключить».</div></div>
    </div>
    <h2 class="section">RAW TX/RX журнал</h2>
    <div class="card"><div class="flex between wrap trbar"><span class="small muted" id="trCount"></span>
      <span class="flex"><button class="btn sm sec" id="trPause">Пауза</button><a class="btn sm sec" href="/api/trace.jsonl" download>Экспорт JSONL</a></span></div>
      <div class="trace" id="trBox"><table><thead><tr><th class="c-seq">#</th><th>t, мс</th><th>фаза</th><th class="c-par">пар.</th><th>TX</th><th>эхо</th><th>RX</th><th>статус</th></tr></thead><tbody></tbody></table></div></div>`);
  S.traceLast = 0;
  S.tracePaused = false;
  $('#trPause').addEventListener('click', e => { S.tracePaused = !S.tracePaused; e.target.textContent = S.tracePaused ? 'Продолжить' : 'Пауза'; });
  loop(() => {
    const s = S.status;
    if (!s) return;
    setHTML('#connBtns', s.link === 'ready'
      ? `<button class="btn sec" onclick="job('identify',{},this)">Повторить идентификацию</button><button class="btn red" onclick="job('disconnect',{},this)">Отключить</button>`
      : `<button class="btn full" onclick="job('connect',{},this)">Подключить</button>`);
    const tid = transportId(s);
    setHTML('#connMsg', (s.link !== 'ready' && s.link_text ? esc(s.link_text) : '') + (tid && S.ref ? '<div>' + transportCard(tid) + '</div>' : ''));
    const a = s.adapter || {};
    setHTML('#linkBox', kvTable([
      ['Адаптер', a.present ? (a.chip || 'K-Line') : 'не обнаружен'],
      ['Автоподключение', autoText(s)],
      ['Время на связи', s.link === 'ready' ? dur(s.connected_s) : '—'],
      ['Ключ WRITE-EN', s.interlock ? 'замкнут' : 'разомкнут']]));
  }, 1000);
  loop(pollTrace, 800);
};

const traceRow = e => `<tr class="${e.st !== 'OK' ? 'err' : ''}"><td class="c-seq">${e.seq}</td><td>${(e.t_us / 1000).toFixed(1)}</td><td>${e.phase}</td><td class="c-par">${e.par}</td><td>${e.phase === 'BREAK' ? '—' : e.tx}</td><td>${e.n > 0 ? e.echo : '··'}</td><td>${e.n > 1 ? e.rx : '··'}</td><td>${e.st}</td></tr>`;
async function pollTrace() {
  if (S.tracePaused) return;
  const t = await api(`trace?after=${S.traceLast}&max=150`);
  const tb = $('#trBox tbody');
  if (!tb) return;
  if (t.events.length) {
    S.traceLast = t.events[t.events.length - 1].seq;
    // Only new rows are added (newest on top); the table is capped.
    tb.insertAdjacentHTML('afterbegin', t.events.slice().reverse().map(traceRow).join(''));
    while (tb.rows.length > 300) tb.deleteRow(tb.rows.length - 1);
  }
  setHTML('#trCount', `событий в буфере: ${t.count}`);
}

// ---- ECU identification
function capRow(c) {
  let st;
  if (c.run === 'unsupported') st = '<span class="badge b-gray">не поддерживается этим ЭБУ</span>';
  else if (!c.allowed) st = '<span class="badge b-gray">недоступно</span>';
  else if (c.run === 'ok') st = '<span class="badge b-ZAV">работает</span>';
  else if (c.run === 'failed') st = '<span class="badge b-BLOCK">ошибка</span>';
  else st = '<span class="badge b-REK">доступно</span>';
  let sub = '';
  if (c.run === 'unsupported') sub = 'Проверено ' + agoMin(c.run_age_s * 1000) + ': ЭБУ не выдаёт эти данные (ответил повтором адреса или не принял запрос)';
  else if (!c.allowed) sub = c.reason;
  else if (c.run === 'ok') sub = 'Выполнено ' + agoMin(c.run_age_s * 1000);
  else if (c.run === 'failed') sub = 'Ошибка ' + agoMin(c.run_age_s * 1000) + ' — см. журнал обмена';
  else if (c.write) sub = c.reason;
  const tech = details('Подробнее', `<div class="small muted">${esc(c.tech)} · ${esc(CONF[c.conf] || c.conf)}</div>`);
  return `<div class="row"><div class="grow"><div class="t">${esc(c.name)}</div>
    <div class="flex wrap" style="gap:6px;margin:4px 0 2px">${st}${c.write ? '<span class="badge b-GIP">изменяет данные ЭБУ</span>' : ''}</div>
    ${sub ? `<div class="s">${esc(sub)}</div>` : ''}${tech}</div></div>`;
}

R.ecu = async () => {
  page('Идентификация ЭБУ', '<div id="ecuBox"></div>');
  try { await loadSettings(); } catch (e) {}
  const load = async () => {
    S.ecu = await api('ecu');
    if (!$('#ecuBox')) return;
    const e = S.ecu, id = e.ident, p = e.profile;
    if (!id.present) { setHTML('#ecuBox', needEcu()); return; }
    const c = e.coding;
    const tri = v => v == null ? '<span class="muted">не применимо для этого ЭБУ</span>' : (v ? 'да' : 'нет');
    const vendor = id.vendor_known ? `${VENDOR[id.vendor] || id.vendor}` : `не определён (код ${id.vendor_code || '—'})`;
    const map = p.variant.dtc_map;
    let coding;
    if (c.unsupported) coding = `<div class="note">Этот ЭБУ не выдаёт байт кодировки: на запрос он вернул сами адреса (27:02 / 24:F0), а не данные. У ранних блоков HFM 3.4 (до 02/1994) комплектация — тип КПП, катализатор — распознаётся автоматически по заводской документации, отдельной кодировки может не быть.</div>`;
    else if (c.valid) coding = kvTable([
      ['Коробка передач', c.automatic == null ? '<span class="muted">не применимо</span>' : c.automatic ? 'автоматическая' + (c.gears > 0 ? `, ${c.gears} ступеней` : '') : 'механическая', true],
      ['Каталитический нейтрализатор', tri(c.catalyst), true], ['Исполнение для США', tri(c.usa), true],
      ['Электронная педаль / круиз (EFP, TPM)', tri(c.efp), true], ['Диагностический модуль DM', tri(c.diag_module), true],
      ['Упор дроссельной заслонки обучен', c.throttle_taught ? 'да' : 'нет']]) +
      `<p class="small muted">Это сведения о комплектации, записанные в самом ЭБУ (байт кодировки), а не 9-значный код заводского тестера. «Не применимо» — маска ЭБУ говорит, что этот признак у блока не используется.</p>` +
      details('Технические данные', `<p class="small muted">Байт кодировки c = ${esc(c.coding_byte)}, маска неприменимых признаков m = ${esc(c.mask_byte)} (адреса 27:02 и 24:F0)${c.screen > 0 ? `; заводской тестер показывает такую кодировку на экране № ${c.screen} — служебная величина` : ''}. Расшифровка битов восстановлена по заводскому тестеру и требует проверки на автомобиле.</p>`);
    else coding = '<p class="muted" style="margin:0">Не прочитана.</p>';
    let vin;
    if (e.vin.unsupported) vin = '<p class="muted" style="margin-top:0">Этот ЭБУ не поддерживает чтение VIN (запрос F9 не принят). Связь при этом не прерывается.</p>';
    else if (e.vin.not_stored) vin = '<p class="muted" style="margin-top:0">VIN в ЭБУ не записан.</p>';
    else if (e.vin.valid) vin = `<p class="mono" style="margin-top:0">${esc(e.vin.text)}</p>`;
    else vin = '<p class="muted" style="margin-top:0">Не прочитан.</p>';
    setHTML('#ecuBox', `
      <div class="card pad">
        <div class="flex between wrap"><div><div class="muted small">Блок управления двигателем N3/4</div><div style="font-size:24px;font-weight:700">${esc(id.part)}</div></div>
        <span class="badge ${p.resolved ? 'b-ZAV' : 'b-GIP'}">${p.resolved ? 'распознан как HFM-SFI' : 'не распознан'}</span></div>
        ${kvTable([['Производитель', vendor], ['Обозначение программы', id.identifier],
          ['Изготовление (аппаратная часть)', weekDate(id.hw_date)], ['Версия программы', weekDate(id.sw_date)],
          ['Диагностический индекс', esc(diagText(id.diag_index)), true],
          ['Профиль диагностики', esc(p.name) + '<div class="small muted">набор команд и таблиц, по которому устройство работает с этим блоком</div>', true],
          ['Память неисправностей', esc(p.bank_text) + `<div class="small muted">${esc(p.bank_basis)}</div>`, true],
          ['Таблица кодов неисправностей', esc(DTC_MAP_TEXT[map] || map), true],
          ['Параметры Live Data', p.live_plan === 'vdo' ? 'шаблоны VDO заводского тестера (экспериментально)' : 'страницы Bosch 20/21 с проверкой ответов']])}
        ${details('Технические данные', kvTable([['Код производителя', esc(id.vendor_code)], ['Карта кодов', esc(map)], ['Байт диагностики F0:26', esc(p.variant.diag_raw)],
          ['Страница ПЗУ / адрес таблицы', id.ext_present ? `${esc(id.rom_page)} / ${esc(id.desc_addr)}` : 'не читались'], ['Профиль', esc(p.id)], ['RAW F0 00–1B', `<span class="hex">${esc(id.raw)}</span>`]].map(r => [r[0], r[1], true])))}
      </div>
      <h2 class="section">Комплектация (кодировка ЭБУ)</h2>
      <div class="card pad">${coding}<div class="btns mt"><button class="btn sec" id="codBtn">Прочитать кодировку</button></div></div>
      <h2 class="section">VIN</h2>
      <div class="card pad">${vin}<div class="btns"><button class="btn sec" id="vinBtn">Прочитать VIN</button></div></div>
      <h2 class="section">Что умеет этот ЭБУ</h2>
      <div class="list">${p.caps.map(capRow).join('')}</div>
      <p class="foot">Функции определяются по идентификации ЭБУ (производитель, номер, диагностический индекс) и по его фактическим ответам. Год и модель автомобиля команды не разблокируют. Операции, изменяющие данные ЭБУ, выполняются только после проверки условий, предупреждения и пароля точки доступа.</p>`);
    $('#codBtn').onclick = async ev => { if (await job('read_coding', {}, ev.target)) load(); else load(); };
    $('#vinBtn').onclick = async ev => { await job('read_vin', {}, ev.target); load(); };
  };
  loop(load, 5000);
};

// ---- fault memory
R.dtc = async () => {
  page('Ошибки', `<div class="btns"><button class="btn" id="rdBtn">Прочитать память неисправностей</button><button class="btn red" id="clrBtn">Стереть…</button></div><div id="dtcBox" class="mt"></div>`);
  try { S.ref = await getRef(); } catch (e) {}
  const render = async () => {
    const d = (await api('dtc')).dtc;
    S.dtc = d;
    if (!$('#dtcBox')) return;
    if (d.valid) {
      // Codes that were not in the previous readout are marked «новая».
      const t = Date.now() - d.age_ms, keys = new Set(d.items.map(x => x.code + '-' + x.pos));
      if (!S.dtcCur || Math.abs(S.dtcCur.t - t) > 3000) {
        S.dtcPrev = S.dtcCur;
        S.dtcCur = {t, keys};
        S.dtcNew = S.dtcPrev ? new Set([...keys].filter(k => !S.dtcPrev.keys.has(k))) : new Set();
      }
    }
    if (!d.valid) { setHTML('#dtcBox', '<div class="note">Память неисправностей ещё не прочитана.</div>'); return; }
    const names = {stored: 'Сохранённые (были раньше)', current: 'Текущие (есть сейчас)', bank_a: 'Банк A', bank_b: 'Банк B'};
    const groups = {};
    d.items.forEach(x => (groups[x.bank] = groups[x.bank] || []).push(x));
    const order = ['current', 'stored', 'bank_a', 'bank_b'].filter(k => groups[k]);
    const ints = d.items.filter(x => x.intermittent);
    const grid = (title, hex) => `<div class="mt"><div class="small muted">${title}</div><div class="hex">${hex}</div></div>`;
    setHTML('#dtcBox', `
      <div class="small muted">Прочитано ${agoMin(d.age_ms)}${d.auto ? ' · автоматически' : ''} · записей: ${d.items.length}</div>
      <div class="note mt info"><b>${esc(d.bank_text || '')}</b><br><span class="small">${esc(d.bank_mapping_basis || '')}</span></div>
      ${groups.current ? '<div class="note mt small">Текущие неисправности имеют смысл только при включённом зажигании и <b>работающем</b> двигателе; при неработающем двигателе часть из них появляется закономерно.</div>' : ''}
      ${d.dtc_map === 'GENERIC' ? '<div class="note mt small warn">Выберите автомобиль в разделе «Автомобили», чтобы применить заводскую карту кодов для 4/6 цилиндров.</div>' : ''}
      ${d.m111_texts ? '<div class="note mt small">Тексты кодов 045–069/093–096 показаны по заводской таблице Engine 111 (4 цилиндра).</div>' : ''}
      ${d.items.length === 0 ? '<div class="card pad mt"><b style="color:var(--green)">Неисправностей нет.</b><p class="small muted" style="margin:6px 0 0">Отсутствие записей не доказывает исправность системы — проверьте также Live Data и модуль DM (отдельно).</p></div>' : ''}
      ${order.map(k => `<h2 class="section">${names[k]} · ${groups[k].length}</h2><div class="list">${groups[k].map(x => dtcRow(x, d)).join('')}</div>`).join('')}
      ${ints.length ? `<p class="foot">Периодические (перемежающийся контакт) отмечены оранжевым: ${ints.length}. По OEM они фиксируются при 5 появлениях за поездку.</p>` : ''}
      ${details('Технические данные (битовые карты)', `<div class="card pad">${grid('F1 00–0F (банк A)', esc(d.bank_a))}${grid('F1 10–1F (банк B)', esc(d.bank_b))}${d.ext_valid ? grid('F1 30–32 / 40–42 (эксп.)', esc(d.ext)) : ''}
        <p class="small muted">Таблица кодов: ${esc(DTC_MAP_TEXT[d.dtc_map] || d.dtc_map || 'GENERIC')} (${esc(d.dtc_map || 'GENERIC')}, ${d.vendor === 'B' ? 'Bosch' : 'вариант V'}, байт F0:26 = ${esc(d.diag_raw || '')}). Коды вне каталога и неиспользуемые позиции показываются без расшифровки.</p></div>`)}
      <h2 class="section">Справка</h2>
      <div class="note">HFM хранит неисправности при отключении АКБ; исчезнувшие стираются автоматически за 19 поездок (WIS GF07.51). Коды P0xxx/P1xxx относятся к отдельному модулю DM N59/1 и здесь не отображаются.</div>`);
  };
  $('#rdBtn').addEventListener('click', async e => { if (await job('read_dtc', {}, e.target)) render(); });
  $('#clrBtn').addEventListener('click', () => serviceFlow('dtc_clear'));
  loop(render, 4000);
  loop(renderImpulse, 3000);
};

// Pulse counter (blink code): factory procedure for ECUs up to model year
// 1995 - the diagnostic contact is grounded through the adapter (BREAK).
async function renderImpulse() {
  const r = await api('dtc');
  if (!$('#impBox')) {
    const host = $('#dtcBox');
    if (!host || !host.parentNode) return;
    host.insertAdjacentHTML('afterend', '<h2 class="section">Импульсная диагностика (счётчик импульсов)</h2><div class="card pad" id="impBox"></div>');
  }
  const im = r.impulse || {codes: []};
  const codes = im.codes.map(c => `<div class="dtc"><div class="code">N ${c.n}</div><div class="grow">${c.a.map(a => `<div>${a.code === 1 ? '' : String(a.code).padStart(3, '0') + ' · '}${esc(a.text)}</div>`).join('')}</div></div>`).join('');
  setHTML('#impBox', `<p class="small" style="margin-top:0">Заводской способ без обмена данными: контакт диагностики замыкается на массу на 2–4 с, после чего ЭБУ «моргает» номером кода N. Адаптер делает это сам (удерживает линию K-Line). Только для ЭБУ до конца 1995 модельного года, кроме W210. На время процедуры связь по K-Line закрывается; после неё нажмите «Подключить».</p>
    ${r.impulse_enabled ? '' : '<div class="note warn small">Включите «Импульсный считыватель» в разделе «Настройки» → «Сервисные операции».</div>'}
    ${codes ? `<div class="list mt">${codes}</div>` : '<p class="muted small">Коды ещё не считывались.</p>'}
    ${im.valid && !im.complete ? `<div class="note small mt">Последняя попытка: код не распознан (вспышек: ${im.last_n}).</div>` : ''}
    <div class="btns mt"><button class="btn sec" id="impRd" ${r.impulse_enabled ? '' : 'disabled'}>Прочитать следующий код</button><button class="btn sec" id="impNew" ${r.impulse_enabled ? '' : 'disabled'}>Начать заново</button><button class="btn red" id="impClr" ${r.impulse_enabled && im.codes.length ? '' : 'disabled'}>Стереть показанный код…</button></div>
    <p class="small muted">Код N = 1 — неисправностей нет. Коды повторяются по кругу: когда снова показан первый, все прочитаны. Стирание удаляет только последний показанный код (контакт на массе ${Math.round((S.settings && S.settings.imp_clear_ms || 7000) / 1000)} с).</p>`);
  const rd = $('#impRd'), nw = $('#impNew'), cl = $('#impClr');
  if (rd) rd.onclick = ev => job('impulse_read', {}, ev.target).then(renderImpulse);
  if (nw) nw.onclick = ev => job('impulse_read', {fresh: 1}, ev.target).then(renderImpulse);
  if (cl) cl.onclick = () => serviceFlow('impulse_clear');
}

const SRC_NAME = {oem: 'заводской текст', reference: 'справочный текст', reconcile: 'требует сверки', unverified: 'не подтверждён',
  app_label: 'метка приложения', candidate: 'предварительно (HHT)', oem_m111: 'заводской текст M111', unknown: 'вне каталога'};
function dtcRow(x, d, withCard = true) {
  const isNew = withCard && S.dtcNew && S.dtcNew.has(x.code + '-' + x.pos);
  return `<div class="dtc"><div class="code ${x.intermittent ? 'int' : ''}">${x.code ? String(x.code).padStart(3, '0') : '—'}</div>
    <div class="grow"><div>${esc(x.text)}${isNew ? ' <span class="badge b-BLOCK">новая</span>' : ''}</div><div class="small muted">бит ${x.pos} (F1 ${x.index}, b${x.bit})${x.remapped ? ` · базовый ${x.base}→${x.code || '—'}` : ''}${x.rule ? ' · ремап ' + esc(x.rule) : ''}${x.map && x.map !== 'GENERIC' ? ' · карта ' + esc(x.map) : ''} · ${esc(SRC_NAME[x.source] || x.source)}${x.not_w124 ? ' · не для W124' : ''}</div>
    ${withCard && x.code ? xcard('d-' + x.code + '-' + x.pos, 'карточка ошибки', () => dtcCard(x, d)) : ''}</div></div>`;
}

// ---- Live Data
// Expected state of discrete signals where the factory documents it (hint only, no alarm).
const EXPECT = {
  idle_contact: 'ожидается ВКЛ на ХХ (педаль отпущена)',
  resonance: 'ожидается выкл на ХХ, вкл > 4000 об/мин',
  camshaft: 'M104.995: < 2000 выкл, > 2000 вкл, > 4000 выкл'
};
const STALE_MS = 3000;
const LEVELS = ['Выкл', 'Тихо', 'Средне', 'Громко'];

R.live = async () => {
  if (!S.defs) S.defs = (await api('live?defs=1')).live.values;
  try { S.ref = await getRef(); } catch (e) {}
  try { await loadSettings(); } catch (e) {}
  const cats = [['all', 'Все'], ['0', 'Базовые'], ['1', 'Смесь'], ['2', 'Зажигание'], ['3', 'Дроссель'], ['4', 'Статусы'], ['5', 'Расширенные']];
  page('Live Data', `
    <div class="btns eq3"><button class="btn" id="lvStart">Старт</button><button class="btn sec" id="lvRec">● Запись</button><button class="btn sec" id="lvFreeze">Стоп-кадр</button></div>
    <div class="flex wrap mt" id="condBox"></div>
    <div class="card pad mt"><canvas class="chart" id="chart"></canvas><div class="small muted" id="chartLegend"></div></div>
    <div class="mt" id="catBox">${cats.map(c => `<button class="chip ${S.cat === c[0] ? 'on' : ''}" data-cat="${c[0]}">${c[1]}</button>`).join('')}</div>
    <div class="grid auto" id="tiles"></div>
    <h2 class="section">Заводские нормы, которые устройство не читает</h2><div class="card pad" id="extraNorms"></div>
    <h2 class="section">Технические данные опроса</h2><div class="card pad" id="rawBox"></div>
    <p class="foot">Рамка карточки: <b style="color:var(--green)">зелёная</b> — в норме, <b style="color:var(--accent)">синяя</b> — близко к границе нормы (в пределах 10 % её ширины), <b style="color:var(--red)">красная</b> — вне нормы. Без рамки — норма не задана или условия её проверки не выполнены (прогретый двигатель > 80 °C, холостой ход). Значения, которых двигатель дать не может, и ответы ЭБУ повтором адреса не показываются. При обрыве/КЗ датчиков ОЖ, IAT и HFM ЭБУ подставляет замещающее значение — неисправность видна только в памяти DTC (OEM 11/3). Нажмите на карточку, чтобы добавить параметр на график (до 4).</p>`);
  try { if (!S.dtc) S.dtc = (await api('dtc')).dtc; } catch (e) {}
  document.querySelectorAll('[data-cat]').forEach(b => b.addEventListener('click', () => {
    S.cat = b.dataset.cat;
    document.querySelectorAll('[data-cat]').forEach(x => x.classList.toggle('on', x === b));
    S.tileKeys = null;
    saveLiveSel();
    renderLive();
  }));
  $('#tiles').addEventListener('click', e => {
    if (e.target.closest('.xcard')) return;  // inside an open parameter card
    const t = e.target.closest('.tile');
    if (!t) return;
    const k = t.dataset.k, i = S.sel.indexOf(k);
    if (i >= 0) S.sel.splice(i, 1); else { S.sel.push(k); if (S.sel.length > 4) S.sel.shift(); }
    saveLiveSel();
    renderLive();
  });
  $('#lvStart').addEventListener('click', async e => {
    const on = S.status && S.status.live_active;
    await job(on ? 'live_stop' : 'live_start', {}, e.target);
  });
  $('#lvRec').addEventListener('click', async () => {
    const on = S.status && S.status.recording;
    try { await post('record', {on: on ? 0 : 1}); toast(on ? 'Запись остановлена (Отчёты → файлы)' : 'Запись в CSV начата'); } catch (e) { toast(e.message, true); }
  });
  $('#lvFreeze').addEventListener('click', e => { S.freeze = !S.freeze; e.target.textContent = S.freeze ? 'Продолжить' : 'Стоп-кадр'; });
  S.tileKeys = null;
  loop(pollLive, 350);
};

async function pollLive() {
  const s = S.status;
  if (s) {
    const b = $('#lvStart');
    if (b && !b.disabled) b.textContent = s.live_active ? 'Стоп' : 'Старт';
    const r = $('#lvRec');
    if (r) r.textContent = s.recording ? `■ Запись ${s.record_rows}` : '● Запись';
  }
  if (S.freeze) return;
  const L = (await api('live', {}, 4000)).live;
  if (!$('#condBox')) return;
  S.live = L;
  const now = Date.now();
  if (L.age_ms < STALE_MS) L.values.forEach(v => {
    if (v.v == null) return;
    const h = S.hist[v.k] = S.hist[v.k] || [];
    if (!h.length || h[h.length - 1].seq !== L.seq) h.push({t: now - L.age_ms, v: v.v, seq: L.seq});
    if (h.length > 400) h.shift();
  });
  renderLive();
}

function renderLive() {
  const L = S.live;
  if (!L || !$('#tiles')) return;
  const defs = Object.fromEntries(S.defs.map(d => [d.k, d]));
  const dcodes = S.dtc && S.dtc.valid ? S.dtc.items.map(x => x.code) : [];
  const subst = {ect: [2, 3, 4, 5], iat: [6, 7, 8], maf: [9, 10]};
  const stale = !(L.age_ms < STALE_MS);
  const st = S.status;
  let fresh;
  if (L.age_ms > 4e9) fresh = '<span class="badge b-gray">нет данных</span>';
  else if (stale) fresh = `<span class="badge b-GIP">данные устарели · ${esc(ago(L.age_ms))}</span>`;
  else fresh = `<span class="badge b-ZAV">обновлено</span><span class="badge b-gray">цикл ${L.cycle_ms} мс</span>`;
  setHTML('#condBox', `<span class="badge ${L.active ? 'b-ZAV' : 'b-gray'}">${L.active ? (L.link_ready ? 'опрос' : 'ожидание связи') : 'остановлен'}</span>
    ${fresh}<span class="badge ${L.warm_idle ? 'b-ZAV' : 'b-gray'}">прогретый ХХ ${L.warm_idle ? '✓' : '—'}</span>
    ${L.norms_vehicle ? '' : '<span class="badge b-gray">нормы: общие для HFM-SFI — выберите автомобиль</span>'}
    ${L.plan_echo ? '<span class="badge b-BLOCK">ЭБУ не отдаёт параметры по этим адресам</span>' : ''}
    ${st && st.link === 'recovering' ? '<span class="badge b-GIP">восстановление связи</span>' : ''}`);
  const tiles = L.values.filter(v => defs[v.k] && (S.cat === 'all' || String(defs[v.k].cat) === S.cat));
  const box = $('#tiles');
  const keys = tiles.map(v => v.k).join(',');
  if (S.tileKeys !== keys) {
    box.innerHTML = tiles.map(v => `<div class="tile${S.open.has('p-' + v.k) ? ' open' : ''}" data-k="${v.k}"><div class="tl"></div>${xcard('p-' + v.k, 'карточка параметра', () => paramCard(v.k))}</div>`).join('');
    box._h = null;
    S.tileKeys = keys;
  }
  box.classList.toggle('stale', stale);
  tiles.forEach(v => {
    const el = box.querySelector(`[data-k="${v.k}"]`);
    if (!el) return;
    const d = defs[v.k];
    const alarm = !stale && (v.norm === 'BELOW' || v.norm === 'ABOVE');
    const open = S.open.has('p-' + v.k);
    el.className = 'tile ' + (stale ? 'NOT_APPLICABLE' : normClass(v)) + (alarm ? ' alarm' : '') + (S.sel.includes(v.k) ? ' sel' : '') + (open ? ' open' : '');
    el.title = d.src + ': ' + d.formula + (v.nnote ? '\n' + v.nnote + ' [' + v.nsrc + ']' : '');
    setHTML(el.querySelector('.tl'), tileInner(v, d, subst[v.k] && subst[v.k].some(x => dcodes.includes(x))));
    if (open) setHTML(el.querySelector('.pnow'), paramNow(v.k));
  });
  if (S.ref) {
    const ng = st && st.vehicle && st.vehicle.norms;
    const rows = S.ref.norms.filter(n => ['maf_voltage', 'purge_duty', 'oil_temp'].includes(n.key) && (!ng || n.group === ng));
    setHTML('#extraNorms', rows.length ? kvTable(rows.map(n => [n.param, `${esc(n.range)} ${esc(n.unit)} <span class="small muted">· ${esc(n.cond)} · ${esc(n.profile)} · ${esc(n.src)}</span>`, true])) + '<p class="small muted">Эти величины показывает заводской тестер; адрес и формула для их чтения по K-Line не подтверждены, поэтому устройство их не опрашивает.</p>'
      : `<p class="small muted" style="margin:0">${ng ? 'Для вашего двигателя таких норм в справочнике нет.' : 'Выберите автомобиль, чтобы увидеть нормы вашего двигателя.'}</p>`);
  }
  setHTML('#rawBox', details('Показать байты последнего цикла', `<div class="small muted">План опроса: ${L.plan === 'vdo' ? 'шаблоны VDO (12/22/23)' : 'страницы Bosch 20/21 (full8)'}</div><table class="kv">${L.groups.map(g => `<tr><td>${g.name} <span class="muted">${g.prefix}</span></td><td class="hex">${g.ok ? esc(g.raw) : '<span style="color:var(--red)">не прочитано</span>'}</td></tr>`).join('')}</table>`));
  drawChart();
}

// Frame colour: green in the norm, blue within 10 % of the norm width from a
// limit, red outside; no frame when no norm applies (conditions not met).
function normClass(v) {
  if (v.norm === 'IN_SPEC' && v.lo != null && v.v != null) {
    const m = (v.hi - v.lo) * 0.1;
    if (v.v < v.lo + m || v.v > v.hi - m) return 'NEAR';
  }
  return v.norm;
}

function tileInner(v, d, substOn) {
  const val = d.bool ? (v.v == null ? '—' : (v.v ? 'ВКЛ' : 'выкл')) : fmt(v.v, d.dec);
  const QT = {VALID: '', NOT_READ: 'не опрашивается', PARTIAL: 'прочитано частично', NO_DATA: 'нет данных (двигатель не работает)', OUT_OF_DOMAIN: 'вне диапазона датчика', IMPLAUSIBLE: 'недостоверно: такого значения быть не может', NOT_SUPPORTED: 'ЭБУ не отдаёт этот параметр'};
  const qtxt = v.q in QT ? QT[v.q] : v.q;
  const arrow = v.norm === 'BELOW' ? '<span class="arrow" title="ниже нормы">↓</span>' : v.norm === 'ABOVE' ? '<span class="arrow" title="выше нормы">↑</span>' : '';
  let bar = '';
  if (v.lo != null && !d.bool && v.v != null) {
    const span = d.max - d.min || 1, p = x => Math.max(0, Math.min(100, (x - d.min) / span * 100));
    bar = `<div class="bar"><i style="left:${p(v.lo)}%;width:${p(v.hi) - p(v.lo)}%"></i><b style="left:${p(v.v)}%"></b></div>`;
  }
  let norm = '';
  if (v.lo != null) norm = `норма ${fmt(v.lo, d.dec)}…${fmt(v.hi, d.dec)}${d.unit ? ' ' + esc(d.unit) : ''}${v.norm === 'NOT_APPLICABLE' ? ' · проверяется на прогретом ХХ' : ''}`;
  else if (EXPECT[v.k]) norm = EXPECT[v.k];
  const sub = substOn ? '<div class="small" style="color:var(--orange)">есть DTC датчика — возможно замещающее значение</div>' : '';
  return `<div class="dot ${v.norm}"></div>${S.sel.includes(v.k) ? '<div class="gmark" title="на графике"></div>' : ''}<div class="name">${esc(d.name)}</div>
    <div class="val">${val}${d.unit && !d.bool ? `<small>${esc(d.unit)}</small>` : ''}${arrow}</div>${bar}
    ${norm ? `<div class="norm">${norm}</div>` : ''}${sub}
    <div class="meta"><span>${esc(qtxt)}</span>${d.conf === 'GIP' ? '<span title="Адрес или формула не подтверждены на реальном ЭБУ">формула не подтверждена</span>' : ''}</div>`;
}

const PALETTE = ['#007aff', '#ff9500', '#34c759', '#af52de'];
function drawChart() {
  const c = $('#chart');
  if (!c) return;
  const dpr = window.devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
  if (c.width !== w * dpr) { c.width = w * dpr; c.height = h * dpr; }
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  const cs = getComputedStyle(document.documentElement);
  g.strokeStyle = cs.getPropertyValue('--sep');
  g.lineWidth = 1;
  for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, h * i / 4); g.lineTo(w, h * i / 4); g.stroke(); }
  const defs = Object.fromEntries((S.defs || []).map(d => [d.k, d]));
  const now = Date.now(), win = 60000;
  const legend = [];
  S.sel.forEach((k, i) => {
    const d = defs[k], hst = (S.hist[k] || []).filter(p => now - p.t < win);
    if (!d) return;
    const vals = hst.map(p => p.v);
    let lo = Math.min(d.min, ...vals), hi = Math.max(d.max, ...vals);
    if (hst.length > 4) { lo = Math.min(...vals); hi = Math.max(...vals); const pad = (hi - lo) * .15 || 1; lo -= pad; hi += pad; }
    g.strokeStyle = PALETTE[i];
    g.lineWidth = 2;
    g.beginPath();
    hst.forEach((p, j) => {
      const x = w - (now - p.t) / win * w, y = h - (p.v - lo) / (hi - lo) * h;
      j ? g.lineTo(x, y) : g.moveTo(x, y);
    });
    g.stroke();
    const last = hst.length ? hst[hst.length - 1].v : null;
    legend.push(`<span style="color:${PALETTE[i]}">■</span> ${esc(d.name)}: <b>${d.bool ? (last == null ? '—' : last ? 'ВКЛ' : 'выкл') : fmt(last, d.dec)}</b> ${esc(d.unit)} <span class="muted">[${fmt(lo, 1)}…${fmt(hi, 1)}]</span>`);
  });
  setHTML('#chartLegend', legend.join(' &nbsp; ') || 'Выберите параметры (нажмите на карточки). Окно графика — 60 с.');
}

// ---- Adaptations
R.adapt = () => {
  getRef().then(r => { S.ref = r; }).catch(() => {});
  page('Адаптации', `<div class="btns"><button class="btn" id="adBtn">Прочитать самоадаптации</button><button class="btn red" id="adRst">Сбросить…</button></div><div id="adBox" class="mt"></div>`);
  const render = async () => {
    const e = await api('ecu');
    if (!$('#adBox')) return;
    const a = e.adapt;
    if (a.unsupported) { setHTML('#adBox', '<div class="note">Этот ЭБУ не выдаёт самоадаптации по адресам 24:90/92/93: на запрос он вернул сами адреса (90, 92, 93), а не значения. Прежде показанные «RAW 90» и «1,07» были именно этими адресами. Сброс самоадаптаций для такого блока тоже недоступен — нет значений «до сброса» для сравнения.</div>'); return; }
    if (!a.valid) { setHTML('#adBox', '<div class="note">Самоадаптации не прочитаны.</div>' + (e.ident.present ? '' : needEcu())); return; }
    // Colour as on Live Data tiles: green / blue near a limit / red outside.
    const cls = (v, lo, hi) => v < lo || v > hi ? 'BELOW' : (v < lo + (hi - lo) * .1 || v > hi - (hi - lo) * .1 ? 'NEAR' : 'IN_SPEC');
    const card = (key, title, val, unit, lo, hi, base, raw, explain) => {
      const c = cls(val, lo, hi);
      const pos = Math.max(0, Math.min(100, (val - (lo - (hi - lo) * .5)) / ((hi - lo) * 2) * 100));
      return `<div class="tile ${c}" style="cursor:default"><div class="name">${title}</div>
        <div class="val">${val.toFixed(unit === '' ? 2 : 1)}${unit ? `<small>${unit}</small>` : ''}${c === 'BELOW' ? `<span class="arrow">${val < lo ? '↓' : '↑'}</span>` : ''}</div>
        <div class="bar"><i style="left:25%;width:50%"></i><b style="left:${pos}%"></b></div>
        <div class="norm">норма ${lo}…${hi}${unit ? ' ' + unit : ''} · базовое ${base}</div><div class="small mt">${explain}</div>
        ${details('Подробнее', `<div class="small muted">исходный байт ${esc(raw)}</div>`)}
        ${xcard('p-' + key, 'карточка параметра', () => paramCard(key))}</div>`;
    };
    const mult = v => {
      const pc = Math.round((v - 1) * 100);
      if (pc === 0) return 'Базовое значение: ЭБУ не корректирует подачу топлива в этом режиме.';
      return pc > 0 ? `ЭБУ добавляет ${pc} % топлива: смесь в этом режиме была бедной (подсос воздуха, слабый бензонасос, загрязнённые форсунки, ошибка расходомера).`
        : `ЭБУ убирает ${-pc} % топлива: смесь в этом режиме была богатой (давление топлива, подтекающие форсунки, продувка адсорбера, ошибка расходомера).`;
    };
    const add = v => Math.abs(v) < 0.05 ? 'Базовое значение: поправки на холостом ходу нет.'
      : v > 0 ? `ЭБУ добавляет ${v.toFixed(1)} кг/ч на холостом ходу: обычно подсос воздуха после расходомера или бедная смесь.`
      : `ЭБУ убирает ${(-v).toFixed(1)} кг/ч на холостом ходу: обычно богатая смесь (давление топлива, форсунки).`;
    const p = e.profile;
    // Units follow the identified ECU: Bosch kg/h; VDO and unknown vendor - the formula is not confirmed.
    const idle = p.vdo_units || p.vdo_candidate
      ? `<div class="tile" style="cursor:default"><div class="name">Холостой ход (добавочная поправка)</div><div class="val">—</div><div class="small">Для ЭБУ ${p.vdo_units ? 'VDO' : 'неизвестного производителя'} формула пересчёта не подтверждена: значение не показывается, чтобы не вводить в заблуждение.</div>${details('Подробнее', `<div class="small muted">исходный байт ${esc(a.raw.idle)}; 80 — середина шкалы</div>`)}</div>`
      : card('adapt_idle', 'Холостой ход (добавочная поправка)', a.idle_kg_h, 'кг/ч', -2, 2, '0,0', a.raw.idle, add(a.idle_kg_h));
    setHTML('#adBox', `<div class="small muted">Прочитано ${agoMin(a.age_ms)}</div>
      <div class="grid c3 mt">
        ${idle}
        ${card('adapt_lower', 'Частичная нагрузка, нижняя (множитель)', a.lower, '', 0.85, 1.15, '1,00', a.raw.lower, mult(a.lower))}
        ${card('adapt_upper', 'Частичная нагрузка, верхняя (множитель)', a.upper, '', 0.85, 1.15, '1,00', a.raw.upper, mult(a.upper))}
      </div>
      <p class="foot">Самоадаптация — поправка, которую ЭБУ выучил по сигналу лямбда-зонда. Добавочная действует на холостом ходу (кг/ч воздуха), множители — при частичной нагрузке (1,00 = без поправки, 1,07 = +7 % топлива). Нормы — заводские данные (Test and Adjustment Data); при выходе за них ЭБУ записывает неисправность регулирования смеси. Формулы пересчёта восстановлены по заводскому тестеру.</p>`);
  };
  $('#adBtn').addEventListener('click', async e => { await job('read_adapt', {}, e.target); render(); });
  $('#adRst').addEventListener('click', () => serviceFlow('adapt_reset'));
  loop(render, 5000);
};

// ---- guarded services: warning -> conditions -> password -> execute (D-035)
const SVC_WARN = {
  dtc_clear: 'Будут стёрты все записи памяти неисправностей. Восстановить их нельзя — перед стиранием сохраните отчёт («Отчёты»). Неисправности, которые есть сейчас, ЭБУ сразу запишет снова. Стирание не устраняет причину.',
  adapt_reset: 'ЭБУ забудет выученные поправки смеси (холостой ход и частичная нагрузка) и вернётся к базовым значениям. Пока он не обучится заново, двигатель может работать неровно; обучение — поездка 10–20 минут в разных режимах на прогретом двигателе. После сброса выключите зажигание минимум на 10 с.',
  purge_test: 'ЭБУ перейдёт в режим управления и по вашей команде будет открывать клапан продувки адсорбера при работающем двигателе (400–1500 об/мин, ОЖ 80–110 °C). Включение ограничено 30 с, затем исходное состояние восстанавливается. Не отходите от автомобиля и держите его на стояночном тормозе.',
  impulse_clear: 'Контакт диагностики будет удерживаться на массе несколько секунд — по заводской процедуре так стирается код, показанный последним. Остальные коды остаются.'
};
const SVC_STOPPED = {dtc_clear: 1, adapt_reset: 1, impulse_clear: 1};
async function serviceFlow(op) {
  const sv = (await api('services')).services;
  const o = sv.ops.find(x => x.id === op);
  const needBat = !(S.status && S.status.battery_v != null);
  sheet(`<h3>${esc(o.name)}</h3>
    ${o.allowed ? '' : `<div class="note danger">${esc(o.reason)}</div>`}
    <div class="note warn mt"><b>Что произойдёт.</b> ${esc(SVC_WARN[op] || '')}<br><span class="small">Если ЭБУ не подтвердит шаг, операция прекращается без повторов, а сервисные операции блокируются до квитирования.</span></div>
    ${needBat ? `<label class="row" style="padding:10px 0;background:none"><div class="grow small">Напряжение бортсети не ниже 11 В (АКБ заряжена или подключено зарядное)</div><span class="switch"><input type="checkbox" id="batCf"${sv.battery_manual_confirm ? ' checked' : ''}><span></span></span></label>` : ''}
    ${SVC_STOPPED[op] ? `<label class="row" style="padding:10px 0;background:none"><div class="grow small">Зажигание включено, двигатель заглушен</div><span class="switch"><input type="checkbox" id="stopCf"><span></span></span></label>` : ''}
    <div id="planBox" class="mt"></div>
    <div class="btns mt"><button class="btn sec" onclick="closeSheet()">Отмена</button><button class="btn" id="prepBtn" ${o.allowed ? '' : 'disabled'}>Проверить условия</button></div>`);
  const b = $('#batCf');
  if (b) b.addEventListener('change', () => post('settings', {battery_manual_confirm: b.checked ? 1 : 0}));
  $('#prepBtn').addEventListener('click', async e => {
    const sc = $('#stopCf');
    await job('svc_prepare', {op, confirm: sc && sc.checked ? 1 : 0}, e.target);
    const s2 = (await api('services')).services, p = s2.plan;
    const lock = s2.pw_lock_s > 0;
    $('#planBox').innerHTML = `<div class="card pad">${p.checks.map(c => `<div class="check"><i class="${c.result == null ? 'unk' : c.result ? 'ok' : 'no'}">${c.result == null ? '?' : c.result ? '✓' : '✕'}</i><div>${esc(c.text)}</div></div>`).join('')}</div>
      ${p.ok ? `<div class="mt small">Введите пароль Wi-Fi точки доступа <b>HFM Connect</b> (действует ${Math.round(p.expires_in_ms / 1000)} с):</div>
        <input class="field mt" id="svcPw" type="password" autocomplete="off" placeholder="Пароль точки доступа" ${lock ? 'disabled' : ''}>
        ${lock ? `<div class="note danger mt small">Ввод пароля заблокирован ещё на ${s2.pw_lock_s} с после 5 неверных попыток.</div>` : ''}
        <button class="btn red full mt" id="execBtn" ${lock ? 'disabled' : ''}>Выполнить</button>` : `<div class="note danger mt">Условия не выполнены: ${esc(p.block_reason || '')}</div>`}`;
    const x = $('#execBtn');
    if (x) x.addEventListener('click', async ev => {
      const pw = $('#svcPw').value;
      if (!pw) return toast('Введите пароль', true);
      if (!confirm('Выполнить «' + o.name + '»? Действие изменит данные ЭБУ.')) return;
      const ok = await job('svc_execute', {op, token: p.token, password: pw}, ev.target);
      $('#svcPw') && ($('#svcPw').value = '');
      const r = (await api('services')).services;
      if (!ok && r.plan && r.plan.ok) return;  // wrong password: the same plan stays, try again
      $('#planBox').innerHTML = `<div class="note ${ok ? 'info' : 'danger'}">${esc(r.last_result || '')}</div>`;
      if (op === 'purge_test' && ok) { closeSheet(); go('actuators'); }
    });
  });
}

// ---- actuators
// Only actuations whose byte sequence is known are offered; for the others the
// factory tester has screens, but which control telegram drives which part is
// not restored - sending one at random is not allowed (D-035).
const NOT_RESTORED = 'В заводском тестере есть экран этой проверки и общие телеграммы управления D0–D3, но какая из них каким механизмом управляет, по источникам не восстановлено. Отправлять их наугад нельзя: команда управления меняет режим работы двигателя.';
const ACTUATORS = [
  {id: 'purge_test', name: 'Клапан продувки адсорбера Y58/1', desc: 'Включение/выключение клапана командой ЭБУ, до 30 с, затем восстановление. Условия: 400–1500 об/мин, ОЖ 80–110 °C. Последовательность — общий шаблон заводского тестера.'},
  {name: 'Насос вторичного воздуха', block: NOT_RESTORED, live: 'Состояние видно в Live Data: «Вторичный воздух (AIR)».'},
  {name: 'Рециркуляция ОГ (EGR)', block: NOT_RESTORED, live: 'Неисправности EGR (P0400/P1400) фиксирует отдельный модуль DM, не HFM.'},
  {name: 'Регулирование распредвала Y49', block: NOT_RESTORED, live: 'Состояние — в Live Data: «Регулирование распредвала» (M104.995: < 2000 выкл, > 2000 вкл, > 4000 выкл).'},
  {name: 'Резонансная заслонка Y22/6', block: NOT_RESTORED, live: 'Состояние — в Live Data: «Резонансная заслонка» (ХХ выкл / > 4000 об/мин вкл).'},
  {name: 'Подогрев впускного коллектора', block: NOT_RESTORED, live: 'Состояние — в Live Data: «Впускной коллектор (частичная нагрузка)».'},
  {name: 'Форсунки (отключение по цилиндрам)', block: NOT_RESTORED, live: 'Проверка по цилиндрам — по изменению оборотов на ХХ при снятии разъёма форсунки (заводская методика).'},
  {name: 'Катушки зажигания', block: 'Отдельной проверки катушек у HFM-SFI в заводском тестере нет — только счётчики пропусков у поздних ЭБУ, их адреса не восстановлены.'},
  {name: 'Реле топливного насоса', block: 'Для HFM-SFI в источниках нет команды управления реле насоса. Проверка — по заводской методике (перемычка в реле, давление топлива).'}
];

R.actuators = () => {
  page('Исполнительные тесты', `<div id="actTop"></div><h2 class="section">Механизмы</h2><div class="list" id="actList"></div>
    <h2 class="section">Дроссельная заслонка и контакты</h2><div class="card pad" id="thrBox"></div>`);
  loop(async () => {
    const sv = (await api('services')).services;
    if (!$('#actList')) return;
    const purgeOp = sv.ops.find(o => o.id === 'purge_test');
    const chip = (on, t) => `<div class="flex wrap" style="gap:6px;margin:4px 0 2px"><span class="badge ${on ? 'b-REK' : 'b-gray'}">${t}</span></div>`;
    setHTML('#actList', ACTUATORS.map(a => a.id ? `<div class="row tap chev" onclick="serviceFlow('purge_test')"><div class="grow"><div class="t">${a.name}</div>${chip(purgeOp.allowed, purgeOp.allowed ? 'доступно' : 'недоступно')}<div class="s">${a.desc}</div><div class="s">${esc(purgeOp.allowed ? 'Перед запуском: предупреждение, проверка условий и пароль' : purgeOp.reason)}</div></div></div>`
      : `<div class="row"><div class="grow"><div class="t">${a.name}</div>${chip(false, 'недоступно')}<div class="s">${esc(a.block)}</div>${a.live ? `<div class="s">${esc(a.live)}</div>` : ''}</div></div>`).join(''));
    const res = sv.last_result;
    const testOn = res && res.startsWith('Режим управления');
    const top = $('#actTop');
    const topHtml = testOn ? `<div class="card pad"><div class="flex between"><b>Клапан продувки — режим управления</b><span class="badge ${sv.purge_on ? 'b-ZAV' : 'b-gray'}">${sv.purge_on ? 'ВКЛ' : 'ВЫКЛ'}</span></div>
      <div class="btns mt"><button class="btn green" onclick="job('purge_on',{},this)">Включить (≤30 с)</button><button class="btn sec" onclick="job('purge_off',{},this)">Выключить</button></div>
      <button class="btn red big-stop mt" onclick="job('stop',{},this)">STOP / восстановить</button></div>`
      : (res ? `<div class="note">${esc(res)}</div>` : '');
    setHTML(top, topHtml);
    let L = null;
    try { L = (await api('live')).live; } catch (e) {}
    if (!$('#thrBox')) return;
    const val = k => L && L.values.find(v => v.k === k);
    const t = val('throttle'), ctp = val('idle_contact'), wot = val('full_load');
    const fresh = L && L.age_ms < STALE_MS;
    setHTML('#thrBox', `<p class="small" style="margin-top:0">Проверка по Live Data, без управления приводом: 1) педаль отпущена — контакт ХХ ВКЛ, угол в норме ХХ; 2) плавно нажмите — угол растёт без скачков; 3) педаль до упора — контакт полной нагрузки ВКЛ.</p>
      ${fresh ? '' : '<div class="note small">Запустите Live Data — значения ниже обновляются только во время опроса.</div>'}
      ${kvTable([['Угол заслонки', t && t.v != null ? t.v.toFixed(2) + ' °' : '—'], ['Контакт ХХ', ctp && ctp.v != null ? (ctp.v ? 'ВКЛ' : 'выкл') : 'не опрашивается (включите «Экспериментальное чтение»)'],
        ['Контакт полной нагрузки', wot && wot.v != null ? (wot.v ? 'ВКЛ' : 'выкл') : '—']])}
      <div class="note mt small"><b>Обучение упора заслонки</b> — недоступно: отдельной команды нет, заводской тестер обучает упор в составе процедуры «Variantencodierung» (удаление исполнения и повторное обучение), байты которой не восстановлены. По заводской документации (M111 11/2) упор определяется ЭБУ автоматически и распознаётся заново после замены ЭБУ или привода; неисправности 133/138 сообщают о необученном упоре.</div>`);
  }, 1500);
};

// ---- commissioning wizard
R.commission = async () => {
  page('Ввод в эксплуатацию', '<div id="cmBox"></div>');
  if (!S.vehicles) S.vehicles = await api('vehicles');
  await loadSettings(true);
  const render = async () => {
    if (document.activeElement && document.activeElement.id === 'code9') return;  // do not disturb typing
    const [st, ecu] = await Promise.all([api('status'), api('ecu')]);
    let L = null, d = null;
    try { L = (await api('live')).live; } catch (e) {}
    try { d = (await api('dtc')).dtc; } catch (e) {}
    if (!$('#cmBox')) return;
    const v = st.vehicle;
    const veh = v ? S.vehicles.vehicles.find(x => x.idx === v.idx) : null;
    const gen = veh ? veh.gen : 'UNKNOWN';
    const fresh = L && L.age_ms < STALE_MS;
    const val = k => fresh && L.values.find(x => x.k === k);
    const rpm = val('rpm'), ect = val('ect');
    const step = (cls, title, body) => `<div class="step ${cls}"><div class="num"></div><div class="grow"><b>${title}</b><div class="small mt">${body}</div></div></div>`;
    const genText = {
      EARLY_AUTO: 'Ранний HFM 3.4 (до 02/1994, diagnosis version ≤ 42): комплектация, катализатор и тип КПП распознаются автоматически. Условия начального распознавания: U ≥ 11 В, скорость 0, двигатель остановлен, P/N, контакт ХХ замкнут; распознавание КПП требует поездки > 5 км/ч (OEM M111 11/2). На стенде процедура не может быть завершена.',
      TRANSITION: 'Переходный период 01–02/1994: поколение определяется только по идентификации ЭБУ, не по дате.',
      CODED: 'Кодируемый HFM 3.4 (с 03/1994, diagnosis ≥ 45): требуется 9-значный код комплектации, который вводится заводским тестером (меню 6 «Version coding»). Некодированный ЭБУ ограничивает обороты 3200 об/мин (MBNA 07.5/1). Перед заменой считайте код старого блока заводским тестером; без него — код по каталогу Gr. 54 и комплектации.',
      HFM41: 'HFM-SFI 4.1 (M104.995): отдельный состав Live Data и счётчиков; DAS-привязка возможна с MY1996.',
      UNKNOWN: 'Выберите автомобиль в разделе «Автомобили», чтобы получить порядок для поколения.'
    }[gen];
    const pre = [
      ['ЭБУ идентифицирован', ecu.ident.present],
      ['Напряжение ≥ 11 В', st.battery_v == null ? null : st.battery_v >= 11],
      ['Двигатель остановлен (n = 0)', rpm && rpm.v != null ? rpm.v === 0 : null],
      ['Температура ОЖ прочитана', ect && ect.v != null ? true : null]
    ];
    const snap = [['DTC', d && d.valid], ['Адаптации', ecu.adapt.valid], ['Кодировка', ecu.coding.valid]];
    const snapDone = snap.every(x => x[1]);
    const code9 = S.settings.code9 || '';
    setHTML('#cmBox', `
      <div class="note info">${esc(genText)}</div>
      <h2 class="section">Мастер</h2>
      <div class="list steps">
        ${step(veh ? 'done' : 'cur', 'Автомобиль и поколение', veh ? `${esc(veh.chassis)} ${esc(veh.name)} · M${esc(veh.engine)} · ${esc(veh.gen_name)}` : '<a href="#vehicles" onclick="go(\'vehicles\')">Выбрать автомобиль</a>')}
        ${step(ecu.ident.present ? 'done' : 'cur', 'Подключение и идентификация', ecu.ident.present ? `${esc(ecu.ident.part)} · diag ${esc(ecu.ident.diag_index)} · ${esc(ecu.profile.name)}` : '<button class="btn sm" onclick="job(\'connect\',{},this)">Подключить</button>')}
        ${step(pre.every(p => p[1]) ? 'done' : '', 'Предварительные условия', pre.map(p => `<div class="check"><i class="${p[1] == null ? 'unk' : p[1] ? 'ok' : 'no'}">${p[1] == null ? '?' : p[1] ? '✓' : '✕'}</i><div>${p[0]}</div></div>`).join('') + '<div class="muted">P/N и контакт ХХ проверьте визуально или в Live Data (запустите опрос).</div>')}
        ${step(snapDone ? 'done' : '', 'Снимок «до»', `${snap.map(x => `<div class="check"><i class="${x[1] ? 'ok' : 'unk'}">${x[1] ? '✓' : '?'}</i><div>${x[0]}: ${x[1] ? 'прочитано' : 'не прочитано'}</div></div>`).join('')}
          <div class="btns mt"><button class="btn sm" id="cmBefore">Прочитать всё и сохранить снимок</button></div>`)}
        ${step(gen === 'CODED' ? 'blocked' : '', '9-значный код комплектации', `Чтение/запись кода командой заводского тестера не восстановлены ${badge('BLOCK')}. Запишите код из заводского тестера или каталога для архива (сохраняется в устройстве и попадает в отчёт):
          <input class="field mt" id="code9" maxlength="9" inputmode="numeric" placeholder="9 цифр" value="${esc(code9)}">`)}
        ${step('', 'Дроссель и упоры', 'Проверка CTP/WOT/угла по Live Data — раздел «Исполнительные тесты». Автоматическое обучение не выполняется.')}
        ${step('', 'DAS / разрешение запуска', 'С MY1996 возможна постоянная привязка N3/4 к DAS: новый тестовый ЭБУ — не более 40 запусков до необратимой привязки (OEM M111 11/3). Обход и перепривязка не выполняются.')}
        ${step('', 'Контроль и отчёт', '<button class="btn sm" onclick="go(\'reports\')">Сформировать отчёт</button>')}
      </div>`);
    const c9 = $('#code9');
    if (c9 && !c9._b) {
      c9._b = 1;
      let t = 0;
      c9.addEventListener('input', () => {
        c9.value = c9.value.replace(/\D/g, '');
        S.settings.code9 = c9.value;
        clearTimeout(t);
        t = setTimeout(() => post('settings', {code9: c9.value}).catch(() => {}), 700);
      });
    }
    const bb = $('#cmBefore');
    if (bb && !bb._b) {
      bb._b = 1;
      bb.addEventListener('click', async e => {
        const btn = e.target;
        for (const j of ['read_dtc', 'read_adapt', 'read_coding']) await job(j, {}, btn, true);
        try {
          const r = await api('report', {}, 15000);
          r.manual = {code9: S.settings.code9 || '', created: new Date().toISOString(), kind: 'before'};
          await api('report/save?name=' + encodeURIComponent('before_' + (r.ident.part || 'ecu').replace(/\s/g, '')), {method: 'POST', body: JSON.stringify(r)}, 15000);
          toast('Снимок «до» сохранён в устройстве (Отчёты → файлы)');
        } catch (x) { toast(x.message, true); }
        render();
      });
    }
  };
  loop(render, 3000);
};


// ---- code reference (all namespaces; HFM digital codes are the only ones read over K-Line)
const NS = [
  ['hfm', 'HFM цифровые'], ['analog', 'Импульсные'], ['dm', 'DM N59/1'], ['cc', 'CC/ISC N4/3'], ['transport', 'HFM Connect']
];
const NS_NOTE = {
  hfm: 'Цифровые коды ЭБУ HFM-SFI (память F1, читаются по K-Line). Номер кода — не ISO P-код.',
  analog: 'Ранние импульсные коды 1–50 (вторичный источник Baum Tools). Не путать с цифровыми 001–164: номера разных пространств не переводятся друг в друга.',
  dm: 'Коды отдельного диагностического модуля DM N59/1. Через память F1 HFM они НЕ читаются; приведены только как справочник.',
  cc: 'Коды отдельного блока круиз-контроля / регулятора ХХ N4/3 (заводское руководство CC/ISC). Через F1 HFM не читаются.',
  transport: 'Ошибки самого HFM Connect (USB, FTDI, K-Line). Это не коды ЭБУ.'
};
R.codes = async () => {
  page('Справочник кодов', `<div id="nsSeg">${NS.map(n => `<button class="chip ${(S.ns || 'hfm') === n[0] ? 'on' : ''}" data-ns="${n[0]}">${n[1]}</button>`).join('')}</div>
    <input class="field mt" id="codeQ" placeholder="Поиск: номер или слово" value="${esc(S.codeQ || '')}">
    <label class="flex mt small" id="myBox"><input type="checkbox" id="onlyMine"${S.onlyMine ? ' checked' : ''}> только коды карты моего ЭБУ</label>
    <div class="note small mt" id="nsNote"></div><div class="list mt" id="codeList"><div class="row muted">Загрузка справочника…</div></div>
    <p class="foot" id="refFoot"></p>`);
  let ref, d = null;
  try { ref = S.ref = await getRef(); } catch (e) { setHTML('#codeList', `<div class="row">Справочник не загружен: ${esc(e.message)}</div>`); return; }
  try { d = (await api('dtc')).dtc; } catch (e) {}
  const render = () => {
    const ns = S.ns || 'hfm', q = (S.codeQ || '').toLowerCase().trim();
    $('#myBox').classList.toggle('hidden', ns !== 'hfm');
    setHTML('#nsNote', esc(NS_NOTE[ns]) + (ns === 'hfm' && d && d.dtc_map && d.dtc_map !== 'GENERIC' ? `<br>Карта вашего ЭБУ: <b>${esc(mapText(ref, d.dtc_map))}</b>.` : ''));
    let rows = [];
    if (ns === 'hfm') {
      rows = Object.keys(ref.dtc).map(Number).sort((a, b) => a - b).filter(c => c > 1).map(c => {
        const r = ref.dtc[c];
        if (S.onlyMine && d && d.dtc_map && d.dtc_map !== 'GENERIC' && !r.maps.includes(d.dtc_map)) return '';
        if (q && !(String(c).padStart(3, '0').includes(q) || r.t.toLowerCase().includes(q) || r.c.toLowerCase().includes(q))) return '';
        const x = {code: c};
        return `<div class="dtc"><div class="code ${r.lvl === 'GIP' ? 'int' : ''}">${String(c).padStart(3, '0')}</div><div class="grow"><div>${esc(r.t)}</div>
          <div class="small muted">${esc(DEF_STATUS[r.st] || r.st)}</div>${xcard('c-' + c, 'карточка ошибки', () => dtcCard(x, d))}</div></div>`;
      });
    } else {
      const list = ref[ns] || [];
      rows = list.map(a => {
        const code = a.code || a.id, text = a.t;
        if (q && !(String(code).toLowerCase().includes(q) || String(text).toLowerCase().includes(q))) return '';
        return `<div class="dtc"><div class="code ns${String(code).length > 4 ? ' long' : ''}">${esc(code)}</div><div class="grow"><div>${esc(text)}</div>
          ${xcard(ns + '-' + code, 'карточка ошибки', () => `<div class="flex wrap"><span class="badge b-gray">${esc(NS.find(n => n[0] === ns)[1])}</span></div>
            <div class="note small mt">${esc(NS_NOTE[ns])}</div>${xsec('Возможные причины', xtext(a.c))}${xsec('Что проверить', xtext(a.k))}${xsec('Источник', xtext(a.src))}`)}</div></div>`;
      });
    }
    rows = rows.filter(Boolean);
    setHTML('#codeList', rows.length ? rows.join('') : '<div class="row muted">Ничего не найдено.</div>');
    setHTML('#refFoot', `Справочник: ${esc(ref.meta.package)}. ${esc(ref.meta.warning)}`);
  };
  document.querySelectorAll('#nsSeg button').forEach(b => b.addEventListener('click', () => {
    S.ns = b.dataset.ns;
    document.querySelectorAll('#nsSeg button').forEach(x => x.classList.toggle('on', x === b));
    render();
  }));
  $('#codeQ').addEventListener('input', e => { S.codeQ = e.target.value; render(); });
  $('#onlyMine').addEventListener('change', e => { S.onlyMine = e.target.checked; render(); });
  render();
};

// ---- guided diagnostics by symptom
const SC_LINKS = {
  'S-01': ['connect', 'Подключение'], 'S-02': ['connect', 'Подключение'], 'S-03': ['live', 'Live Data'], 'S-04': ['live', 'Live Data'],
  'S-05': ['adapt', 'Адаптации'], 'S-06': ['adapt', 'Адаптации'], 'S-07': ['live', 'Live Data'], 'S-08': ['dtc', 'Ошибки'],
  'S-09': ['live', 'Live Data'], 'S-13': ['ecu', 'Идентификация'], 'S-14': ['ecu', 'Идентификация'], 'S-16': ['live', 'Live Data'],
  'S-17': ['live', 'Live Data'], 'S-18': ['dtc', 'Ошибки'], 'S-19': ['dtc', 'Ошибки'], 'S-20': ['connect', 'Подключение']
};
R.guide = async () => {
  page('Диагностика по симптомам', `<div class="note small">Подсказки по сочетанию кодов и Live Data из справочника — не автоматический диагноз и не замена заводского порядка проверки. Если нужного значения нет, вывод не делается.</div>
    <div class="list mt" id="scList"><div class="row muted">Загрузка…</div></div>
    <h2 class="section">Правила качества данных</h2><div class="list" id="qList"></div>`);
  let ref;
  try { ref = S.ref = await getRef(); } catch (e) { setHTML('#scList', `<div class="row">Справочник не загружен: ${esc(e.message)}</div>`); return; }
  setHTML('#scList', ref.scenarios.map(s => {
    const l = SC_LINKS[s.id];
    return `<div class="dtc"><div class="code" style="background:rgba(0,122,255,.1);color:var(--accent)">${esc(s.id.slice(2))}</div><div class="grow"><div>${esc(s.symptom)}</div>
      ${xcard('sc-' + s.id, 'карточка сценария', () => `${xsec('Что контролировать', xtext(s.inputs))}${xsec('Порядок действий', xtext(s.actions))}${l ? `<button class="btn sm sec mt" onclick="go('${l[0]}')">Открыть: ${l[1]}</button>` : ''}`)}</div></div>`;
  }).join(''));
  setHTML('#qList', ref.quality.map(q => `<div class="row"><div class="grow"><div class="t small"><b>${esc(q.id)}</b> — ${esc(q.trigger)}</div>
    ${xcard('q-' + q.id, 'подробнее', () => `${xsec('Действие', xtext(q.action))}${xsec('Основание', xtext(q.evidence))}`)}</div></div>`).join(''));
};

// ---- reports
R.reports = async () => {
  page('Отчёты', `<div class="btns"><button class="btn" id="mkRep">Сформировать отчёт</button><a class="btn sec" href="/api/report?dl=1">JSON</a><a class="btn sec" href="/api/trace.jsonl" download>RAW трасса</a></div>
    <div id="repBox" class="mt"></div><h2 class="section">Файлы на устройстве</h2><div id="fileBox"></div>`);
  $('#mkRep').addEventListener('click', e => buildReport(e.target));
  loadFiles();
};

async function loadFiles() {
  const f = await api('files');
  if (!$('#fileBox')) return;
  $('#fileBox').innerHTML = `<div class="list">${f.files.length ? f.files.map(x => `<div class="row"><div class="grow"><div class="t mono">${esc(x.path)}</div><div class="s">${(x.size / 1024).toFixed(1)} КБ</div></div>
    <a class="btn sm sec" href="/api/file?path=${encodeURIComponent(x.path)}">⇩</a><button class="btn sm sec" data-del="${esc(x.path)}">✕</button></div>`).join('') : '<div class="row muted">Нет файлов. Запись Live Data — в разделе Live Data.</div>'}</div>
    <p class="foot">Занято ${(f.used / 1024).toFixed(0)} из ${(f.total / 1024).toFixed(0)} КБ (LittleFS).</p>`;
  document.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', async () => {
    if (!confirm('Удалить ' + b.dataset.del + '?')) return;
    await post('file/delete', {path: b.dataset.del});
    loadFiles();
  }));
}

async function buildReport(btn) {
  let label;
  if (btn) { label = btn.innerHTML; btn.disabled = true; btn.innerHTML = '<span class="spin"></span>'; }
  let r, set;
  try {
    [r, set] = await Promise.all([api('report', {}, 15000), loadSettings(true)]);
  } catch (e) {
    toast(e.message, true);
    return;
  } finally {
    if (btn) { btn.disabled = false; btn.innerHTML = label; }
  }
  const st = r.status, id = r.ident, d = r.dtc, L = r.live;
  const code9 = set.code9 || '';
  const live = L.values.filter(v => v.v != null);
  const liveAge = L.age_ms < 4e9 ? `снимок ${ago(L.age_ms)}` : '';
  const html = `<div class="card pad" id="repCard">
    <div><div style="font-size:22px;font-weight:700">Отчёт диагностики HFM-SFI</div><div class="muted small">${new Date().toLocaleString('ru-RU')} · ${esc(r.generator)}</div></div>
    <div class="note warn mt small">${esc(r.disclaimer)}</div>
    <h2 class="section" style="margin-left:0">Автомобиль и ЭБУ</h2>
    ${kvTable([['Автомобиль', st.vehicle ? `${st.vehicle.chassis} ${st.vehicle.name} · M${st.vehicle.engine}` : 'не выбран'],
      ['ЭБУ', id.present ? `${id.part} · ${id.identifier}${id.vendor_known ? ' · ' + (VENDOR[id.vendor] || id.vendor) : ''}` : 'не идентифицирован'], ['Диагностический индекс', id.diag_index || '—'],
      ['HW / SW', `${id.hw_date || '—'} / ${id.sw_date || '—'}`], ['Профиль', r.profile.name], ['RAW ID', `<span class="hex">${esc(id.raw)}</span>`, true],
      ['9-значный код (введён вручную)', code9 || '—']])}
    <h2 class="section" style="margin-left:0">Память неисправностей</h2>
    ${d.valid ? `<p class="small muted">Прочитано ${esc(ago(d.age_ms))}</p>` + (d.items.length ? `<div class="list">${d.items.map(x => dtcRow(x, d, false)).join('')}</div>` : '<p>Неисправностей нет.</p>') + `<p class="small muted">${esc(d.bank_text || '')} · A: <span class="hex">${esc(d.bank_a)}</span> · B: <span class="hex">${esc(d.bank_b)}</span></p>` : '<p class="muted">Не прочитана.</p>'}
    <h2 class="section" style="margin-left:0">Адаптации и кодировка</h2>
    ${r.adapt.unsupported ? '<p class="muted">Самоадаптации: этот ЭБУ их не выдаёт.</p>' : r.adapt.valid ? kvTable([['Холостой ход, добавочная', r.adapt.vdo || r.profile.vdo_candidate ? 'формула не подтверждена (байт ' + r.adapt.raw.idle + ')' : r.adapt.idle_kg_h + ' кг/ч'], ['Частичная нагрузка, нижняя', r.adapt.lower], ['Частичная нагрузка, верхняя', r.adapt.upper]]) : '<p class="muted">Адаптации не прочитаны.</p>'}
    ${r.coding.unsupported ? '<p class="muted small">Кодировка: этот ЭБУ её не выдаёт.</p>' : r.coding.valid ? `<p class="small">Кодировка c=${esc(r.coding.coding_byte)} m=${esc(r.coding.mask_byte)}</p>` : '<p class="muted small">Кодировка не прочитана.</p>'}
    <h2 class="section" style="margin-left:0">Live Data (последний снимок)</h2>
    ${live.length ? `<p class="small muted">${esc(liveAge)}</p><table class="kv">${live.map(v => `<tr><td>${esc(v.name)}</td><td>${v.bool ? (v.v ? 'ВКЛ' : 'выкл') : v.v + ' ' + esc(v.unit)}${v.lo != null ? ` <span class="muted small">норма ${v.lo}…${v.hi}</span>` : ''}${v.norm === 'BELOW' ? ' <b style="color:var(--red)">↓</b>' : v.norm === 'ABOVE' ? ' <b style="color:var(--red)">↑</b>' : ''}</td></tr>`).join('')}</table>` : '<p class="muted">Live Data в этом сеансе не опрашивались.</p>'}
    ${r.impulse.codes.length ? `<p>Импульсные коды N: <b>${r.impulse.codes.map(c => c.n).join(', ')}</b></p>` : ''}
    </div><div class="btns mt noprint"><button class="btn" onclick="window.print()">Печать / PDF</button><button class="btn sec" id="saveRep">Сохранить на устройстве</button></div>`;
  $('#repBox').innerHTML = html;
  $('#saveRep').addEventListener('click', async () => {
    r.manual = {code9, created: new Date().toISOString()};
    try {
      await api('report/save?name=' + encodeURIComponent((id.part || 'ecu').replace(/\s/g, '')), {method: 'POST', body: JSON.stringify(r)}, 15000);
      toast('Отчёт сохранён');
      loadFiles();
    } catch (e) { toast(e.message, true); }
  });
}

// ---- settings
R.settings = async () => {
  const s = await loadSettings(true);
  const sw = (k, title, sub, danger) => `<label class="row"><div class="grow"><div class="t" ${danger ? 'style="color:var(--red)"' : ''}>${title}</div>${sub ? `<div class="s">${sub}</div>` : ''}</div><span class="switch"><input type="checkbox" data-k="${k}" ${s[k] ? 'checked' : ''}><span></span></span></label>`;
  const swv = (k, on, title, sub) => `<label class="row"><div class="grow"><div class="t">${title}</div>${sub ? `<div class="s">${sub}</div>` : ''}</div><span class="switch"><input type="checkbox" data-k="${k}" ${on ? 'checked' : ''}><span></span></span></label>`;
  const theme = s.ui_theme || LS.get('theme', 'auto');
  page('Настройки', `
    <h2 class="section">Оформление</h2>
    <div class="card pad"><div class="seg" id="thSeg">${[['auto', 'Авто'], ['light', 'Светлая'], ['dark', 'Тёмная']].map(t => `<button data-t="${t[0]}" class="${theme === t[0] ? 'on' : ''}">${t[1]}</button>`).join('')}</div></div>
    <h2 class="section">Диагностика</h2>
    <div class="list">
      ${sw('experimental_reads', 'Экспериментальное чтение', 'Дополнительные параметры (скорость, нижний лямбда-зонд, контакты ХХ и стартера, корректирующий штекер), коды 129–152, шаблоны VDO и чтение у нераспознанных ЭБУ. Адреса и формулы не подтверждены на реальном ЭБУ; ответы проверяются.')}
      ${sw('auto_connect', 'Автоподключение', 'Подключаться к ЭБУ автоматически и восстанавливать связь после обрыва')}
      ${sw('dtc_on_connect', 'Читать ошибки при подключении', 'Читать память неисправностей (только чтение) сразу после подключения и заново после автоматического восстановления связи — показанный результат всегда относится к текущему соединению')}
      ${sw('keep_alive', 'Keep-alive (F0:00)', 'Безопасное чтение раз в 0,7 с при простое')}
      ${sw('trace_capture', 'RAW журнал TX/RX', 'Кольцевой буфер в ОЗУ (1536 событий)')}
      ${sw('mask_vin', 'Скрывать VIN в отчётах', '')}
    </div>
    <h2 class="section">Индикация</h2>
    <div class="card pad">
      <div class="small muted">Звук (пьезоизлучатель)${s.buzzer_fitted ? '' : ' — не задан в Config.h'}</div>
      <div class="seg mt" id="sndSeg">${LEVELS.map((t, i) => `<button data-v="${i}" class="${s.sound_level === i ? 'on' : ''}">${t}</button>`).join('')}</div>
      <div class="small muted mt2">Яркость светодиода${s.led_fitted ? '' : ' — внешний RGB не задан в Config.h'}</div>
      <div class="seg mt" id="ledSeg">${LEVELS.map((t, i) => `<button data-v="${i}" class="${s.light_level === i ? 'on' : ''}">${t}</button>`).join('')}</div>
      <div class="btns mt"><button class="btn sm sec" data-test="sound">Проверить звук</button><button class="btn sm sec" data-test="dtc">Звук «ошибки»</button><button class="btn sm sec" data-test="light">Проверить цвета</button></div>
      <p class="small muted">Звуки: подключение — восходящий аккорд; восстановление — короткая трель вверх; отключение и обрыв — нисходящие; нет ошибок — арпеджио вверх; есть ошибки — двойное «динь-дон»; адаптер подключён/отключён — короткий щелчок. Цвета: синее дыхание — поиск ЭБУ, зелёный — на связи, бирюзовый со вспышками — опрос Live Data, двойная янтарная вспышка — в памяти есть ошибки, янтарное дыхание — восстановление, красное — нет адаптера / ошибка.</p></div>
    ${s.oled && s.oled.built ? `<h2 class="section">Дисплей OLED</h2>
    <div class="list">
      ${swv('oled_on', s.oled.on, 'Дисплей включён', s.oled.present ? 'Найден на шине I²C по адресу ' + esc(s.oled.address) : 'Не обнаружен на шине I²C — сканер работает без него')}
      ${swv('oled_flip', s.oled.flip, 'Повернуть на 180°', 'Если дисплей установлен «вверх ногами»')}
    </div>
    <div class="card pad mt">
      <div class="small muted">Контроллер дисплея</div>
      <div class="seg mt" id="oledType">${['SSD1306', 'SH1106'].map((t, i) => `<button data-v="${i}" class="${s.oled.type === i ? 'on' : ''}">${t}</button>`).join('')}</div>
      <div class="small muted mt2">Яркость</div>
      <div class="seg mt" id="oledLevel">${['Низкая', 'Средняя', 'Высокая'].map((t, i) => `<button data-v="${i + 1}" class="${s.oled.level === i + 1 ? 'on' : ''}">${t}</button>`).join('')}</div>
      <p class="small muted">Если изображение сдвинуто или справа полоса «мусора» — выберите другой контроллер (у дисплеев 0,96" обычно SSD1306, у 1,3" — SH1106). Дисплей только показывает состояние: команд ЭБУ он не отправляет.</p>
    </div>` : ''}
    <h2 class="section">Сервисные операции</h2>
    <div class="list">
      ${sw('engineer_mode', 'Технические подробности', 'Раскрывать в карточках коды команд, адреса, байты и уровни подтверждения')}
      ${sw('require_write_key', 'Требовать ключ WRITE-EN', 'Если на устройстве установлен аппаратный ключ (GPIO10 на массу): операции записи дополнительно требуют его замкнуть. Без этой настройки защиту обеспечивают предупреждение, проверка условий и пароль точки доступа.')}
      ${sw('impulse_enabled', 'Импульсный считыватель', 'Замыкает диагностическую линию на массу через адаптер. Только для ЭБУ до конца 1995 модельного года; не для W210.', true)}
    </div>
    <div class="card pad mt"><div class="grid c3"><div><div class="small muted">Нажатие для чтения, мс (2000–4000)</div><input class="field" id="impPr" type="number" value="${s.imp_press_ms}"></div>
      <div><div class="small muted">Пауза конца кода, мс</div><input class="field" id="impGap" type="number" value="${s.imp_end_gap_ms}"></div>
      <div><div class="small muted">Удержание для стирания, мс (5000–8000)</div><input class="field" id="impClr" type="number" value="${s.imp_clear_ms || 7000}"></div></div>
      <p class="small muted">Заводская процедура: стирание — удержание 6–8 с после показа кода (Bosch до 08/1993 — 5–6 с).</p></div>
    <h2 class="section">Тайминги K-Line</h2>
    <div class="card pad"><div class="grid c3">
      <div><div class="small muted">BREAK, мс</div><input class="field" id="tBrk" type="number" value="${s.break_ms}"></div>
      <div><div class="small muted">Пауза после BREAK, мс</div><input class="field" id="tPb" type="number" value="${s.post_break_ms}"></div>
      <div><div class="small muted">Тайм-аут эхо+ответ, мс</div><input class="field" id="tTo" type="number" value="${s.byte_timeout_ms}"></div></div>
      <div class="grid c2 mt"><div><div class="small muted">DTR / RTS адаптера FTDI</div><select class="field" id="tModem">
        ${[[0, 'выключены (по умолчанию)'], [1, 'DTR вкл'], [2, 'RTS вкл'], [3, 'DTR и RTS вкл']].map(([v, t]) => `<option value="${v}"${s.ftdi_modem === v ? ' selected' : ''}>${t}</option>`).join('')}</select></div>
        <div><div class="small muted">Автоподбор</div><label class="flex"><input type="checkbox" id="tAuto"${s.wake_auto ? ' checked' : ''}> пробовать BREAK 165/160/130 мс и DTR/RTS, пока ЭБУ не ответил ни разу</label></div></div>
      <div class="btns mt"><button class="btn sec" id="tSave">Сохранить</button><button class="btn sec" id="tDef">По умолчанию (177/214/750)</button></div>
      <p class="small muted">${s.wake_learned ? 'Рабочий профиль пробуждения найден на автомобиле и сохранён в устройстве; изменение этих полей его сбрасывает.' : 'Рабочий профиль пробуждения ещё не найден.'}</p></div>
    <h2 class="section">Wi-Fi</h2>
    <div class="card pad">
      <div class="small muted">Точка доступа: <b>${esc(s.ap_ssid)}</b>${s.ap_default_pass ? ' · используется пароль по умолчанию — смените его: этот же пароль подтверждает стирание ошибок и другие операции записи' : ''}</div>
      <input class="field mt" id="apPass" type="password" placeholder="Новый пароль точки доступа (≥ 8 символов)">
      <div class="small muted mt">Подключение к сети мастерской (необязательно)</div>
      <input class="field mt" id="staSsid" placeholder="SSID" value="${esc(s.sta_ssid)}">
      <input class="field mt" id="staPass" type="password" placeholder="Пароль">
      <button class="btn full mt" id="netSave">Сохранить сеть</button>
      <p class="small muted">Адрес: http://192.168.4.1/ или http://hfm.local/</p></div>`);
  [['#sndSeg', 'sound_level'], ['#ledSeg', 'light_level'], ['#oledType', 'oled_type'], ['#oledLevel', 'oled_level']].forEach(([sel, key]) => document.querySelectorAll(sel + ' button').forEach(b => b.addEventListener('click', async () => {
    document.querySelectorAll(sel + ' button').forEach(x => x.classList.toggle('on', x === b));
    try { await post('settings', {[key]: b.dataset.v}); if (key === 'sound_level' && b.dataset.v !== '0') post('indicator/test', {what: 'sound'}).catch(() => {}); } catch (e) { toast(e.message, true); }
  })));
  document.querySelectorAll('[data-test]').forEach(b => b.addEventListener('click', () => {
    const w = b.dataset.test;
    post('indicator/test', w === 'dtc' ? {what: 'dtc', faults: 1} : {what: w}).catch(e => toast(e.message, true));
  }));
  document.querySelectorAll('#thSeg button').forEach(b => b.addEventListener('click', async () => {
    applyTheme(b.dataset.t);
    document.querySelectorAll('#thSeg button').forEach(x => x.classList.toggle('on', x === b));
    try { await post('settings', {ui_theme: b.dataset.t}); } catch (e) { toast(e.message, true); }
  }));
  document.querySelectorAll('input[data-k]').forEach(i => i.addEventListener('change', async () => {
    if (i.checked && i.dataset.k === 'impulse_enabled' &&
      !confirm('Включить «' + i.closest('.row').querySelector('.t').textContent + '»? Это разрешает операции, влияющие на ЭБУ/линию.')) { i.checked = false; return; }
    try { await post('settings', {[i.dataset.k]: i.checked ? 1 : 0}); toast('Сохранено'); } catch (e) { toast(e.message, true); i.checked = !i.checked; }
  }));
  ['impPr', 'impGap', 'impClr'].forEach(id => $('#' + id).addEventListener('change', () => post('settings', {imp_press_ms: $('#impPr').value, imp_end_gap_ms: $('#impGap').value, imp_clear_ms: $('#impClr').value}).then(() => toast('Сохранено'), e => toast(e.message, true))));
  const saveT = async v => { try { await post('settings', v); toast('Тайминги сохранены'); } catch (e) { toast(e.message, true); } R.settings(); };
  $('#tSave').addEventListener('click', () => saveT({break_ms: $('#tBrk').value, post_break_ms: $('#tPb').value, byte_timeout_ms: $('#tTo').value, ftdi_modem: $('#tModem').value, wake_auto: $('#tAuto').checked ? 1 : 0}));
  $('#tDef').addEventListener('click', () => saveT({break_ms: 177, post_break_ms: 214, byte_timeout_ms: 750, ftdi_modem: 0, wake_auto: 1}));
  $('#netSave').addEventListener('click', async () => {
    const p = {sta_ssid: $('#staSsid').value, sta_pass: $('#staPass').value};
    if ($('#apPass').value) {
      if ($('#apPass').value.length < 8) return toast('Пароль ≥ 8 символов', true);
      p.ap_pass = $('#apPass').value;
    }
    try { await post('settings', p); toast('Сеть перезапускается — переподключитесь к Wi-Fi «' + s.ap_ssid + '»', false, 6000); } catch (e) { toast(e.message, true); }
  });
};

// ---------------------------------------------------------------- boot
buildNav();
statusLoop();
getRef().then(r => { S.ref = r; }).catch(() => {});
loadSettings().catch(() => {}).finally(() => go(location.hash.slice(1) || 'home'));
