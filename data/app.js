const state = {
  status: null,
  time: null,
  clock: { baseLocal: 0, baseMs: 0, valid: false },
  config: null,
  schedule: null,
  ota: null,
  history: null,
  windows: [],
  wifiModal: { open: false, ssid: "", secure: true, ch: 0, bssid: "" },
  redirect: { ip: "", startedAtMs: 0, countdownSec: 0 },
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

async function apiGet(path, options) {
  const timeoutMs = Number((options && options.timeoutMs) || 0);
  const res = await _fetchWithTimeout(path, { cache: "no-store" }, timeoutMs);
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  if (!res.ok) {
    const msg = (data && (data.error || data.message)) || text || `HTTP ${res.status}`;
    const err = new Error(msg);
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

async function apiPost(path, body, options) {
  const timeoutMs = Number((options && options.timeoutMs) || 0);
  const res = await _fetchWithTimeout(
    path,
    {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(body || {}),
    },
    timeoutMs
  );
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  if (!res.ok) {
    const msg = (data && (data.error || data.message)) || text || `HTTP ${res.status}`;
    const err = new Error(msg);
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

async function _fetchWithTimeout(url, opts, timeoutMs) {
  const ms = Number(timeoutMs || 0);
  if (!ms) return fetch(url, opts);
  if (typeof AbortController === "undefined") {
    return await Promise.race([
      fetch(url, opts),
      new Promise((_, reject) => setTimeout(() => reject(new Error("timeout")), ms))
    ]);
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), ms);
  try {
    const options = Object.assign({}, opts || {});
    if (!options.signal) options.signal = controller.signal;
    return await fetch(url, options);
  } finally {
    clearTimeout(timer);
  }
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

const hebrewGematriaMap = {
  א: 1,
  ב: 2,
  ג: 3,
  ד: 4,
  ה: 5,
  ו: 6,
  ז: 7,
  ח: 8,
  ט: 9,
  י: 10,
  כ: 20,
  ך: 20,
  ל: 30,
  מ: 40,
  ם: 40,
  נ: 50,
  ן: 50,
  ס: 60,
  ע: 70,
  פ: 80,
  ף: 80,
  צ: 90,
  ץ: 90,
  ק: 100,
  ר: 200,
  ש: 300,
  ת: 400,
};

function addGershayim(text) {
  const s = String(text || "");
  if (!s) return "";
  if (s.length === 1) return `${s}׳`;
  return `${s.slice(0, -1)}״${s.slice(-1)}`;
}

function hebrewNumber(num) {
  const n = Math.floor(Number(num || 0));
  if (!Number.isFinite(n) || n <= 0) return "";
  let value = n;
  let out = "";

  while (value >= 400) {
    out += "ת";
    value -= 400;
  }

  if (value >= 100) {
    const h = Math.floor(value / 100);
    if (h === 1) out += "ק";
    else if (h === 2) out += "ר";
    else if (h === 3) out += "ש";
    value = value % 100;
  }

  if (value === 15) {
    out += "טו";
    value = 0;
  } else if (value === 16) {
    out += "טז";
    value = 0;
  }

  if (value >= 10) {
    const t = Math.floor(value / 10);
    if (t === 1) out += "י";
    else if (t === 2) out += "כ";
    else if (t === 3) out += "ל";
    else if (t === 4) out += "מ";
    else if (t === 5) out += "נ";
    else if (t === 6) out += "ס";
    else if (t === 7) out += "ע";
    else if (t === 8) out += "פ";
    else if (t === 9) out += "צ";
    value = value % 10;
  }

  if (value > 0) {
    if (value === 1) out += "א";
    else if (value === 2) out += "ב";
    else if (value === 3) out += "ג";
    else if (value === 4) out += "ד";
    else if (value === 5) out += "ה";
    else if (value === 6) out += "ו";
    else if (value === 7) out += "ז";
    else if (value === 8) out += "ח";
    else if (value === 9) out += "ט";
  }

  return addGershayim(out);
}

function parseNumberFromText(text) {
  const digits = String(text || "").match(/\d+/g);
  if (!digits) return 0;
  const joined = digits.join("");
  const n = Number(joined);
  return Number.isFinite(n) ? n : 0;
}

function hebrewGematriaValue(text) {
  if (!text) return 0;
  let sum = 0;
  const clean = String(text).replace(/[^א-ת]/g, "");
  for (const char of clean) {
    sum += hebrewGematriaMap[char] || 0;
  }
  return sum;
}

function hebrewDateParts(epochLocal) {
  if (!epochLocal) return null;
  const fmt = getHebrewFormatter();
  if (!fmt) return null;
  const d = new Date(epochLocal * 1000);
  try {
    if (typeof fmt.formatToParts === "function") {
      const parts = fmt.formatToParts(d);
      const dayRaw = parts.find((p) => p.type === "day")?.value || "";
      let month = parts.find((p) => p.type === "month")?.value || "";
      const yearRaw = parts.find((p) => p.type === "year")?.value || "";
      const dayNum = parseNumberFromText(dayRaw);
      const yearNum = parseNumberFromText(yearRaw);
      const day = dayNum ? hebrewNumber(dayNum) : dayRaw;
      const yearVal = yearNum ? (yearNum % 1000 || yearNum) : 0;
      const year = yearVal ? hebrewNumber(yearVal) : yearRaw;
      month = month.replace(/^ב/, "");
      const text = `${day} ${month} ${year}`.replace(/\s+/g, " ").trim();
      return {
        text: text || "—",
        daySymbol: day || dayRaw,
        month,
        year: year || yearRaw,
        dayValue: dayNum || hebrewGematriaValue(day),
      };
    }
    const raw = String(fmt.format(d) || "").replace(/\s+/g, " ").trim();
    const nums = raw.match(/\d+/g) || [];
    if (nums.length >= 2) {
      const dayNum = parseNumberFromText(nums[0]);
      const yearNum = parseNumberFromText(nums[nums.length - 1]);
      const monthRaw = raw
        .replace(/\d+/g, " ")
        .replace(/\s+/g, " ")
        .trim()
        .replace(/^ב/, "");
      const day = dayNum ? hebrewNumber(dayNum) : "";
      const yearVal = yearNum ? (yearNum % 1000 || yearNum) : 0;
      const year = yearVal ? hebrewNumber(yearVal) : "";
      const text = `${day} ${monthRaw} ${year}`.replace(/\s+/g, " ").trim();
      return {
        text: text || raw || "—",
        daySymbol: day || String(nums[0] || ""),
        month: monthRaw,
        year: year || String(nums[nums.length - 1] || ""),
        dayValue: dayNum || 0,
      };
    }
    return { text: raw || "—", daySymbol: "", month: "", year: "", dayValue: 0 };
  } catch {
    return null;
  }
}

function fmtHebrewShort(epochLocal) {
  const parts = hebrewDateParts(epochLocal);
  return parts?.text || "—";
}

function hebrewDateSummary(epochLocal) {
  const parts = hebrewDateParts(epochLocal);
  if (!parts) return null;
  return { text: parts.text || "—", gem: "" };
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
  if (st.schedule?.errorCode === "CLOCK_NOT_SET") return "צריך לכוון שעה";
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

  const ntpEnabled = !!state.config?.time?.ntpEnabled;
  const src = ntpEnabled ? "ntp" : (tm?.source || "");
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
  const baseHint = hint === "מוכן" ? (clockOk ? "מכוון" : "לא מכוון") : hint;
  const ntpMinutes = Number(tm?.ntpResyncMinutes ?? state.config?.time?.ntpResyncMinutes ?? 0);
  const ntpSuffix = ntpEnabled && ntpMinutes > 0 ? ` · NTP כל ${ntpMinutes} דקות` : "";
  setText("clockHint", `${baseHint}${ntpSuffix}`.trim());
}

function renderClockTick() {
  const localNow = clockLocalNow();
  setText("nowTime", localNow ? fmtLocal(localNow) : "—");
  let html = `<div class="muted">—</div>`;
  if (localNow) {
    const summary = hebrewDateSummary(localNow);
    if (summary && summary.text && summary.text !== "—") {
      html = `<div>${summary.text}${summary.gem ? ` · ${summary.gem}` : ""}</div>`;
      const nextEpoch = Number(state.time?.nextHebrewDateStartLocal || 0);
      const afterSunset = nextEpoch > 0 && localNow >= nextEpoch;
      if (afterSunset && nextEpoch > 0) {
        const nextSummary = hebrewDateSummary(nextEpoch);
        if (nextSummary && nextSummary.text && nextSummary.text !== "—") {
          html += `<div class="muted">אור ל ${nextSummary.text}${nextSummary.gem ? ` · ${nextSummary.gem}` : ""}</div>`;
        }
      }
    }
  }
  setHtml("nowHebrewDate", html);

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
  const connected = st.relay?.connected !== undefined ? !!st.relay.connected : !!st.relay?.on;
  setText("relayState", connected ? "מחובר" : "מנותק");
  setText("modeState", computeModeState());
  setText("nextChange", renderNextChange(st));

  setText("netBadge", buildNetBadge(wifi));
  setText("netHint", buildNetHint(wifi));

  setText("healthLine", computeHealthLine(st));

  const holy = st.schedule?.ok && st.schedule?.inHolyTime;
  setPill("holyPill", holy ? "שבת/חג" : "חול", holy ? "warn" : "good");

  renderClockInfo();
  if (wifi.apMode && wifi.staIp && wifi.staIp !== location.hostname) {
    navigateToIp(wifi.staIp);
  }
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
  if ($("relayActiveLow")) $("relayActiveLow").value = cfg?.relay?.activeLow === false ? "0" : "1";
  if ($("relayBootMode")) $("relayBootMode").value = String(cfg?.relay?.bootMode ?? 0);
  renderRelayWiringHint();

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

function renderRelayWiringHint() {
  const box = $("contactMapHint");
  if (!box) return;

  const wiredToNo = String($("contactMap")?.value || "0") === "0";
  const activeLow = String($("relayActiveLow")?.value || "1") === "1";

  const lines = [];
  lines.push("מחובר = יש מתח ביציאה · מנותק = אין מתח ביציאה.");
  lines.push(wiredToNo ? "בחרת NO: מחובר רק כשהממסר דלוק (קליק בזמן חיבור/ניתוק)." : "בחרת NC: מחובר כשהממסר כבוי.");
  lines.push("המלצה: כדי להיות מנותק בחול ושקט (בלי קליק קבוע) — חבר את העומס ל‑NO ובחר NO כאן.");
  if (!wiredToNo) {
    lines.push("שים לב: אם מחברים ל‑NC ורוצים להיות מנותק בחול, הממסר עלול להיות דלוק דווקא בחול (זה תקין, אבל יש קליק/חום).");
  }
  lines.push(activeLow ? "Active LOW: הממסר נדלק כשהפין LOW." : "Active HIGH: הממסר נדלק כשהפין HIGH.");
  lines.push("אם בפועל הממסר נדלק כשאמור להיות כבוי — נסה להחליף Active LOW/HIGH ואז לשמור.");

  box.innerHTML = lines.map((t) => `<div>${t}</div>`).join("");
}

async function loadConfig() {
  try {
    const cfg = await apiGet("/api/config");
    state.config = cfg;
    applyConfigToUi(cfg);
    // Config affects derived UI strings (e.g. NTP auto/manual label).
    renderClockInfo();
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
    el.querySelector("button").onclick = () => openWifiModal(n.ssid || "", !!n.secure, Number(n.ch || 0), String(n.bssid || ""));
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

	function openWifiModal(ssid, secure, ch, bssid) {
	  state.wifiModal = { open: true, ssid, secure, ch: Number(ch || 0), bssid: String(bssid || "") };
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
  state.wifiModal = { open: false, ssid: "", secure: true, ch: 0, bssid: "" };
  const modal = $("wifiModal");
  if (modal) modal.style.display = "none";
}

function cancelWifiConnectWatch() {
  const w = state.wifiConnectWatch;
  if (!w) return;
  w.cancelled = true;
  if (w.pollTimer) clearInterval(w.pollTimer);
  if (w.countdownTimer) clearInterval(w.countdownTimer);
  if (w.timer) clearTimeout(w.timer);
  state.wifiConnectWatch = null;
}

function navigateToIp(ip) {
  const targetIp = String(ip || "").trim();
  if (!targetIp || targetIp === "0.0.0.0") return false;
  if (targetIp === location.hostname) return false;
  window.location.href = `http://${targetIp}/`;
  return true;
}

function startWifiConnectWatch(targetSsid) {
  cancelWifiConnectWatch();
  const hint = $("wifiModalHint");
  const btn = $("wifiModalConnect");
  const startedAtMs = Date.now();
  const deadlineMs = startedAtMs + 10 * 1000;
  state.wifiConnectWatch = {
    ssid: targetSsid,
    startedAtMs,
    deadlineMs,
    cancelled: false,
    lastSeenStaIp: "",
    pollTimer: null,
    countdownTimer: null,
    inFlight: false,
    lastShownSec: null,
  };

  const updateUi = () => {
    const w = state.wifiConnectWatch;
    if (!w || w.cancelled) return;
    const leftSec = Math.max(0, Math.ceil((w.deadlineMs - Date.now()) / 1000));
    if (w.lastShownSec === leftSec) return;
    w.lastShownSec = leftSec;
    if (hint) {
      hint.textContent =
        leftSec > 0
          ? `מתחבר… ננסה להעביר אותך ל‑IP החדש בעוד ${leftSec} שניות.`
          : "מנסה להעביר ל‑IP החדש…";
    }
    if (btn) btn.textContent = leftSec > 0 ? `ממתין… (${leftSec})` : "מעביר…";
  };

  const stopWatch = () => {
    const w = state.wifiConnectWatch;
    if (!w) return;
    w.cancelled = true;
    if (w.pollTimer) clearInterval(w.pollTimer);
    if (w.countdownTimer) clearInterval(w.countdownTimer);
    state.wifiConnectWatch = null;
  };

  const failNow = (msg) => {
    stopWatch();
    if (hint) hint.textContent = msg;
    toast(msg);
    if (btn) {
      btn.disabled = false;
      btn.textContent = "התחבר";
    }
  };

  const succeedNow = (ip) => {
    const cleanIp = String(ip || "").trim();
    if (!cleanIp || cleanIp === "0.0.0.0") return;
    stopWatch();
    toast(`מחובר · IP ${cleanIp}`);
    navigateToIp(cleanIp);
  };

  const pollOnce = async () => {
    const w = state.wifiConnectWatch;
    if (!w || w.cancelled || w.inFlight) return;
    w.inFlight = true;
    try {
      const s = await apiGet("/api/wifi/status", { timeoutMs: 900 });
      const ip = String(s?.staIp || "").trim();
      if (ip && ip !== "0.0.0.0") {
        w.lastSeenStaIp = ip;
        succeedNow(ip);
        return;
      }

      const code = Number(s?.lastFailCode ?? s?.staStatusCode ?? 0);
      const sdkCode = Number(s?.sdkStaStatus || 0);
      if (code === 6 || sdkCode === 2) {
        failNow("סיסמה שגויה.");
        return;
      }
    } catch {
      // ignore transient fetch failures (AP/STA switching)
    } finally {
      const w2 = state.wifiConnectWatch;
      if (w2) w2.inFlight = false;
    }
  };

  const onTimeout = () => {
    const w = state.wifiConnectWatch;
    if (!w || w.cancelled) return;
    const leftMs = w.deadlineMs - Date.now();
    if (leftMs > 0) return;
    const lastIp = String(w.lastSeenStaIp || "").trim();
    stopWatch();
    if (lastIp) {
      navigateToIp(lastIp);
      return;
    }
    location.reload();
  };

  if (btn) {
    btn.disabled = true;
    btn.textContent = "ממתין… (10)";
  }
  updateUi();

  state.wifiConnectWatch.countdownTimer = setInterval(() => {
    updateUi();
    onTimeout();
  }, 250);
  state.wifiConnectWatch.pollTimer = setInterval(pollOnce, 650);
  setTimeout(pollOnce, 60);
}

	async function wifiModalConnect() {
	  const ssid = state.wifiModal?.ssid || "";
	  if (!ssid) return;
	  const password = state.wifiModal?.secure ? String($("wifiModalPass")?.value || "") : "";
	  const channel = Number(state.wifiModal?.ch || 0) || 0;
	  const bssid = String(state.wifiModal?.bssid || "");
  const btn = $("wifiModalConnect");
  const hint = $("wifiModalHint");
  const prev = btn?.textContent || "";
  if (btn) {
    btn.disabled = true;
    btn.textContent = "מתחבר…";
  }
  if (hint) hint.textContent = "";

  startWifiConnectWatch(ssid);
	  try {
	    await apiPost("/api/wifi/connect", { ssid, password, channel, bssid }, { timeoutMs: 1200 });
	  } catch (e) {
    // If the AP is switching channels, the request may time out even if it was received.
    // Keep the 10s watch running; only show a user-facing error for clear failures.
    const code = Number(e?.data?.status ?? e?.data?.staStatusCode ?? 0);
    if (code === 6) {
      cancelWifiConnectWatch();
      const msg = "סיסמה שגויה.";
      if (hint) hint.textContent = msg;
      toast(msg);
      if (btn) {
        btn.disabled = false;
        btn.textContent = prev || "התחבר";
      }
      return;
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
  const activeLow = String($("relayActiveLow")?.value || "1") === "1";
  const bootMode = Number($("relayBootMode")?.value || 0);

  try {
    await apiPost("/api/config", {
      operation: { runMode, windows: state.windows || [] },
      halacha: { minutesBeforeShkia, minutesAfterTzeit },
      relay: { holyOnNo, activeLow, bootMode },
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

    try {
      const cur = String(s.currentVersion || "").trim();
      const key = "smartshabat_last_version";
      const prev = String(localStorage.getItem(key) || "").trim();
      if (cur && prev && cur !== prev) {
        toast(`עודכן לגרסה ${cur}`);
      }
      if (cur) localStorage.setItem(key, cur);
    } catch {
      // ignore (private mode / storage disabled)
    }

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
  } catch (e) {
    const msg = String(e?.data?.message || e?.data?.error || e?.message || "").trim();
    toast(msg ? `בדיקה נכשלה: ${msg}` : "בדיקה נכשלה");
  }
  refreshOtaStatus();
}

async function otaUpdateNow() {
  if (!confirm("להתחיל עדכון? המכשיר יאתחל בסיום.")) return;
  try {
    const r = await apiPost("/api/ota/update", {});
    if (r && r.started === false) {
      toast("אין עדכון זמין");
      return;
    }
    toast("מתעדכן…");
    startPostUpdateReloadProbe();
  } catch (e) {
    const msg = String(e?.data?.message || e?.data?.error || e?.message || "").trim();
    toast(msg ? `עדכון נכשל: ${msg}` : "עדכון נכשל");
  }
}

function startPostUpdateReloadProbe() {
  clearInterval(startPostUpdateReloadProbe._t);
  const startedAt = Date.now();
  const maxMs = 4 * 60 * 1000;

  const probe = async () => {
    if ((Date.now() - startedAt) > maxMs) {
      clearInterval(startPostUpdateReloadProbe._t);
      return;
    }
    try {
      // If the device rebooted and the web server is back, this request will succeed.
      await fetch(`/status.txt?ts=${Date.now()}`, { cache: "no-store" });
      clearInterval(startPostUpdateReloadProbe._t);
      setTimeout(() => location.reload(), 600);
    } catch {
      // still rebooting
    }
  };

  startPostUpdateReloadProbe._t = setInterval(probe, 2000);
  setTimeout(probe, 900);
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
  $("contactMap")?.addEventListener("change", renderRelayWiringHint);
  $("relayActiveLow")?.addEventListener("change", renderRelayWiringHint);

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
