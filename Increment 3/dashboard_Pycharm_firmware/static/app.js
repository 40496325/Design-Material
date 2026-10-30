// REII Dashboard Frontend — WebSocket client
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
  const magnetCurrentEl = document.getElementById("magnetCurrent");
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
  let wsConnected = false;

  function log(msg, type = "info") {
    const entry = document.createElement("div");
    entry.className = `log-entry ${type}`;
    const ts = new Date().toLocaleTimeString("en-GB", { hour12: false });
    entry.innerHTML = `<span class="ts">[${ts}]</span>${msg}`;
    logContainer.appendChild(entry);
    logContainer.scrollTop = logContainer.scrollHeight;
  }

  function setStatus(online, text) {
    connIndicator.className = online ? "indicator online" : "indicator offline";
    connText.textContent = text;
  }

  function updateTelemetry(frame) {
    if (!frame || frame.status === "no_data") return;
    trolleyPosEl.textContent = frame.trolley_pos_m !== undefined ? frame.trolley_pos_m.toFixed(3) : "--";
    hoistPosEl.textContent = frame.hoist_pos_m !== undefined ? frame.hoist_pos_m.toFixed(3) : "--";
    trolleyVelEl.textContent = frame.trolley_vel_m_s !== undefined ? frame.trolley_vel_m_s.toFixed(3) : "--";
    hoistVelEl.textContent = frame.hoist_vel_m_s !== undefined ? frame.hoist_vel_m_s.toFixed(3) : "--";
    loadEl.textContent = frame.load_kg !== undefined ? frame.load_kg.toFixed(1) : "--";
    vfdCurrentEl.textContent = frame.vfd_current_A !== undefined ? frame.vfd_current_A.toFixed(1) : "--";
    magnetCurrentEl.textContent = frame.magnet_current_A !== undefined ? frame.magnet_current_A.toFixed(1) : "--";
    seqAckEl.textContent = frame.seq_ack !== undefined ? frame.seq_ack : "--";
    magnetStatEl.textContent = frame.magnet_status === 1 ? "HOLDING" : (frame.magnet_status === 2 ? "FAULT" : "OFF");
    errorCodeEl.textContent = frame.error_code !== undefined ? frame.error_code : "--";
    faultFlagsEl.textContent = frame.fault_flags !== undefined ? `0x${frame.fault_flags.toString(16).padStart(2, '0').toUpperCase()}` : "--";
    sysHealthEl.textContent = frame.system_health !== undefined ? `0x${frame.system_health.toString(16).padStart(2, '0').toUpperCase()}` : "--";

    lastTelemetryTs = Date.now();
    setStatus(true, "Crane Link Active (ch77)");
  }

  function checkWatchdog() {
    if (!wsConnected) {
      setStatus(false, "Server Disconnected");
      return;
    }
    if (!lastTelemetryTs || Date.now() - lastTelemetryTs > 2000) {
      setStatus(false, "RF Link Offline (No ACKs)");
      lastUpdateEl.textContent = lastTelemetryTs ? `${Math.floor((Date.now() - lastTelemetryTs)/1000)}s ago` : "--";
    } else {
      const secs = Math.floor((Date.now() - lastTelemetryTs) / 1000);
      lastUpdateEl.textContent = secs === 0 ? "just now" : `${secs}s ago`;
    }
  }

  setInterval(checkWatchdog, 1000);

  socket.on("connect", () => {
    wsConnected = true;
    setStatus(true, "Waiting for Crane Link...");
    log("WebSocket connected to backend", "info");
  });

  socket.on("disconnect", () => {
    wsConnected = false;
    setStatus(false, "Disconnected");
    log("WebSocket disconnected", "warn");
  });

  socket.on("telemetry", (frame) => {
    updateTelemetry(frame);
  });

  socket.on("link_status", (data) => {
    if (!data.link_ok) {
      setStatus(false, "RF Link Lost");
    }
  });

  function sendCommand(overrides = {}) {
    const trolley = parseInt(trolleyCtrl.value, 10);
    const hoist = parseInt(hoistCtrl.value, 10);
    socket.emit("command", {
      estop: overrides.estop ?? false,
      reset: overrides.reset ?? false,
      manual: manualMode,
      trolley: overrides.trolley ?? trolley,
      hoist: overrides.hoist ?? hoist,
      magnet: magnetOn ? 1 : 0,
    });
  }

  btnManual.addEventListener("click", () => {
    manualMode = true;
    btnManual.classList.add("active");
    btnAuto.classList.remove("active");
    sendCommand();
  });

  btnAuto.addEventListener("click", () => {
    manualMode = false;
    btnAuto.classList.add("active");
    btnManual.classList.remove("active");
    sendCommand();
  });

  btnMagnet.addEventListener("click", () => {
    magnetOn = !magnetOn;
    btnMagnet.textContent = magnetOn ? "On" : "Off";
    btnMagnet.classList.toggle("active", magnetOn);
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

  let sendTimeout;
  [trolleyCtrl, hoistCtrl].forEach((ctrl) => {
    ctrl.addEventListener("input", () => {
      trolleyValEl.textContent = trolleyCtrl.value;
      hoistValEl.textContent = hoistCtrl.value;
      clearTimeout(sendTimeout);
      sendTimeout = setTimeout(() => sendCommand(), 50);
    });
  });

  setStatus(false, "Connecting...");
})();