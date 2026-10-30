// REII crane console - talks to server.py (Server-Sent Events in, JSON POSTs out).
"use strict";

const $ = (id) => document.getElementById(id);
const FIELD_COLOURS = ["#8a96a3", "#6b7d8f", "#2f6fed", "#9c27b0", "#00897b", "#f2b705", "#e65100", "#5d4037",
                       "#c62828", "#3949ab", "#43a047", "#d81b60", "#00acc1", "#7cb342"];
let ICD = null;
let CONFIG = { rail_mm: 5000, stroke_mm: 1600 };
let lastState = null;
let prevDown = "", prevUp = "";

// ------------------------------------------------------------------ operator controls
const ctl = { trolley: 0, hoist: 0, magnet_on: false, hold_brake: false, auto: false };
let keySpeed = 60;
let softStopLatched = false;

function post(path, body) {
  return fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) })
    .catch(() => {});
}
function sendControl() { post("/api/control", ctl); }
setInterval(sendControl, 200);   // also the deadman heartbeat

function bindAxis(name) {
  const el = $(name), out = $(name + "Out");
  const set = (v) => { el.value = v; ctl[name] = Number(v); out.textContent = `${v > 0 ? "+" : ""}${v} %`; sendControl(); };
  el.addEventListener("input", () => set(el.value));
  const release = () => { if ($("holdToRun").checked) set(0); };
  el.addEventListener("pointerup", release);
  el.addEventListener("pointercancel", release);
  el.addEventListener("blur", release);
  el.addEventListener("keydown", (e) => e.stopPropagation());   // let the slider use arrows when focused
  return set;
}
const setTrolley = bindAxis("trolley");
const setHoist = bindAxis("hoist");

for (const [id, key] of [["magnetOn", "magnet_on"], ["holdBrake", "hold_brake"], ["autoMode", "auto"]]) {
  $(id).addEventListener("change", (e) => { ctl[key] = e.target.checked; sendControl(); });
}
document.querySelectorAll("[data-action]").forEach((b) =>
  b.addEventListener("click", () => post("/api/action", { action: b.dataset.action })));
$("pa").addEventListener("change", (e) => post("/api/action", { action: "pa", value: e.target.value }));
$("channel").addEventListener("change", (e) => post("/api/action", { action: "channel", value: Number(e.target.value) }));
$("softStop").addEventListener("click", toggleSoftStop);
$("calBtn").addEventListener("click", () => {
  const kg = parseFloat($("calKg").value);
  if (!(kg >= 0.05 && kg <= 50)) { $("calKg").focus(); return; }
  post("/api/action", { action: "load_cal", value: kg });
});

function toggleSoftStop() {
  if (!softStopLatched) { setTrolley(0); setHoist(0); }
  post("/api/action", { action: softStopLatched ? "release_stop" : "soft_stop" });
}

const held = new Set();
document.addEventListener("keydown", (e) => {
  if (e.target.matches("input[type=text], select")) return;
  const k = e.key;
  if (k === " ") { e.preventDefault(); if (!e.repeat) toggleSoftStop(); return; }
  if (k === "m" || k === "M") { if (!e.repeat) { $("magnetOn").click(); } return; }
  const map = { ArrowLeft: [setTrolley, -1], ArrowRight: [setTrolley, 1], ArrowDown: [setHoist, -1], ArrowUp: [setHoist, 1] };
  if (map[k]) { e.preventDefault(); if (!held.has(k)) { held.add(k); map[k][0](map[k][1] * keySpeed); } }
});
document.addEventListener("keyup", (e) => {
  const k = e.key;
  if (!held.has(k)) return;
  held.delete(k);
  if (k === "ArrowLeft" || k === "ArrowRight") setTrolley(0);
  if (k === "ArrowUp" || k === "ArrowDown") setHoist(0);
});
window.addEventListener("blur", () => { held.clear(); setTrolley(0); setHoist(0); });

// ------------------------------------------------------------------ rendering
const fmt = (v, d = 0) => (v === null || v === undefined || Number.isNaN(v) ? "–" : Number(v).toFixed(d));

const STATE_TEXT = {
  starting: ["Starting", "chip-wait"], connecting: ["Connecting", "chip-wait"], running: ["Linked", "chip-ok"],
  lost: ["No reply from crane", "chip-bad"], check_failed: ["Ground station not ready", "chip-bad"],
  port_error: ["Ground station unplugged", "chip-bad"],
};

function renderLink(s) {
  const [text, cls] = STATE_TEXT[s.state] || [s.state, "chip-wait"];
  const chip = $("linkState");
  chip.textContent = text;
  chip.className = `chip ${cls}`;
  const st = s.stats;
  $("statPer").textContent = st.per_pct === null ? "–" : `${fmt(st.per_pct, 1)} %`;
  $("statRtt").textContent = st.rtt_avg_ms === null ? "–" : `${fmt(st.rtt_avg_ms, 2)} ms (max ${fmt(st.rtt_max_ms, 2)})`;
  $("statArc").textContent = st.retries_avg === null ? "–" : fmt(st.retries_avg, 2);
  $("statSent").textContent = `${st.acked} / ${st.sent}`;
  $("statPa").textContent = s.gs_link.pa ? `${s.gs_link.pa} · ch ${s.gs_link.ch}` : "–";
  renderRadio(s);

  const banner = $("linkBanner");
  const messages = {
    starting: "Starting the link to the ground station.",
    connecting: `Checking the ground station on ${s.gs_port}.`,
    check_failed: `The ground station on ${s.gs_port} is not ready: ${s.detail}. Flash the Increment 4 ground-station firmware or check the port.`,
    port_error: `${s.detail}. Plug the ground station in; the dashboard retries every 2 s.`,
    lost: "The crane is not acknowledging commands. Check that it is powered, flashed and within range. The crane holds position on its own after 0.5 s without commands.",
  };
  if (messages[s.state]) {
    banner.hidden = false;
    banner.textContent = messages[s.state];
    banner.className = s.state === "connecting" || s.state === "starting" ? "banner wait" : "banner";
  } else if (s.operator.deadman_tripped) {
    banner.hidden = false;
    banner.className = "banner wait";
    banner.textContent = "This page lost contact with the dashboard backend for over 1 s, so speeds were set to 0.";
  } else {
    banner.hidden = true;
  }
  const r = s.rig || {};
  $("rigInfo").textContent = r.connected
    ? `Crane console on ${s.rig_port}: telemetry source ${r.tlm || r.tlm_source || "unknown"}` +
      `${(r.tlm || r.tlm_source) === "MODEL" ? " (positions, speeds and currents are a bench model; the load is the real load cell)" : ""}, ` +
      `${r.frames || 0} frames received, link ${r.link || "?"}`
    : "Telemetry source is set on the crane (TLM MODEL / TLM ZERO). Start with --rig-port COM12 to show it here.";
  $("logDir").textContent = s.log_dir ? `Saved with telemetry.csv in ${s.log_dir}` : "";
}

function renderOperator(o) {
  softStopLatched = o.soft_stop;
  const b = $("softStop");
  b.textContent = o.soft_stop ? "Release soft stop" : "Soft stop";
  b.classList.toggle("latched", o.soft_stop);
  $("trolleyCmd").textContent = fmt(o.cmd_trolley, 0);
  $("hoistCmd").textContent = fmt(o.cmd_hoist, 0);
}

function renderGantry(t) {
  const railFrac = Math.max(0, Math.min(1, t.trolley_pos_mm / CONFIG.rail_mm));
  const x = 108 + railFrac * (892 - 108);
  $("trolley").setAttribute("transform", `translate(${x} 0)`);
  const heightFrac = Math.max(0, Math.min(1, t.hoist_pos_mm / CONFIG.stroke_mm));
  const hookY = 290 - heightFrac * (290 - 110);       // floor-ish at 0, near the trolley at full stroke
  $("cable").setAttribute("y2", hookY);
  $("hook").setAttribute("transform", `translate(0 ${hookY})`);
  $("dirL").classList.toggle("on", t.trolley_vel_mm_s < 0);
  $("dirR").classList.toggle("on", t.trolley_vel_mm_s > 0);
  const mag = $("magnet");
  mag.classList.toggle("on", t.magnet_status === 1);
  mag.classList.toggle("fault", t.magnet_status === 2);
  const over = t.fault_flags & 0x02;
  $("loadBox").hidden = !(t.load_kg > 0);
  $("loadText").textContent = t.load_kg >= 6553.5 ? "over range" : `${fmt(t.load_kg, 1)} kg`;
  document.querySelector(".load").classList.toggle("over", !!over);
  const railLimit = t.fault_flags & 0x08;
  $("stopL").classList.toggle("hit", !!railLimit && railFrac < 0.5);
  $("stopR").classList.toggle("hit", !!railLimit && railFrac >= 0.5);
  $("posLabel").textContent =
    `Trolley ${fmt(t.trolley_pos_mm / 1000, 2)} m of ${fmt(CONFIG.rail_mm / 1000, 1)} m, hook ${fmt(t.hoist_pos_mm / 1000, 2)} m above the floor`;
}

function renderReadouts(t, age) {
  $("tPos").textContent = fmt(t.trolley_pos_mm / 1000, 3);
  $("tVel").textContent = fmt(t.trolley_vel_mm_s / 1000, 2);
  $("hPos").textContent = fmt(t.hoist_pos_mm / 1000, 3);
  $("hVel").textContent = fmt(t.hoist_vel_mm_s, 0);
  const overRange = t.load_kg >= 6553.5;   // 0xFFFF: crane ADC at its limit
  $("load").textContent = overRange ? "over range" : fmt(t.load_kg, 1);
  const bar = $("loadBar");
  bar.style.width = `${overRange ? 100 : Math.min(100, (t.load_kg / 25) * 100)}%`;
  bar.classList.toggle("over", overRange || t.load_kg > 20);
  renderLoadCheck(t, overRange);
  $("vfdI").textContent = fmt(t.vfd_current_a, 1);
  $("magI").textContent = fmt(t.magnet_current_a, 1);
  $("magS").textContent = t.magnet_status_text;
  $("tlmAge").textContent = fmt(age, 2);
}

// Load-cell check: the crane's own reading over USB (--rig-port) next to the value that
// arrived over the radio in Table 34 bytes 10-11. They should agree to 0.1 kg.
function renderLoadCheck(t, overRange) {
  const el = $("loadCheck");
  const r = (lastState && lastState.rig) || {};
  $("loadTools").hidden = !r.connected;
  const fresh = r.lc_time && Date.now() / 1000 - Number(r.lc_time) < 12;
  if (!r.connected) { el.textContent = "Radio value. Add --rig-port COM12 to compare with the crane's own reading."; el.className = "loadcheck"; return; }
  if (r.lc_running === "0") { el.textContent = "Crane reports the load cell is not running."; el.className = "loadcheck bad"; return; }
  if (r.lc_saturated === "1" || overRange) {
    el.textContent = r.lc_src === "ADS1220"
      ? "Crane ADC at its limit: check the J6 wiring (EXC+ J6.1, EXC- J6.2, SIG+ J6.5, SIG- J6.6)."
      : "HX711 at its limit: check the 4 load-cell wires on the HX711 board (E+, E-, A+, A-).";
    el.className = "loadcheck bad"; return;
  }
  if (r.lc_valid === "0" && r.lc_src === "HX711") {
    el.textContent = "HX711 not answering: check DAT J7.11, CLK J7.4, VCC J3.11 (3.3 V), GND J3.1.";
    el.className = "loadcheck bad"; return;
  }
  if (r.lc_valid === "0" && r.lc_src === "ADS1220" && Number(r.lc_samples || 0) === 0) {
    el.textContent = "ADS1220 not converting yet: check the J6 wiring and the crane's boot log.";
    el.className = "loadcheck bad"; return;
  }
  if (!fresh) { el.textContent = "Waiting for the crane's load reading (every 5 s, or LOAD WATCH on COM12)."; el.className = "loadcheck"; return; }
  const crane = Math.max(0, Number(r.lc_kg));
  const diff = Math.abs(crane - t.load_kg);
  const cal = r.lc_calibrated === "1" ? "calibrated" : "not calibrated yet (LOAD CAL)";
  el.textContent = `Crane (${r.lc_src || "load cell"}) reads ${crane.toFixed(2)} kg over USB, ${cal}. ` +
    (diff <= 0.15 ? "Radio value matches." : `Radio value differs by ${diff.toFixed(2)} kg (normal while the load is moving).`);
  el.className = "loadcheck " + (diff <= 0.15 ? "ok" : "");
}

function renderLamps(t) {
  if (!ICD) return;
  const lamps = (ul, table, value) => {
    ul.innerHTML = "";
    for (const [bit, name] of Object.entries(table)) {
      const li = document.createElement("li");
      li.textContent = name;
      if ((value >> Number(bit)) & 1) li.className = "on";
      ul.appendChild(li);
    }
  };
  lamps($("faults"), ICD.fault_flags, t ? t.fault_flags : 0);
  lamps($("health"), ICD.health_flags, t ? t.system_health : 0);
  const ec = $("errCode");
  ec.textContent = t ? (t.error_code ? `code ${t.error_code}` : "code 0, no faults") : "no telemetry yet";
  ec.classList.toggle("bad", !!(t && t.error_code));
}

function fieldFor(map, i) { return map.findIndex((f) => i >= f.first && i <= f.last); }

function renderStrip(stripId, fieldsId, hex, prev, map, decoded) {
  const strip = $(stripId);
  if (!strip.childElementCount) {
    for (let i = 0; i < 32; i++) {
      const c = document.createElement("div");
      const fi = fieldFor(map, i);
      const f = map[fi];
      c.className = "cell" + (f.name === "padding" ? " pad" : "");
      c.style.setProperty("--fc", f.name === "padding" ? "var(--line)" : FIELD_COLOURS[fi % FIELD_COLOURS.length]);
      c.title = `byte ${i}: ${f.name} (${f.type}) - ${f.meaning}`;
      c.innerHTML = `--<small>${i}</small>`;
      strip.appendChild(c);
    }
    const legend = $(fieldsId);
    map.forEach((f, fi) => {
      if (f.name === "padding") return;
      const s = document.createElement("span");
      s.style.setProperty("--fc", FIELD_COLOURS[fi % FIELD_COLOURS.length]);
      s.innerHTML = `<i></i>${f.name} <b data-field="${f.name}">–</b>`;
      legend.appendChild(s);
    });
  }
  if (!hex) return;
  [...strip.children].forEach((c, i) => {
    const byte = hex.substr(i * 2, 2);
    c.firstChild.textContent = byte;
    c.classList.toggle("changed", prev.length === 64 && prev.substr(i * 2, 2) !== byte);
  });
  for (const b of $(fieldsId).querySelectorAll("b[data-field]")) {
    const v = decoded[b.dataset.field];
    b.textContent = v === undefined ? "–" : v;
  }
}

function decodedUp(t) {
  if (!t) return {};
  return {
    magic: "0x81", seq_ack: t.seq_ack, trolley_pos: `${t.trolley_pos_mm} mm`, hoist_pos: `${t.hoist_pos_mm} mm`,
    trolley_vel: `${t.trolley_vel_mm_s} mm/s`, hoist_vel: `${t.hoist_vel_mm_s} mm/s`, load_cell: `${fmt(t.load_kg, 1)} kg`,
    vfd_current: `${fmt(t.vfd_current_a, 1)} A`, magnet_current: `${fmt(t.magnet_current_a, 1)} A`,
    magnet_status: t.magnet_status_text, error_code: t.error_code,
    fault_flags: `0x${t.fault_flags.toString(16).padStart(2, "0").toUpperCase()}`,
    system_health: `0x${t.system_health.toString(16).padStart(2, "0").toUpperCase()}`,
  };
}
function decodedDown(c) {
  if (!c || c.magic === undefined) return {};
  return {
    magic: "0xA5", msg_type: c.msg_type === 1 ? "ping" : "move", sequence: c.sequence,
    mode_flags: c.flags.length ? c.flags.join(", ") : "none", trolley_cmd: `${c.trolley_cmd} %`,
    hoist_cmd: `${c.hoist_cmd} %`, magnet_state: c.magnet_state ? "on" : "off", segment_idx: c.segment_idx,
  };
}

// ------------------------------------------------------------------ history chart
const history = [];
function drawChart() {
  const cv = $("chart");
  const dpr = window.devicePixelRatio || 1;
  const w = cv.clientWidth, h = cv.clientHeight || 180;
  if (cv.width !== Math.round(w * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
  const ctx = cv.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const css = getComputedStyle(document.documentElement);
  ctx.strokeStyle = css.getPropertyValue("--line");
  ctx.lineWidth = 1;
  for (let g = 0; g <= 4; g++) { const y = 6 + (g * (h - 12)) / 4; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke(); }
  if (history.length < 2) return;
  const t1 = history[history.length - 1].t, t0 = t1 - 30;
  const series = [
    ["trolley_pos", CONFIG.rail_mm, "--magnet"], ["hoist_pos", CONFIG.stroke_mm, "--hazard"], ["load", 25, "--fault"],
  ];
  for (const [key, full, colour] of series) {
    ctx.strokeStyle = css.getPropertyValue(colour);
    ctx.lineWidth = 2;
    ctx.beginPath();
    history.forEach((p, i) => {
      const x = ((p.t - t0) / 30) * w;
      const y = h - 6 - Math.max(0, Math.min(1, p[key] / full)) * (h - 12);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.stroke();
  }
}

// ------------------------------------------------------------------ event log
function addLog(ev) {
  const li = document.createElement("li");
  li.className = ev.level;
  li.innerHTML = `<time>${ev.t}</time><span></span>`;
  li.lastChild.textContent = ev.text;
  const list = $("events");
  list.prepend(li);
  while (list.childElementCount > 200) list.lastChild.remove();
}

// ------------------------------------------------------------------ main
function render(s) {
  lastState = s;
  if (!ICD) loadIcd();
  renderLink(s);
  renderOperator(s.operator);
  const t = s.telemetry;
  const stale = !t || s.telemetry_age_s === null || s.telemetry_age_s > 1.0;
  document.querySelector(".gantry-panel").classList.toggle("stale", stale);
  if (t) {
    renderGantry(t);
    renderReadouts(t, s.telemetry_age_s);
  }
  renderLamps(t);
  document.querySelector(".controls").classList.toggle("disabled", s.state !== "running");
  if (ICD) {
    renderStrip("downStrip", "downFields", s.downlink_hex, prevDown, ICD.downlink, decodedDown(s.last_cmd));
    renderStrip("upStrip", "upFields", s.uplink_hex, prevUp, ICD.uplink, decodedUp(t));
  }
  prevDown = s.downlink_hex; prevUp = s.uplink_hex;
  $("downMeta").textContent = s.last_cmd && s.last_cmd.sequence !== undefined ? `seq ${s.last_cmd.sequence}` : "";
  $("upMeta").textContent = t ? `answers command ${t.seq_ack} (the reply is loaded one command ahead)` : "no telemetry yet";
  if (t) {
    history.push({ t: Date.now() / 1000, trolley_pos: t.trolley_pos_mm, hoist_pos: t.hoist_pos_mm, load: t.load_kg });
    while (history.length && history[0].t < Date.now() / 1000 - 31) history.shift();
  }
  drawChart();
}

// ---------- radio link: power and clean channels (Increment 2 survey) ----------
let radioOptionsBuilt = false;
function buildRadioOptions() {
  const r = ICD && ICD.radio;
  if (!r || radioOptionsBuilt) return;
  const pa = $("pa");
  pa.textContent = "";
  for (const lvl of r.pa_levels) {
    const label = { LOW: "Low", HIGH: "High", MIN: "Minimum", MAX: "Maximum" }[lvl] || lvl;
    pa.add(new Option(`${label} (${String(r.pa_dbm[lvl]).replace("-", "−")} dBm)${lvl === "LOW" ? ", bench" : ""}`, lvl));
  }
  const ch = $("channel");
  ch.textContent = "";
  const groups = [["75–81, above WiFi ch 11", (c) => c.ch >= 75], ["2–4, below WiFi ch 1", (c) => c.ch < 75]];
  for (const [title, test] of groups) {
    const g = document.createElement("optgroup");
    g.label = title;
    for (const c of r.channels.filter(test)) {
      const o = new Option(`${c.ch} · ${c.mhz} MHz${c.ch === r.default_channel ? ", ICD default" : ""}`, String(c.ch));
      o.title = `worst RPD ${c.worst_rpd_pct} % over 9 surveys${c.note ? "; " + c.note : ""}`;
      g.appendChild(o);
    }
    ch.appendChild(g);
  }
  radioOptionsBuilt = true;
}

function syncSelect(sel, value) {
  if (document.activeElement === sel || value === undefined || value === null) return;
  const v = String(value);
  if (![...sel.options].some((o) => o.value === v)) sel.add(new Option(v, v));   // e.g. --pa MAX from the command line
  sel.value = v;
}

function renderRadio(s) {
  buildRadioOptions();
  const r = s.radio || {};
  const rig = s.rig || {};
  syncSelect($("pa"), r.pa);
  syncSelect($("channel"), r.channel);
  const busy = r.verifying || r.pending;
  $("pa").disabled = s.state !== "running" || busy;
  $("channel").disabled = !r.can_change_channel || s.state !== "running" || busy;
  $("channel").title = r.can_change_channel ? "" : "Start the dashboard with --rig-port COM12 to change channel";

  const el = $("radioState");
  const gs = `Ground station: ch ${s.gs_link.ch || "?"}, ${s.gs_link.pa || "?"}`;
  if (r.verifying) {
    el.textContent = `Checking channel ${r.channel}… (switches back by itself if the crane doesn't answer)`;
    el.className = "radiostate busy";
  } else if (!rig.connected) {
    el.textContent = `${gs}. Crane: not visible. Power changes reach only the ground station, and channel ` +
      "changes need --rig-port COM12, until the crane can be switched over the air.";
    el.className = "radiostate";
  } else {
    const mismatch = (rig.ch && rig.ch !== String(r.channel)) || (rig.pa && rig.pa !== r.pa);
    el.textContent = `${gs}. Crane (USB): ch ${rig.ch || "?"}, ${rig.pa || "?"}` +
      (mismatch ? ". The two ends differ: the dashboard re-applies its settings when the crane reboots." : ".");
    el.className = "radiostate" + (mismatch ? " bad" : "");
  }
}

let icdLoading = false;
async function loadIcd() {
  if (icdLoading) return;
  icdLoading = true;
  try {
    ICD = await (await fetch("/api/icd")).json();
    CONFIG = ICD.config || CONFIG;
  } catch { /* backend not up yet - retried on the next state message */ }
  icdLoading = false;
}

async function start() {
  await loadIcd();
  renderLamps(null);
  const es = new EventSource("/events");
  es.addEventListener("state", (e) => render(JSON.parse(e.data)));
  es.addEventListener("log", (e) => addLog(JSON.parse(e.data)));
  es.onerror = () => {
    const chip = $("linkState");
    chip.textContent = "Dashboard backend stopped";
    chip.className = "chip chip-bad";
  };
}
start();
