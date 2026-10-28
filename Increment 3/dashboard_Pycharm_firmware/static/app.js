// REII Dashboard Frontend — WebSocket client for Flask/SocketIO backend
// Architecture A1 (decision D-10)

(function () {
  const socket = io();

  const connIndicator = document.getElementById("connIndicator");
  const connText = document.getElementById("connText");
  const logContainer = document.getElementById("logContainer");

  const trolleyPosEl = document.getElementById("trolleyPos");
  const hoistPosEl = document.getElementById("hoistPos");
  const trolleyVelEl = document.getElementById("trolleyVel");
  const hoistVelEl = document.getElementById("hoistVel");
  const loadEl = document.getElementById("load");
  const vfdCurrentEl = document.getElementById("vfdCurrent");
  const tempEl = document.getElementById("temp");
  const seqAckEl = document.getElementById("seqAck");
  const magnetStatEl = document.getElementById("magnetStat");
  const errorCodeEl = document.getElementById("errorCode");
  const faultFlagsEl = document.getElementById("faultFlags");
  const sysHealthEl = document.getElementById("sysHealth");
  const lastUpdateEl = document.getElementById("lastUpdate");

  const trolleyCtrl = document.getElementById("trolleyCtrl");
  const hoistCtrl = document.getElementById("hoistCtrl");
  const trolleyValEl = document.getElementById("trolleyVal");
  const hoistValEl = document.getElementById("hoistVal");
  const btnManual = document.getElementById("btnManual");
  const btnAuto = document.getElementById("btnAuto");
  const btnMagnet = document.getElementById("btnMagnet");
  const btnEstop = document.getElementById("btnEstop");
  const btnReset = document.getElementById("btnReset");
  const cmdSentEl = document.getElementById("cmdSent");

  let manualMode = true;
  let magnetOn = false;
  let lastTelemetryTs = null;

  function log(msg, type = "info") {
    const entry = document.createElement("div");
    entry.className = `log-entry ${type}`;
    const ts = new Date().toLocaleTimeString("en-GB", { hour12: false });
    entry.innerHTML = `<span class="ts">[${ts}]</span>${msg}`;
    logContainer.appendChild(entry);
    logContainer.scrollTop = logContainer.scrollHeight;
  }

  function setConnected(connected) {
    if (connected) {
      connIndicator.className = "indicator online";
      connText.textContent = "Connected";
    } else {
      connIndicator.className = "indicator offline";
      connText.textContent = "Disconnected";
    }
  }

  function updateTelemetry(frame) {
    if (!frame || frame.status === "no_data") return;
    trolleyPosEl.textContent = frame.trolley_pos_m !== undefined ? frame.trolley_pos_m.toFixed(3) : "--";
    hoistPosEl.textContent = frame.hoist_pos_m !== undefined ? frame.hoist_pos_m.toFixed(3) : "--";
    trolleyVelEl.textContent = frame.trolley_vel_m_s !== undefined ? frame.trolley_vel_m_s.toFixed(3) : "--";
    hoistVelEl.textContent = frame.hoist_vel_m_s !== undefined ? frame.hoist_vel_m_s.toFixed(3) : "--";
    loadEl.textContent = frame.load_kg !== undefined ? frame.load_kg.toFixed(1) : "--";
    vfdCurrentEl.textContent = frame.vfd_current_A !== undefined ? frame.vfd_current_A.toFixed(1) : "--";
    tempEl.textContent = frame.temp_c !== undefined ? frame.temp_c : "--";
    seqAckEl.textContent = frame.seq_ack !== undefined ? frame.seq_ack : "--";
    magnetStatEl.textContent = frame.magnet_status !== undefined ? (frame.magnet_status ? "ON" : "OFF") : "--";
    errorCodeEl.textContent = frame.error_code !== undefined ? frame.error_code : "--";
    faultFlagsEl.textContent = frame.fault_flags !== undefined ? frame.fault_flags : "--";
    sysHealthEl.textContent = frame.system_health !== undefined ? frame.system_health : "--";
    lastTelemetryTs = Date.now();
    updateLastUpdate();
  }

  function updateLastUpdate() {
    if (!lastTelemetryTs) { lastUpdateEl.textContent = "--"; return; }
    const secs = Math.floor((Date.now() - lastTelemetryTs) / 1000);
    lastUpdateEl.textContent = secs === 0 ? "just now" : `${secs}s`;
  }

  setInterval(updateLastUpdate, 1000);

  function setManualMode(manual) {
    manualMode = manual;
    btnManual.classList.toggle("active", manual);
    btnAuto.classList.toggle("active", !manual);
  }

  function setMagnet(on) {
    magnetOn = on;
    btnMagnet.textContent = on ? "On" : "Off";
    btnMagnet.classList.toggle("active", on);
  }

  socket.on("connect", () => {
    setConnected(true);
    log("WebSocket connected", "info");
  });

  socket.on("disconnect", () => {
    setConnected(false);
    log("WebSocket disconnected", "warn");
  });

  socket.on("connected", (data) => {
    log(`Server ready — port ${data.port}`, "info");
  });

  socket.on("telemetry", (frame) => {
    updateTelemetry(frame);
    log(`Telemetry: seq=${frame.seq_ack ?? "?"} load=${frame.load_kg ?? "?"}kg trolley=${frame.trolley_pos_m ?? "?"}m`, "telemetry");
  });

  socket.on("pong", (data) => {
    log(`PONG ok=${data.ok} rtt=${data.rtt_ms}ms`, "info");
  });

  socket.on("echo", (data) => {
    log(`ECHO: ${data.text}`, "info");
  });

  socket.on("banner", (data) => {
    log(data.text, "info");
  });

  socket.on("status", (data) => {
    if (data.status === "ready") {
      log("Groundstation READY", "info");
    } else if (data.status === "error") {
      log(`Error: ${data.detail}`, "error");
    } else {
      log(`Status: ${data.detail || data.status}`, "info");
    }
  });

  socket.on("command_sent", (data) => {
    if (data.ok) {
      log(`Command sent: seq=${data.seq} hex=${data.hex}`, "command");
      cmdSentEl.textContent = `Last command: seq=${data.seq} hex=${data.hex}`;
    } else {
      log(`Command failed: ${data.error}`, "error");
      cmdSentEl.textContent = `Last command: FAILED (${data.error})`;
    }
  });

  socket.on("reader_started", (data) => {
    log(`Serial reader started on ${data.port}`, "info");
  });

  socket.on("reader_stopped", () => {
    log("Serial reader stopped", "warn");
  });

  trolleyCtrl.addEventListener("input", () => {
    trolleyValEl.textContent = trolleyCtrl.value;
  });

  hoistCtrl.addEventListener("input", () => {
    hoistValEl.textContent = hoistCtrl.value;
  });

  btnManual.addEventListener("click", () => setManualMode(true));
  btnAuto.addEventListener("click", () => setManualMode(false));

  btnMagnet.addEventListener("click", () => {
    setMagnet(!magnetOn);
    sendCommand();
  });

  btnEstop.addEventListener("click", () => {
    sendCommand({ estop: true });
    log("E-STOP triggered!", "warn");
  });

  btnReset.addEventListener("click", () => {
    sendCommand({ reset: true });
    log("RESET triggered", "warn");
  });

  function sendCommand(overrides = {}) {
    const trolley = parseInt(trolleyCtrl.value, 10);
    const hoist = parseInt(hoistCtrl.value, 10);
    const payload = {
      estop: overrides.estop ?? false,
      reset: overrides.reset ?? false,
      manual: manualMode,
      trolley: overrides.trolley ?? trolley,
      hoist: overrides.hoist ?? hoist,
      magnet: magnetOn ? 1 : 0,
    };
    socket.emit("command", payload);
  }

  let sendTimeout;
  [trolleyCtrl, hoistCtrl].forEach((ctrl) => {
    ctrl.addEventListener("input", () => {
      clearTimeout(sendTimeout);
      sendTimeout = setTimeout(() => sendCommand(), 100);
    });
  });

  setManualMode(true);
  setMagnet(false);
  setConnected(false);
})();