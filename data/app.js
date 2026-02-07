const state = {
  status: null,
  time: null,
  clock: { baseLocal: 0, baseMs: 0, valid: false },
  config: null,
  schedule: null,
  ota: null,
  history: null,
  windows: [],
  wifiModal: { open: false, ssid: "", secure: true },
  redirect: { ip: "", startedAtMs: 0 },
};

function $(id) {
  return document.getElementById(id);
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function toast(msg) {
  const el = $("toast");
  if (!el) return;
  el.textContent = msg;
  el.style.display = "block";
  clearTimeout(toast._t);
  toast._t = setTimeout(() => {
    el.style.display = "none";
  }, 2800);
}

async function apiGet(path) {
  const res = await fetch(path, { cache: "no-store" });
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  if (!res.ok) {
    const err = new Error((data && data.error) || text || `HTTP ${res.status}`);
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

async function apiPost(path, body) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  if (!res.ok) {
    const err = new Error((data && data.error) || text || `HTTP ${res.status}`);
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text;
}

function setHtml(id, html) {
  const el = $(id);
  if (el) el.innerHTML = html;
}

function setPill(id, text, tone) {
  const el = $(id);
  if (!el) return;
  el.textContent = text;
  el.classList.remove("good", "warn", "bad");
  if (tone) el.classList.add(tone);
}

function fmtLocal(epochLocal) {
  if (!epochLocal) return "—";
  const d = new Date(epochLocal * 1000);
  return d.toLocaleString("he-IL", { hour12: false, timeZone: "UTC" });
}

let _hebrewFmt = null;
function getHebrewFormatter() {
  if (_hebrewFmt) return _hebrewFmt;
  try {
    _hebrewFmt = new Intl.DateTimeFormat("he-IL-u-ca-hebrew-nu-hebr", {
      timeZone: "UTC",
      day: "numeric",
      month: "long",
      year: "numeric",
    });
    return _hebrewFmt;
  } catch {
    _hebrewFmt = null;
    return null;
  }
}

function fmtHebrewShort(epochLocal) {
  if (!epochLocal) return "—";
  const d = new Date(epochLocal * 1000);
  try {
    const fmt = getHebrewFormatter();
    if (!fmt) return "—";
    // Prefer parts so we can render: "י״ט שבט תשפ״ו" (no weekday / no "ב" prefix).
    if (typeof fmt.formatToParts === "function") {
      const parts = fmt.formatToParts(d);
      const day = parts.find((p) => p.type === "day")?.value || "";
      let month = parts.find((p) => p.type === "month")?.value || "";
      const year = parts.find((p) => p.type === "year")?.value || "";
      month = month.replace(/^ב/, "");
      const out = `${day} ${month} ${year}`.replace(/\s+/g, " ").trim();
      return out || "—";
    }
    // Fallback: best-effort string cleanup.
    return String(fmt.format(d) || "")
      .replace(/\s+/g, " ")
      .replace(/\sב/g, " ")
      .trim();
  } catch {
    return "—";
  }
}

function setClockBaseFromDevice(timeObj) {
  const valid = !!timeObj?.valid;
  const local = Number(timeObj?.local || 0);
  state.clock.valid = valid && local > 0;
  state.clock.baseLocal = state.clock.valid ? local : 0;
  state.clock.baseMs = Date.now();
}

function clockLocalNow() {
  if (!state.clock.valid || !state.clock.baseLocal) return 0;
  const delta = Math.floor((Date.now() - state.clock.baseMs) / 1000);
  return state.clock.baseLocal + Math.max(0, delta);
}

function toLocalEpochFromUtc(epochUtc) {
  if (!epochUtc) return 0;
  const off = state.time?.tzOffsetSeconds || 0;
  return epochUtc + off;
}

function fmtUtcAsLocal(epochUtc) {
  return fmtLocal(toLocalEpochFromUtc(epochUtc));
}

function isoLocalFromEpochLocal(epochLocal) {
  if (!epochLocal) return "";
  const d = new Date(epochLocal * 1000);
  return d.toISOString().slice(0, 16);
}

function suffix4FromMac(mac) {
  const clean = String(mac || "")
    .toUpperCase()
    .replace(/[^0-9A-F]/g, "");
  return clean.length >= 4 ? clean.slice(clean.length - 4) : "0000";
}

function defaultSmartName() {
  const mac = state.status?.wifi?.mac || "";
  return `SmartShabat-${suffix4FromMac(mac)}`;
}

function buildNetBadge(wifi) {
  if (!wifi) return "—";
  if (wifi.staSsid) return `Wi‑Fi: ${wifi.staSsid}`;
  if (wifi.apMode) return `Hotspot: ${wifi.apSsid || ""}`.trim();
  return "לא מחובר";
}

function wifiStatusMessage(code) {
  const c = Number(code || 0);
  switch (c) {
    case 6:
      return "סיסמה שגויה";
    case 4:
      return "ההתחברות נכשלה";
    case 1:
      return "הרשת לא נמצאה";
    case 5:
      return "החיבור אבד";
    case 7:
      return "מנותק";
    case 3:
      return "מחובר";
    case 0:
      return "מנסה להתחבר…";
    default:
      return "בעיה בהתחברות";
  }
}

function computeHealthLine(st) {
  if (!st) return "—";
  if (!state.time?.valid) return "צריך לכוון שעה";
  if (!st.schedule?.hasZmanim) return "חסר לוח זמנים";
  if (st.schedule?.errorCode === "CLOCK_NOT_SET") return "צריך לכוון שעה";
  if (!st.schedule?.ok) return "בעיה בלוח שבת/חג";
  return "מוכן";
}

function modeLabel(runMode) {
  const m = Number(runMode || 0);
  if (m === 1) return "חול";
  if (m === 2) return "שבת";
  return "אוטומטי";
}

function computeModeState() {
  const st = state.status;
  if (!st) return "—";
  const rm = st.operation?.runMode ?? state.config?.operation?.runMode ?? 0;
  return modeLabel(rm);
}

function buildNetHint(wifi) {
  if (!wifi) return "—";
  if (wifi.apMode && !wifi.staSsid) {
    return `Hotspot פעיל: ${wifi.apSsid || ""}. מתחברים אליו כדי להגדיר Wi‑Fi.`;
  }
  if (wifi.apMode && wifi.staSsid) {
    return `Hotspot פעיל להגדרה · מחובר גם ל‑Wi‑Fi: ${wifi.staSsid}.`;
  }
  if (!wifi.staSsid) {
    return "לא מחובר ל‑Wi‑Fi. אם אין רשת זמינה, יופעל Hotspot.";
  }
  return `מחובר ל‑Wi‑Fi: ${wifi.staSsid} · IP ${wifi.staIp || wifi.ip || ""}`.trim();
}

function renderNextChange(st) {
  if (!st?.schedule?.ok || !st.schedule?.nextChangeLocal) return "—";
  const label = st.schedule.nextStateOn ? "הדלקה" : "כיבוי";
  const when = fmtLocal(st.schedule.nextChangeLocal);
  const h = fmtHebrewShort(st.schedule.nextChangeLocal);
  return `${label} · ${when}${h !== "—" ? ` (${h})` : ""}`;
}

function renderClockInfo() {
  const st = state.status;
  const tm = state.time;
  if (!st) return;

  const clockOk = !!tm?.valid;
  setPill("clockPill", clockOk ? "תקין" : "לא מכוון", clockOk ? "good" : "warn");

  const src = tm?.source || "";
  setText("clockSource", src === "ntp" ? "אוטומטי" : src === "manual" ? "ידני" : "—");

  const lastNtp = tm?.lastNtpSyncUtc || 0;
  const lastManual = tm?.lastManualSetUtc || 0;
  const last = Math.max(lastNtp || 0, lastManual || 0);
  setText("clockLastSync", last ? fmtUtcAsLocal(last) : "—");

  const dstMode = Number(tm?.dstMode ?? 1);
  const dstActive = !!tm?.dstActive;
  const dstModeText = dstMode === 0 ? "כבוי" : dstMode === 2 ? "ידני" : "אוטומטי";
  setText("dstState", `${dstModeText}${dstMode === 0 ? "" : dstActive ? " · פעיל" : " · לא פעיל"}`);

  const next = Number(tm?.nextDstChangeLocal || 0);
  setText("dstNext", next ? fmtLocal(next) : "—");

  const hint = computeHealthLine(st);
  setText("clockHint", hint === "מוכן" ? (clockOk ? "מכוון" : "לא מכוון") : hint);
}

function renderClockTick() {
  const localNow = clockLocalNow();
  setText("nowTime", localNow ? fmtLocal(localNow) : "—");
  setText("nowHebrewDate", localNow ? fmtHebrewShort(localNow) : "—");

  // Prefill manual clock input to current local time
  if (state.time?.valid && $("manualTime") && document.activeElement !== $("manualTime")) {
    $("manualTime").value = isoLocalFromEpochLocal(localNow || 0);
  }
}

function renderStatus() {
  const st = state.status;
  if (!st) return;

  const wifi = st.wifi || {};
  const ip = wifi.staIp || wifi.apIp || wifi.ip || "";
  const host = wifi.hostName || defaultSmartName();

  setText("deviceMeta", `${host}${ip ? ` · ${ip}` : ""}`);
  setText("relayState", st.relay?.on ? "דלוק" : "כבוי");
  setText("modeState", computeModeState());
  setText("nextChange", renderNextChange(st));

  setText("netBadge", buildNetBadge(wifi));
  setText("netHint", buildNetHint(wifi));

  setText("healthLine", computeHealthLine(st));

  const holy = st.schedule?.ok && st.schedule?.inHolyTime;
  setPill("holyPill", holy ? "שבת/חג" : "חול", holy ? "warn" : "good");

  renderClockInfo();
}

function renderUpcoming(upcoming) {
  const box = $("upcomingList");
  if (!box) return;
  if (!Array.isArray(upcoming) || upcoming.length === 0) {
    box.innerHTML = `<div class="muted">—</div>`;
    return;
  }
  box.innerHTML = "";
  for (const w of upcoming.slice(0, 8)) {
    const el = document.createElement("div");
    el.className = "item";
    const title = w.title || w.label || "—";
    el.innerHTML = `
      <div class="left">
        <div class="title">${title}</div>
        <div class="sub">${fmtLocal(w.startLocal)} → ${fmtLocal(w.endLocal)}</div>
        <div class="sub">${fmtHebrewShort(w.startLocal)} → ${fmtHebrewShort(w.endLocal)}</div>
      </div>
      <div class="pill">${w.kind === 1 ? "שבת" : w.kind === 2 ? "חג" : "שבת/חג"}</div>
    `;
    box.appendChild(el);
  }
}

function renderNextWindow(upcoming) {
  const box = $("nextWindow");
  if (!box) return;
  if (!Array.isArray(upcoming) || upcoming.length === 0) {
    box.innerHTML = `<div class="muted">—</div>`;
    return;
  }
  const w = upcoming[0];
  const inHoly = !!state.status?.schedule?.inHolyTime;
  const title = w.title || w.label || "—";
  box.innerHTML = `
    <div class="left">
      <div class="title">${inHoly ? "פעיל עכשיו" : "החלון הבא"} · ${title}</div>
      <div class="sub">${fmtLocal(w.startLocal)} → ${fmtLocal(w.endLocal)}</div>
      <div class="sub">${fmtHebrewShort(w.startLocal)} → ${fmtHebrewShort(w.endLocal)}</div>
    </div>
    <div class="pill">${w.kind === 1 ? "שבת" : w.kind === 2 ? "חג" : "שבת/חג"}</div>
  `;
}

function iconForKind(kind) {
  switch (kind) {
    case "relay":
      return "🔌";
    case "network":
      return "📶";
    case "clock":
      return "⏰";
    case "update":
      return "⬇️";
    case "boot":
      return "ℹ️";
    default:
      return "ℹ️";
  }
}

function renderHistory(items) {
  const box = $("historyList");
  if (!box) return;
  if (!Array.isArray(items) || items.length === 0) {
    box.innerHTML = `<div class="muted">אין היסטוריה עדיין</div>`;
    return;
  }
  box.innerHTML = "";
  for (const it of items.slice().reverse().slice(0, 30)) {
    const el = document.createElement("div");
    el.className = "item";
    el.innerHTML = `
      <div class="left">
        <div class="title">${iconForKind(it.kind)} ${it.msg || "—"}</div>
        <div class="sub">${fmtLocal(it.t || 0)}</div>
        <div class="sub">${fmtHebrewShort(it.t || 0)}</div>
      </div>
    `;
    box.appendChild(el);
  }
}

function renderWindowsList() {
  const box = $("windowsList");
  if (!box) return;
  if (!Array.isArray(state.windows) || state.windows.length === 0) {
    box.innerHTML = `<div class="muted">אין חלונות ידניים</div>`;
    return;
  }
  box.innerHTML = "";
  state.windows.forEach((w, idx) => {
    const el = document.createElement("div");
    el.className = "item";
    const action = w.on ? "הדלקה" : "כיבוי";
    const startLocal = toLocalEpochFromUtc(w.startUtc || 0);
    const endLocal = toLocalEpochFromUtc(w.endUtc || 0);
    el.innerHTML = `
      <div class="left">
        <div class="title">${action}</div>
        <div class="sub">${fmtUtcAsLocal(w.startUtc)} → ${fmtUtcAsLocal(w.endUtc)}</div>
        <div class="sub">${fmtHebrewShort(startLocal)} → ${fmtHebrewShort(endLocal)}</div>
      </div>
      <button class="btn danger" type="button">מחק</button>
    `;
    el.querySelector("button").onclick = () => {
      state.windows.splice(idx, 1);
      renderWindowsList();
    };
    box.appendChild(el);
  });
}

function clamp(n, min, max) {
  const v = Number(n);
  if (!Number.isFinite(v)) return min;
  return Math.min(max, Math.max(min, v));
}

function parseUtcFromDatetimeLocal(value) {
  if (!value) return 0;
  const t = new Date(value).getTime();
  if (!Number.isFinite(t)) return 0;
  return Math.floor(t / 1000);
}

function isValidIp(s) {
  const m = String(s || "").trim().match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
  if (!m) return false;
  for (let i = 1; i <= 4; i += 1) {
    const n = Number(m[i]);
    if (!Number.isFinite(n) || n < 0 || n > 255) return false;
  }
  return true;
}

function showStaticIp(show) {
  const grid = $("staticIpGrid");
  if (!grid) return;
  grid.style.display = show ? "" : "none";
}

function showApPassword(show) {
  const row = $("apPasswordRow");
  if (!row) return;
  row.style.display = show ? "" : "none";
}

function showDstManual(show) {
  const row = $("dstManualRow");
  if (!row) return;
  row.style.display = show ? "" : "none";
}

function applyConfigToUi(cfg) {
  // Status run-mode quick selector
  if ($("runModeQuick")) $("runModeQuick").value = String(cfg?.operation?.runMode ?? 0);

  // Network settings
  if ($("hostName")) $("hostName").value = cfg?.network?.hostName ?? "";
  if ($("apSsid")) $("apSsid").value = cfg?.network?.ap?.ssid ?? "";

  const dhcp = !!cfg?.network?.sta?.dhcp;
  if ($("ipMode")) $("ipMode").value = dhcp ? "dhcp" : "static";
  showStaticIp(!dhcp);
  if ($("staIp")) $("staIp").value = cfg?.network?.sta?.static?.ip ?? "";
  if ($("staGw")) $("staGw").value = cfg?.network?.sta?.static?.gateway ?? "";
  if ($("staMask")) $("staMask").value = cfg?.network?.sta?.static?.subnet ?? "";
  if ($("staDns1")) $("staDns1").value = cfg?.network?.sta?.static?.dns1 ?? "";
  if ($("staDns2")) $("staDns2").value = cfg?.network?.sta?.static?.dns2 ?? "";

  const apPasswordSet = !!cfg?.network?.ap?.passwordSet;
  if ($("apProtected")) $("apProtected").checked = apPasswordSet;
  showApPassword(apPasswordSet);
  if ($("apPassword")) $("apPassword").value = "";

  // Clock
  if ($("ntpEnabled")) $("ntpEnabled").checked = !!cfg?.time?.ntpEnabled;
  if ($("ntpServer")) $("ntpServer").value = cfg?.time?.ntpServer ?? "pool.ntp.org";
  if ($("ntpResync")) $("ntpResync").value = String(cfg?.time?.ntpResyncMinutes ?? 360);

  if ($("tzOffset")) $("tzOffset").value = String(cfg?.time?.tzOffsetMinutes ?? 120);
  if ($("dstMode")) $("dstMode").value = String(cfg?.time?.dstMode ?? 1);
  showDstManual(String(cfg?.time?.dstMode ?? 1) === "2");
  if ($("dstManualOn")) $("dstManualOn").checked = !!cfg?.time?.dstEnabled;

  // Timer
  if ($("runMode")) $("runMode").value = String(cfg?.operation?.runMode ?? 0);
  if ($("beforeShkia")) $("beforeShkia").value = String(cfg?.halacha?.minutesBeforeShkia ?? 30);
  if ($("afterTzeit")) $("afterTzeit").value = String(cfg?.halacha?.minutesAfterTzeit ?? 30);
  if ($("contactMap")) $("contactMap").value = cfg?.relay?.holyOnNo === false ? "1" : "0";
  if ($("relayBootMode")) $("relayBootMode").value = String(cfg?.relay?.bootMode ?? 0);

  state.windows = Array.isArray(cfg?.operation?.windows) ? cfg.operation.windows.slice(0, 10) : [];
  renderWindowsList();

  // OTA
  if ($("otaAuto")) $("otaAuto").checked = !!cfg?.ota?.auto;
  if ($("otaCheckHours")) $("otaCheckHours").value = String(cfg?.ota?.checkHours ?? 12);

  // Placeholders for default names
  const def = defaultSmartName();
  if ($("hostName") && !($("hostName").value || "").length) $("hostName").placeholder = def;
  if ($("apSsid") && !($("apSsid").value || "").length) $("apSsid").placeholder = def;
}

async function loadConfig() {
  try {
    const cfg = await apiGet("/api/config");
    state.config = cfg;
    applyConfigToUi(cfg);
  } catch {
    // ignore
  }
}

async function refreshStatusLite() {
  try {
    state.status = await apiGet("/api/status?lite=1");
    renderStatus();
  } catch {
    setText("healthLine", "אין חיבור");
    setPill("holyPill", "—", "bad");
  }
}

async function refreshTime() {
  try {
    const t = await apiGet("/api/time");
    state.time = t;
    setClockBaseFromDevice(t);
    renderClockInfo();
    renderClockTick();
  } catch {
    state.time = { valid: false };
    setClockBaseFromDevice({ valid: false });
  }
}

async function refreshSchedule() {
  try {
    state.schedule = await apiGet("/api/schedule");
    const upcoming = state.schedule?.upcoming || [];
    renderNextWindow(upcoming);
    renderUpcoming(upcoming);
  } catch {
    renderNextWindow([]);
    renderUpcoming([]);
  }
}

async function refreshHistory() {
  try {
    const r = await apiGet("/api/history?limit=60");
    state.history = r;
    renderHistory(r.items || []);
  } catch {
    renderHistory([]);
  }
}

function renderNetworks(nets) {
  const box = $("netList");
  if (!box) return;
  if (!Array.isArray(nets) || nets.length === 0) {
    box.innerHTML = `<div class="muted">לא נמצאו רשתות</div>`;
    return;
  }
  box.innerHTML = "";
  for (const n of nets) {
    const el = document.createElement("div");
    el.className = "item";
    const lock = n.secure ? "🔒" : "🔓";
    const rssi = Number(n.rssi || -100);
    const sig = rssi > -55 ? "חזקה" : rssi > -67 ? "בינונית" : "חלשה";
    el.innerHTML = `
      <div class="left">
        <div class="title">${lock} ${n.ssid || "(ללא שם)"}</div>
        <div class="sub">${n.secure ? "מוגנת" : "פתוחה"} · קליטה ${sig}</div>
      </div>
      <button class="btn" type="button">התחבר</button>
    `;
    el.querySelector("button").onclick = () => openWifiModal(n.ssid || "", !!n.secure);
    box.appendChild(el);
  }
}

async function scanNetworks() {
  setText("scanBtn", "סורק…");
  $("scanBtn").disabled = true;
  try {
    const nets = await apiGet("/api/wifi/scan");
    const sorted = Array.isArray(nets)
      ? nets
          .slice()
          .sort((a, b) => (Number(b?.rssi ?? -999) || -999) - (Number(a?.rssi ?? -999) || -999))
      : [];
    renderNetworks(sorted);
  } catch {
    renderNetworks([]);
    toast("סריקה נכשלה");
  } finally {
    setText("scanBtn", "סריקה");
    $("scanBtn").disabled = false;
  }
}

function renderSavedNetworks(res) {
  const box = $("savedList");
  if (!box) return;
  const nets = res?.nets || [];
  if (!Array.isArray(nets) || nets.length === 0) {
    box.innerHTML = `<div class="muted">אין רשתות שמורות עדיין</div>`;
    return;
  }
  box.innerHTML = "";
  for (const n of nets) {
    const el = document.createElement("div");
    el.className = "item";
    el.innerHTML = `
      <div class="left">
        <div class="title">${n.ssid || "(ללא שם)"}</div>
        <div class="sub">${n.last ? "אחרון שהצליח" : ""}</div>
      </div>
      <button class="btn danger" type="button">מחק</button>
    `;
    el.querySelector("button").onclick = async () => {
      if (!confirm(`למחוק את "${n.ssid}" מהרשתות השמורות?`)) return;
      try {
        await apiPost("/api/wifi/forget", { ssid: n.ssid });
        toast("נמחק");
      } catch {
        toast("מחיקה נכשלה");
      }
      loadSavedNetworks();
    };
    box.appendChild(el);
  }
}

async function loadSavedNetworks() {
  try {
    const res = await apiGet("/api/wifi/saved");
    renderSavedNetworks(res);
  } catch {
    renderSavedNetworks({ nets: [] });
  }
}

function openWifiModal(ssid, secure) {
  state.wifiModal = { open: true, ssid, secure };
  setText("wifiModalSsid", ssid ? `רשת: ${ssid}` : "רשת: —");
  setText("wifiModalHint", "");
  if ($("wifiModalPass")) $("wifiModalPass").value = "";
  if ($("wifiModalConnect")) {
    $("wifiModalConnect").disabled = false;
    $("wifiModalConnect").textContent = "התחבר";
  }

  const showPass = !!secure;
  const passRow = $("wifiModalPassRow");
  if (passRow) passRow.style.display = showPass ? "" : "none";

  const modal = $("wifiModal");
  if (modal) modal.style.display = "";
  setTimeout(() => {
    if (showPass && $("wifiModalPass")) $("wifiModalPass").focus();
    else if ($("wifiModalConnect")) $("wifiModalConnect").focus();
  }, 10);
}

function closeWifiModal() {
  cancelWifiConnectWatch();
  state.wifiModal = { open: false, ssid: "", secure: true };
  const modal = $("wifiModal");
  if (modal) modal.style.display = "none";
}

function cancelWifiConnectWatch() {
  const w = state.wifiConnectWatch;
  if (!w) return;
  w.cancelled = true;
  if (w.timer) clearTimeout(w.timer);
  state.wifiConnectWatch = null;
}

function pollWifiUntilConnected(targetSsid) {
  cancelWifiConnectWatch();
  state.wifiConnectWatch = { ssid: targetSsid, startedAtMs: Date.now(), cancelled: false, timer: null };

  const hint = $("wifiModalHint");
  const btn = $("wifiModalConnect");
  const maxMs = 2 * 60 * 1000;

  const tick = async () => {
    const w = state.wifiConnectWatch;
    if (!w || w.cancelled) return;
    if ((Date.now() - w.startedAtMs) > maxMs) {
      if (hint) hint.textContent = "לא הצלחנו להתחבר. אפשר לנסות שוב.";
      toast("התחברות נכשלה");
      cancelWifiConnectWatch();
      if (btn) {
        btn.disabled = false;
        btn.textContent = "התחבר";
      }
      return;
    }

    try {
      const s = await apiGet("/api/wifi/status");
      const connected = s?.staStatusCode === 3 && String(s?.staSsid || "") === String(targetSsid || "");
      const ip = String(s?.staIp || "").trim();
      if (connected && ip) {
        toast(`מחובר · IP ${ip}`);
        startRedirectToIp(ip);
        cancelWifiConnectWatch();
        closeWifiModal();
        refreshStatusLite();
        loadSavedNetworks();
        return;
      }

      const code = Number(s?.staStatusCode || 0);
      if (code === 6) {
        if (hint) hint.textContent = "סיסמה שגויה.";
        toast("סיסמה שגויה");
        cancelWifiConnectWatch();
        if (btn) {
          btn.disabled = false;
          btn.textContent = "התחבר";
        }
        return;
      }
    } catch {
      // ignore transient fetch failures (AP/STA switching)
    }

    const w2 = state.wifiConnectWatch;
    if (!w2 || w2.cancelled) return;
    w2.timer = setTimeout(tick, 1200);
  };

  if (hint) hint.textContent = "מתחבר… זה יכול לקחת עד דקה.";
  if (btn) {
    btn.disabled = true;
    btn.textContent = "ממתין…";
  }
  tick();
}

function startRedirectToIp(ip) {
  const targetIp = String(ip || "").trim();
  if (!targetIp) return;
  if (targetIp === location.hostname) return;

  state.redirect = { ip: targetIp, startedAtMs: Date.now() };
  clearInterval(startRedirectToIp._timer);

  const maxMs = 5 * 60 * 1000;
  const probe = async () => {
    if (!state.redirect?.ip) return;
    if ((Date.now() - state.redirect.startedAtMs) > maxMs) {
      clearInterval(startRedirectToIp._timer);
      state.redirect = { ip: "", startedAtMs: 0 };
      return;
    }
    try {
      // We don't need to read the response; success means the device is reachable on that network.
      await fetch(`http://${state.redirect.ip}/status.txt?ts=${Date.now()}`, { mode: "no-cors", cache: "no-store" });
      window.location.href = `http://${state.redirect.ip}/`;
    } catch {
      // Still not reachable from this network (likely still connected to the Hotspot).
    }
  };

  startRedirectToIp._timer = setInterval(probe, 2000);
  setTimeout(probe, 700);
}

async function wifiModalConnect() {
  const ssid = state.wifiModal?.ssid || "";
  if (!ssid) return;
  const password = state.wifiModal?.secure ? String($("wifiModalPass")?.value || "") : "";

  const btn = $("wifiModalConnect");
  const hint = $("wifiModalHint");
  const prev = btn?.textContent || "";
  let watchStarted = false;
  if (btn) {
    btn.disabled = true;
    btn.textContent = "מתחבר…";
  }
  if (hint) hint.textContent = "";

  try {
    const r = await apiPost("/api/wifi/connect", { ssid, password });
    if (r?.connected && r?.ip) {
      toast(`מחובר · IP ${r.ip}`);
      startRedirectToIp(r.ip);
      closeWifiModal();
      await sleep(300);
    } else if (Number(r?.status || 0) === 6) {
      const msg = "סיסמה שגויה.";
      if (hint) hint.textContent = msg;
      toast(msg);
    } else {
      watchStarted = true;
      pollWifiUntilConnected(ssid);
    }
  } catch (e) {
    const code = e?.data?.status ?? e?.data?.staStatusCode ?? 0;
    const msg = wifiStatusMessage(code);
    if (hint) hint.textContent = msg;
    toast(msg);
  } finally {
    if (!watchStarted && btn) {
      btn.disabled = false;
      btn.textContent = prev;
    }
  }

  refreshStatusLite();
  loadSavedNetworks();
}

async function resetWifi() {
  if (!confirm("לאפס Wi‑Fi ולהפעיל מחדש?")) return;
  try {
    await apiPost("/api/wifi/reset", {});
  } catch {
    toast("איפוס נכשל");
  }
}

async function factoryReset() {
  const msg =
    "איפוס מפעל ימחק את כל ההגדרות, רשתות Wi‑Fi שמורות, היסטוריה ונתוני עדכונים.\nלהמשיך?";
  if (!confirm(msg)) return;
  if (!confirm("בטוח?")) return;
  try {
    await apiPost("/api/factory_reset", {});
    toast("מאפס…");
  } catch {
    toast("איפוס נכשל");
  }
}

async function saveRunModeQuick() {
  const runMode = Number($("runModeQuick")?.value || 0);
  try {
    await apiPost("/api/config", { operation: { runMode } });
    toast("נשמר");
    await loadConfig();
    await refreshStatusLite();
  } catch {
    toast("שמירה נכשלה");
  }
}

async function saveNetworkPrefs() {
  const hostName = String($("hostName")?.value || "").trim();
  const apSsid = String($("apSsid")?.value || "").trim();

  const ipMode = String($("ipMode")?.value || "dhcp");
  const dhcp = ipMode !== "static";

  const body = { network: { hostName, ap: { ssid: apSsid }, sta: { dhcp } } };

  if (!dhcp) {
    const ip = String($("staIp")?.value || "").trim();
    const gateway = String($("staGw")?.value || "").trim();
    const subnet = String($("staMask")?.value || "").trim();
    const dns1 = String($("staDns1")?.value || "").trim();
    const dns2 = String($("staDns2")?.value || "").trim();

    if (!isValidIp(ip) || !isValidIp(gateway) || !isValidIp(subnet)) {
      toast("יש להזין IP תקין (IP/Gateway/Subnet)");
      return;
    }
    if (dns1 && !isValidIp(dns1)) {
      toast("DNS 1 לא תקין");
      return;
    }
    if (dns2 && !isValidIp(dns2)) {
      toast("DNS 2 לא תקין");
      return;
    }

    body.network.sta.static = {
      ip,
      gateway,
      subnet,
      dns1: dns1 || "0.0.0.0",
      dns2: dns2 || "0.0.0.0",
    };
  }

  // Hotspot password behavior:
  // - If protection OFF: clear password only if previously set.
  // - If protection ON: set new password if provided; if previously not set, require >=8.
  const apPasswordSet = !!state.config?.network?.ap?.passwordSet;
  const protectedOn = !!$("apProtected")?.checked;
  const apPassword = String($("apPassword")?.value || "");
  if (!protectedOn) {
    if (apPasswordSet) body.network.ap.password = "";
  } else if (apPassword.trim().length) {
    if (apPassword.trim().length < 8) {
      toast("סיסמת Hotspot חייבת להיות לפחות 8 תווים");
      return;
    }
    body.network.ap.password = apPassword.trim();
  } else if (!apPasswordSet) {
    toast("יש להזין סיסמה ל‑Hotspot (לפחות 8 תווים)");
    return;
  }

  try {
    const r = await apiPost("/api/config", body);
    toast(r?.reboot ? "נשמר · המכשיר יאתחל…" : "נשמר");
    if (r?.reboot) {
      // Best effort: the IP may change. Try to reload after a bit.
      await sleep(2500);
      location.reload();
      return;
    }
    await loadConfig();
    await refreshStatusLite();
  } catch {
    toast("שמירה נכשלה");
  }
}

async function setTimeNow() {
  try {
    await apiPost("/api/time", { utc: Math.floor(Date.now() / 1000) });
    toast("השעון עודכן");
    await refreshTime();
    refreshStatusLite();
  } catch {
    toast("עדכון נכשל");
  }
}

async function setManualTime() {
  const value = String($("manualTime")?.value || "");
  const utc = parseUtcFromDatetimeLocal(value);
  if (!utc) {
    toast("יש לבחור תאריך ושעה");
    return;
  }
  try {
    await apiPost("/api/time", { utc });
    toast("השעון עודכן");
    await refreshTime();
    refreshStatusLite();
  } catch {
    toast("עדכון נכשל");
  }
}

async function ntpSyncNow() {
  try {
    await apiPost("/api/ntp/sync", {});
    toast("סנכרון בוצע");
  } catch {
    toast("סנכרון נכשל");
  }
  await refreshTime();
  refreshStatusLite();
}

async function saveClockPrefs() {
  const ntpEnabled = !!$("ntpEnabled")?.checked;
  const ntpServer = String($("ntpServer")?.value || "pool.ntp.org").trim() || "pool.ntp.org";
  const ntpResyncMinutes = Number($("ntpResync")?.value || 0);
  const tzOffsetMinutes = Number($("tzOffset")?.value || 120);
  const dstMode = Number($("dstMode")?.value || 1);
  const dstEnabled = !!$("dstManualOn")?.checked;
  try {
    await apiPost("/api/config", {
      time: { ntpEnabled, ntpServer, ntpResyncMinutes, tzOffsetMinutes, dstMode, dstEnabled },
    });
    toast("נשמר");
    await loadConfig();
    await refreshTime();
    await refreshStatusLite();
  } catch {
    toast("שמירה נכשלה");
  }
}

function addWindowOverride() {
  const startUtc = parseUtcFromDatetimeLocal(String($("winStart")?.value || ""));
  const endUtc = parseUtcFromDatetimeLocal(String($("winEnd")?.value || ""));
  const on = String($("winAction")?.value || "on") === "on";
  if (!startUtc || !endUtc || endUtc <= startUtc) {
    toast("יש לבחור חלון תקין (התחלה/סיום)");
    return;
  }
  state.windows.push({ startUtc, endUtc, on });
  state.windows = state.windows
    .slice()
    .sort((a, b) => (a.startUtc || 0) - (b.startUtc || 0))
    .slice(0, 10);
  renderWindowsList();
  if ($("winStart")) $("winStart").value = "";
  if ($("winEnd")) $("winEnd").value = "";
  toast("נוסף");
}

async function saveTimerPrefs() {
  const runMode = Number($("runMode")?.value || 0);
  const minutesBeforeShkia = clamp($("beforeShkia")?.value || 30, 0, 240);
  const minutesAfterTzeit = clamp($("afterTzeit")?.value || 30, 0, 240);
  const holyOnNo = String($("contactMap")?.value || "0") === "0";
  const bootMode = Number($("relayBootMode")?.value || 0);

  try {
    await apiPost("/api/config", {
      operation: { runMode, windows: state.windows || [] },
      halacha: { minutesBeforeShkia, minutesAfterTzeit },
      relay: { holyOnNo, bootMode },
    });
    toast("נשמר");
    await loadConfig();
    await refreshSchedule();
    await refreshStatusLite();
  } catch {
    toast("שמירה נכשלה");
  }
}

async function refreshOtaStatus() {
  try {
    const s = await apiGet("/api/ota/status");
    state.ota = s;

    setText("otaCurrent", s.currentVersion || "—");
    const available = !!s.state?.available;
    setText("otaAvailable", available ? s.state?.availableVersion || "כן" : "לא");
    setText("otaLastCheck", s.state?.lastCheckUtc ? fmtUtcAsLocal(s.state.lastCheckUtc) : "—");

    const configured = !!s.config?.manifestUrl;
    if (!configured) {
      setPill("otaPill", "לא מוגדר", "warn");
      setText("otaHintTop", "לא מוגדר");
      setText("otaHint", "עדכונים לא הוגדרו בקושחה זו.");
    } else if (available) {
      setPill("otaPill", "יש עדכון", "warn");
      setText("otaHintTop", "יש עדכון");
      setText("otaHint", s.state?.notes || "");
    } else {
      setPill("otaPill", "מעודכן", "good");
      setText("otaHintTop", "מעודכן");
      setText("otaHint", s.state?.error ? `שגיאה אחרונה: ${s.state.error}` : "");
    }
  } catch {
    setPill("otaPill", "שגיאה", "bad");
    setText("otaHintTop", "שגיאה");
  }
}

async function saveOtaPrefs() {
  const auto = !!$("otaAuto")?.checked;
  const checkHours = Number($("otaCheckHours")?.value || 0);
  try {
    await apiPost("/api/config", { ota: { auto, checkHours } });
    toast("נשמר");
    await loadConfig();
    await refreshOtaStatus();
  } catch {
    toast("שמירה נכשלה");
  }
}

async function otaCheckNow() {
  try {
    const r = await apiPost("/api/ota/check", {});
    toast(r.available ? "יש עדכון" : "אין עדכון");
  } catch {
    toast("בדיקה נכשלה");
  }
  refreshOtaStatus();
}

async function otaUpdateNow() {
  if (!confirm("להתחיל עדכון? המכשיר יאתחל בסיום.")) return;
  try {
    await apiPost("/api/ota/update", {});
    toast("מתעדכן…");
  } catch {
    toast("עדכון נכשל");
  }
}

async function clearHistory() {
  if (!confirm("לנקות היסטוריה?")) return;
  try {
    await apiPost("/api/history/clear", {});
    toast("נוקה");
    refreshHistory();
  } catch {
    toast("נכשל");
  }
}

function bindEvents() {
  $("scanBtn")?.addEventListener("click", scanNetworks);
  $("resetWifiBtn")?.addEventListener("click", resetWifi);
  $("factoryResetBtn")?.addEventListener("click", factoryReset);

  $("ipMode")?.addEventListener("change", () => showStaticIp($("ipMode").value === "static"));
  $("apProtected")?.addEventListener("change", () => showApPassword(!!$("apProtected").checked));

  $("saveNetworkBtn")?.addEventListener("click", saveNetworkPrefs);

  $("saveModeBtn")?.addEventListener("click", saveRunModeQuick);

  $("setNowBtn")?.addEventListener("click", setTimeNow);
  $("setManualBtn")?.addEventListener("click", setManualTime);
  $("ntpSyncBtn")?.addEventListener("click", ntpSyncNow);
  $("saveClockBtn")?.addEventListener("click", saveClockPrefs);

  $("dstMode")?.addEventListener("change", () => showDstManual(String($("dstMode").value) === "2"));

  $("addWinBtn")?.addEventListener("click", addWindowOverride);
  $("saveTimerBtn")?.addEventListener("click", saveTimerPrefs);

  $("saveOtaBtn")?.addEventListener("click", saveOtaPrefs);
  $("otaCheckBtn")?.addEventListener("click", otaCheckNow);
  $("otaUpdateBtn")?.addEventListener("click", otaUpdateNow);

  $("clearHistoryBtn")?.addEventListener("click", clearHistory);

  // Wi‑Fi modal
  $("wifiModalCancel")?.addEventListener("click", closeWifiModal);
  $("wifiModalConnect")?.addEventListener("click", wifiModalConnect);
  $("wifiModal")?.addEventListener("click", (e) => {
    if (e.target === $("wifiModal")) closeWifiModal();
  });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && state.wifiModal?.open) closeWifiModal();
  });
}

(async function main() {
  bindEvents();
  await refreshTime();
  await refreshStatusLite();
  await loadConfig();
  await loadSavedNetworks();
  await refreshSchedule();
  await refreshOtaStatus();
  await refreshHistory();

  setInterval(renderClockTick, 1000);
  setInterval(refreshTime, 60000);
  setInterval(refreshStatusLite, 2000);
  setInterval(refreshSchedule, 15000);
  setInterval(refreshOtaStatus, 20000);
  setInterval(refreshHistory, 12000);
})();
