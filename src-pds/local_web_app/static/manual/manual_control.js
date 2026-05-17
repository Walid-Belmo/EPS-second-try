// Manual Control page front-end.
// Polls /api/status every 250 ms. The Off-then-Enter-Manual gate mirrors the
// State page convention: the user must click Off once on this page load
// before the Enter Manual Mode button is enabled. The four control cards
// (PWM, PV switch, BAT switch, LED) stay disabled until the firmware
// confirms it is in manual mode (in_manual_mode == true).

let offClickedSinceLoad = false;
let inManualMode = false;

async function getJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`GET ${url} -> ${response.status}`);
  }
  return response.json();
}

async function postJson(url, body) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  const data = await response.json();
  if (!data.ok) {
    throw new Error(data.error || "request failed");
  }
  return data;
}

const el = (id) => document.getElementById(id);

// ── Initialization ──────────────────────────────────────────────────────────

async function initializePage() {
  bindButtons();
  setInterval(pollStatusAndRender, 250);
  await pollStatusAndRender();
}

function bindButtons() {
  el("connectButton").addEventListener("click", onClickConnect);
  el("disconnectButton").addEventListener("click", onClickDisconnect);
  el("offButton").addEventListener("click", onClickOff);
  el("enterManualButton").addEventListener("click", onClickEnterManual);

  el("pwmApplyButton").addEventListener("click", onClickApplyPwm);
  el("pwmZeroButton").addEventListener("click", () => sendPwm(0));
  el("pwmSlider").addEventListener("input", () => {
    el("pwmTextInput").value = el("pwmSlider").value;
  });
  el("pwmTextInput").addEventListener("input", () => {
    const value = clamp(parseInt(el("pwmTextInput").value, 10) || 0, 0, 65535);
    el("pwmSlider").value = value;
  });

  el("pvOnButton").addEventListener("click", () => postManual("/api/manual/set_pv", { on: true }));
  el("pvOffButton").addEventListener("click", () => postManual("/api/manual/set_pv", { on: false }));
  el("batOnButton").addEventListener("click", () => postManual("/api/manual/set_bat", { on: true }));
  el("batOffButton").addEventListener("click", () => postManual("/api/manual/set_bat", { on: false }));
  el("ledOnButton").addEventListener("click", () => postManual("/api/manual/set_led", { on: true }));
  el("ledOffButton").addEventListener("click", () => postManual("/api/manual/set_led", { on: false }));
}

function clamp(value, lo, hi) {
  return Math.max(lo, Math.min(hi, value));
}

// ── Button handlers ─────────────────────────────────────────────────────────

async function onClickConnect() {
  const port = el("serialPort").value.trim() || "COM3";
  try {
    await postJson("/api/connect", { port });
  } catch (exc) {
    alert("Connect failed: " + exc.message);
  }
}

async function onClickDisconnect() {
  try {
    await postJson("/api/disconnect", {});
    offClickedSinceLoad = false;
    refreshGate();
  } catch (exc) {
    alert("Disconnect failed: " + exc.message);
  }
}

async function onClickOff() {
  try {
    await postJson("/api/off", {});
    offClickedSinceLoad = true;
    refreshGate();
  } catch (exc) {
    alert("Off failed: " + exc.message);
  }
}

async function onClickEnterManual() {
  try {
    await postJson("/api/manual/enter_manual_mode", {});
  } catch (exc) {
    alert("Enter manual mode failed: " + exc.message);
  }
}

async function onClickApplyPwm() {
  const duty = clamp(parseInt(el("pwmTextInput").value, 10) || 0, 0, 65535);
  await sendPwm(duty);
}

async function sendPwm(duty) {
  try {
    await postJson("/api/manual/set_pwm", { duty });
  } catch (exc) {
    alert("Set PWM failed: " + exc.message);
  }
}

async function postManual(url, body) {
  try {
    await postJson(url, body);
  } catch (exc) {
    alert("Command failed: " + exc.message);
  }
}

// ── Polling ─────────────────────────────────────────────────────────────────

async function pollStatusAndRender() {
  let snapshot;
  try {
    snapshot = await getJson("/api/status");
  } catch (exc) {
    el("connectionStatus").textContent = "Server unreachable: " + exc.message;
    return;
  }

  renderConnectionStatus(snapshot);
  inManualMode = !!snapshot.in_manual_mode;
  refreshGate();
  renderTelemetry(snapshot.telemetry || {});
  renderCommandLog(snapshot.commands || []);
}

function renderConnectionStatus(snapshot) {
  if (snapshot.connected) {
    el("connectionStatus").textContent = "Connected on " + snapshot.serial_port;
  } else if (snapshot.connection_error) {
    el("connectionStatus").textContent = "Disconnected: " + snapshot.connection_error;
  } else {
    el("connectionStatus").textContent = "Disconnected";
  }
}

function refreshGate() {
  // Off gate: hides the banner and unlocks Enter Manual once Off has been
  // pressed at least once on this page load.
  el("offGateBanner").classList.toggle("hidden", offClickedSinceLoad);
  el("enterManualButton").disabled = !offClickedSinceLoad;

  // Manual mode gate: per-card controls only enable while firmware reports
  // in_manual_mode == true.
  const ids = [
    "pwmSlider", "pwmTextInput", "pwmApplyButton", "pwmZeroButton",
    "pvOnButton", "pvOffButton",
    "batOnButton", "batOffButton",
    "ledOnButton", "ledOffButton",
  ];
  ids.forEach((id) => { el(id).disabled = !inManualMode; });

  const badge = el("manualModeBadge");
  badge.textContent = inManualMode ? "Manual mode active" : "Not in manual mode";
  badge.classList.toggle("active", inManualMode);
}

function renderTelemetry(telemetry) {
  // PWM card readbacks.
  setText("pwmRequestedReadback", telemetry.requested_pwm);
  setText("pwmAppliedReadback", telemetry.applied_pwm);
  setText("pwmEnabledReadback", telemetry.pwm_enabled);

  // PV/BAT/LED requested + readback indicators.
  setText("pvRequestedReadback", telemetry.manual_pv_requested);
  setIndicator("pvPgoodReadback", telemetry.pv_pgood, "on", "off");
  setIndicator("pvFaultReadback", telemetry.pv_flt, "bad", "off");

  setText("batRequestedReadback", telemetry.manual_bat_requested);
  setIndicator("batPgoodReadback", telemetry.bat_pgood, "on", "off");
  setIndicator("batFaultReadback", telemetry.bat_flt, "bad", "off");

  setText("ledRequestedReadback", telemetry.manual_led_requested);
  setIndicator("ledActualReadback", telemetry.manual_led_is_on, "on", "off");

  // Live telemetry sidebar.
  setText("liveModeValue", telemetry.requested_mode);
  setText("liveManualPwmValue", telemetry.manual_pwm);
  setText("liveAppliedPwmValue", telemetry.applied_pwm);
  setText("livePvPgoodValue", telemetry.pv_pgood);
  setText("livePvFaultValue", telemetry.pv_flt);
  setText("liveBatPgoodValue", telemetry.bat_pgood);
  setText("liveBatFaultValue", telemetry.bat_flt);
  setText("liveLedOnValue", telemetry.manual_led_is_on);
}

function setText(id, value) {
  const node = el(id);
  if (value === undefined || value === null) {
    node.textContent = "—";
  } else {
    node.textContent = String(value);
  }
}

function setIndicator(id, value, onClass, offClass) {
  const node = el(id);
  node.classList.remove("on", "off", "bad");
  if (value === undefined || value === null) {
    node.textContent = "—";
    node.classList.add("off");
    return;
  }
  if (Number(value) !== 0) {
    node.textContent = "yes";
    node.classList.add(onClass);
  } else {
    node.textContent = "no";
    node.classList.add(offClass);
  }
}

function renderCommandLog(commands) {
  const lines = commands.slice(-50).map((cmd) => {
    const time = new Date(cmd.time * 1000).toLocaleTimeString();
    return `[${time}] ${cmd.direction}  ${cmd.text}`;
  });
  el("commandLog").textContent = lines.join("\n");
}

initializePage();
