// Sensor Reads page — always-on observability of every sensor on every rail.
// Polls /api/status every 250 ms. On first load issues
// /api/sensor-reads/enter to start the firmware streaming. Source toggle
// flips the firmware's global sensor source.

let streamStarted = false;

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

async function initializePage() {
  bindButtons();
  setInterval(pollStatusAndRender, 250);
  await pollStatusAndRender();
  await ensureStreamStarted();
}

function bindButtons() {
  el("connectButton").addEventListener("click", onClickConnect);
  el("disconnectButton").addEventListener("click", onClickDisconnect);
  el("sourceInjectedButton").addEventListener("click", () => onClickSource("injected"));
  el("sourceRealButton").addEventListener("click", () => onClickSource("real"));
}

async function onClickConnect() {
  const port = el("serialPort").value.trim() || "COM3";
  try {
    await postJson("/api/connect", { port });
    streamStarted = false;
    await ensureStreamStarted();
  } catch (exc) {
    alert("Connect failed: " + exc.message);
  }
}

async function onClickDisconnect() {
  try {
    await postJson("/api/disconnect", {});
    streamStarted = false;
  } catch (exc) {
    alert("Disconnect failed: " + exc.message);
  }
}

async function onClickSource(source) {
  try {
    await postJson("/api/sensor-reads/set_source", { source });
  } catch (exc) {
    alert("Set source failed: " + exc.message);
  }
}

async function ensureStreamStarted() {
  // Only kick the stream once per page load. Subsequent connects re-arm
  // the flag in onClickConnect so the new session also gets streaming.
  if (streamStarted) {
    return;
  }
  try {
    await postJson("/api/sensor-reads/enter", {});
    streamStarted = true;
  } catch (exc) {
    // Quiet — likely the serial port isn't open yet. The next connect
    // will trigger this path again.
  }
}

async function pollStatusAndRender() {
  let snapshot;
  try {
    snapshot = await getJson("/api/status");
  } catch (exc) {
    el("connectionStatus").textContent = "Server unreachable: " + exc.message;
    return;
  }

  renderConnectionStatus(snapshot);
  renderTelemetry(snapshot.telemetry || {});
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

function renderTelemetry(telemetry) {
  // Per-card sensor rows.
  setText("batteryVIna226",   telemetry.battery_v_ina226);
  setText("batteryVDivider",  telemetry.battery_v_divider);
  setText("batteryVLayer1",   telemetry.battery_v_layer1);

  setText("batteryIIna226",   telemetry.battery_i_ina226);
  setText("batteryILt6108",   telemetry.battery_i_lt6108);
  setText("batteryITps25940", telemetry.battery_i_tps25940);
  setText("batteryILayer1",   telemetry.battery_i_layer1);

  setText("panelVIna226",     telemetry.panel_v_ina226);
  setText("panelVDivider",    telemetry.panel_v_divider);
  setText("panelVLayer1",     telemetry.panel_v_layer1);

  setText("panelIIna226",     telemetry.panel_i_ina226);
  setText("panelILt6108",     telemetry.panel_i_lt6108);
  setText("panelITps25940",   telemetry.panel_i_tps25940);
  setText("panelILayer1",     telemetry.panel_i_layer1);

  setText("railVDivider",     telemetry.rail_v_divider);
  setText("railVLayer1",      telemetry.rail_v_layer1);

  // Sidebar.
  setText("liveModeValue",         telemetry.requested_mode);
  setText("liveSourceValue",       telemetry.sensor_source);
  setText("liveStreamValue",       telemetry.stream_enabled);
  setText("liveStreamPeriodValue", telemetry.stream_period_ms);

  // Source-toggle pill highlight.
  const source = telemetry.sensor_source;
  el("sourceInjectedButton").classList.toggle("active", source === "injected");
  el("sourceRealButton").classList.toggle("active", source === "real");
}

function setText(id, value) {
  const node = el(id);
  if (value === undefined || value === null) {
    node.textContent = "—";
  } else {
    node.textContent = String(value);
  }
}

initializePage();
