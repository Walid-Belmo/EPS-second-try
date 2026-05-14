// State Transition Page front-end.
// Polls /api/status every 300 ms. Wires the buttons, runs scenarios, renders
// the SVG state diagram, fills the live metric tiles, and updates the
// expected-vs-observed table for the last scenario.

const PCU_MODES_FOR_DIAGRAM = [
  { id: "MPPT_CHARGE",       label: "MPPT_CHARGE",       x: 120, y: 80 },
  { id: "CV_FLOAT",          label: "CV_FLOAT",          x: 360, y: 80 },
  { id: "SA_LOAD_FOLLOW",    label: "SA_LOAD_FOLLOW",    x: 600, y: 80 },
  { id: "BATTERY_DISCHARGE", label: "BATTERY_DISCHARGE", x: 240, y: 200 },
];

const SAFE_BUBBLE = { id: "SAFE_MODE", label: "SAFE_MODE", x: 520, y: 200 };

const SAFE_REASON_NAMES = {
  0: "NONE",
  1: "BATTERY_BELOW_MINIMUM",
  2: "TEMPERATURE_OUT_OF_RANGE",
  3: "OBC_HEARTBEAT_TIMEOUT",
};

let scenarios = [];
let scenarioRowsByName = new Map();

// Off-before-switch UX gate: blocks Enter State Test Mode (and the scenario
// Run buttons through it) until the user clicks Off once on this page load.
// Switching pages (full page navigation) resets the flag.
let offClickedSinceLoad = false;

// ── API helpers ───────────────────────────────────────────────────────────

async function getJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`GET ${url} → ${response.status}`);
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

// ── DOM lookup shortcuts ──────────────────────────────────────────────────

const el = (id) => document.getElementById(id);

// ── Initialization ────────────────────────────────────────────────────────

async function initializePage() {
  await loadScenarios();
  bindHeaderButtons();
  drawStateDiagram(null, false);
  setInterval(pollStatusAndRender, 300);
  await pollStatusAndRender();
}

async function loadScenarios() {
  const data = await getJson("/api/state/scenarios");
  scenarios = data.scenarios || [];
  renderScenarioList();
}

function renderScenarioList() {
  const list = el("scenarioList");
  list.innerHTML = "";
  scenarioRowsByName = new Map();

  scenarios.forEach((scenario) => {
    const row = document.createElement("div");
    row.className = "scenario-row";

    const name = document.createElement("div");
    name.className = "name";
    name.textContent = scenario.name;
    name.title = scenario.name;

    const runButton = document.createElement("button");
    runButton.type = "button";
    runButton.textContent = "Run";
    runButton.disabled = true;
    runButton.addEventListener("click", () => runOneScenario(scenario.name));

    const pill = document.createElement("span");
    pill.className = "pill";

    row.append(name, runButton, pill);
    list.append(row);

    scenarioRowsByName.set(scenario.name, { row, runButton, pill });
  });
}

function bindHeaderButtons() {
  el("connectButton").addEventListener("click", onClickConnect);
  el("offButton").addEventListener("click", onClickOff);
  el("enterTestButton").addEventListener("click", onClickEnterStateTestMode);
  el("runAllButton").addEventListener("click", onClickRunAll);
}

// ── Button handlers ───────────────────────────────────────────────────────

async function onClickConnect() {
  const port = el("serialPort").value.trim() || "COM3";
  try {
    await postJson("/api/connect", { port });
  } catch (exc) {
    alert("Connect failed: " + exc.message);
  }
}

async function onClickOff() {
  try {
    await postJson("/api/off", {});
    offClickedSinceLoad = true;
    updateOffGateBanner();
  } catch (exc) {
    alert("Off failed: " + exc.message);
  }
}

function updateOffGateBanner() {
  const banner = el("offGateBanner");
  if (!banner) return;
  banner.classList.toggle("hidden", offClickedSinceLoad);
}

async function onClickEnterStateTestMode() {
  try {
    await postJson("/api/state/enter_state_test_mode", {});
  } catch (exc) {
    alert("Enter state-test mode failed: " + exc.message);
  }
}

async function onClickRunAll() {
  setAllRunButtonsDisabled(true);
  scenarios.forEach((scenario) => {
    setScenarioPillState(scenario.name, "pending");
  });

  try {
    for (const scenario of scenarios) {
      setScenarioPillState(scenario.name, "running");
      const data = await postJson("/api/state/run_scenario", { name: scenario.name });
      applyScenarioResult(data.result);
    }
  } catch (exc) {
    alert("Run all failed: " + exc.message);
  } finally {
    setAllRunButtonsDisabled(false);
  }
}

async function runOneScenario(name) {
  setScenarioPillState(name, "running");
  try {
    const data = await postJson("/api/state/run_scenario", { name });
    applyScenarioResult(data.result);
  } catch (exc) {
    setScenarioPillState(name, "fail");
    alert(`Run "${name}" failed: ` + exc.message);
  }
}

function setAllRunButtonsDisabled(disabled) {
  scenarioRowsByName.forEach(({ runButton }) => {
    runButton.disabled = disabled;
  });
  el("runAllButton").disabled = disabled;
}

function setScenarioPillState(name, state) {
  const row = scenarioRowsByName.get(name);
  if (!row) return;
  row.pill.className = "pill";
  if (state === "pass") row.pill.classList.add("pass");
  else if (state === "fail") row.pill.classList.add("fail");
  else if (state === "running") row.pill.classList.add("running");
}

function applyScenarioResult(result) {
  if (!result) return;
  setScenarioPillState(result.name, result.passed ? "pass" : "fail");
  el("commandPreview").textContent = result.command || "—";
  drawDiffTable(result);
  drawStateDiagram(
    result.snapshot ? result.snapshot.pcu_mode : null,
    result.snapshot ? Boolean(result.snapshot.safe_alert) : false
  );
}

// ── Diff table rendering ─────────────────────────────────────────────────

function drawDiffTable(result) {
  const body = el("diffTableBody");
  body.innerHTML = "";
  const expected = result.expected || {};
  const observed = result.observed || {};
  const fields = Object.keys(expected);

  if (fields.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.className = "empty";
    cell.textContent = "Scenario asserts on no fields.";
    row.append(cell);
    body.append(row);
    return;
  }

  for (const field of fields) {
    const row = document.createElement("tr");
    const expectedValue = expected[field];
    const observedValue = observed[field];
    const ok = expectedValue === observedValue;
    row.innerHTML = `
      <td>${escapeHtml(field)}</td>
      <td>${escapeHtml(formatValue(expectedValue))}</td>
      <td>${escapeHtml(formatValue(observedValue))}</td>
      <td class="${ok ? "ok" : "bad"}">${ok ? "OK" : "MISMATCH"}</td>
    `;
    body.append(row);
  }
}

function formatValue(value) {
  if (value === undefined) return "—";
  if (value === null) return "null";
  if (typeof value === "number" && value > 9 && /load|fault/i.test("")) {
    return "0x" + value.toString(16);
  }
  return String(value);
}

function escapeHtml(text) {
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

// ── State diagram (SVG) ───────────────────────────────────────────────────

function drawStateDiagram(currentMode, safeIsActive) {
  const svg = el("stateDiagram");
  svg.innerHTML = "";
  const ns = "http://www.w3.org/2000/svg";

  for (const bubble of PCU_MODES_FOR_DIAGRAM) {
    const isCurrent = bubble.id === currentMode && !safeIsActive;
    drawBubble(svg, ns, bubble, isCurrent ? "current" : "");
  }
  drawBubble(svg, ns, SAFE_BUBBLE, safeIsActive ? "safe" : "");
}

function drawBubble(svg, ns, bubble, extraClass) {
  const rect = document.createElementNS(ns, "rect");
  rect.setAttribute("x", String(bubble.x - 90));
  rect.setAttribute("y", String(bubble.y - 24));
  rect.setAttribute("width", "180");
  rect.setAttribute("height", "48");
  rect.setAttribute("rx", "12");
  rect.setAttribute("class", "bubble " + extraClass);
  svg.append(rect);

  const text = document.createElementNS(ns, "text");
  text.setAttribute("x", String(bubble.x));
  text.setAttribute("y", String(bubble.y + 5));
  text.setAttribute("text-anchor", "middle");
  text.textContent = bubble.label;
  svg.append(text);
}

// ── Status polling ────────────────────────────────────────────────────────

async function pollStatusAndRender() {
  let snapshot;
  try {
    snapshot = await getJson("/api/status");
  } catch (exc) {
    setConnectionStatus("Connection error: " + exc.message, false);
    return;
  }
  updateHeader(snapshot);
  updateLiveTelemetry(snapshot.telemetry || {});
  updateCommandLog(snapshot);
  applyLastScenarioRunIfNew(snapshot.last_scenario_run);
  applyScenarioResults(snapshot.scenario_results || {});

  // Diagram tracks live telemetry too (so it animates while idle).
  drawStateDiagram(
    snapshot.telemetry ? snapshot.telemetry.pcu_mode : null,
    snapshot.telemetry ? Boolean(snapshot.telemetry.safe_alert) : false
  );
}

function updateHeader(snapshot) {
  if (snapshot.connected) {
    setConnectionStatus(
      `Connected to ${snapshot.serial_port}`
        + (snapshot.in_state_test_mode ? " · in state-test mode" : ""),
      true
    );
  } else {
    const error = snapshot.connection_error
      ? `: ${snapshot.connection_error}`
      : "";
    setConnectionStatus("Disconnected" + error, false);
  }

  el("enterTestButton").disabled = !snapshot.connected || !offClickedSinceLoad;
  const ready = snapshot.connected && snapshot.in_state_test_mode && offClickedSinceLoad;
  scenarioRowsByName.forEach(({ runButton }) => {
    runButton.disabled = !ready;
  });
  el("runAllButton").disabled = !ready;
  updateOffGateBanner();
}

function setConnectionStatus(text, connected) {
  el("connectionStatus").textContent = text;
  el("connectionStatus").style.color = connected ? "#cdeacd" : "#f3c0ba";
}

function updateLiveTelemetry(telemetry) {
  el("liveModeValue").textContent = telemetry.pcu_mode || "—";
  el("liveSafeAlertValue").textContent =
    telemetry.safe_alert === undefined ? "—" : String(telemetry.safe_alert);
  el("liveSafeReasonValue").textContent =
    telemetry.safe_reason === undefined
      ? "—"
      : `${telemetry.safe_reason} (${SAFE_REASON_NAMES[telemetry.safe_reason] || "?"})`;
  el("livePanelEfuseValue").textContent =
    telemetry.panel_efuse === undefined ? "—" : String(telemetry.panel_efuse);
  el("liveHeaterValue").textContent =
    telemetry.heater === undefined ? "—" : String(telemetry.heater);
  el("liveLoadMaskValue").textContent =
    telemetry.load_mask === undefined ? "—" : "0x" + telemetry.load_mask.toString(16);
  el("liveStateDutyValue").textContent =
    telemetry.state_duty === undefined ? "—" : String(telemetry.state_duty);
  el("livePwmEnabledValue").textContent =
    telemetry.pwm_enabled === undefined ? "—" : String(telemetry.pwm_enabled);
}

function updateCommandLog(snapshot) {
  const commands = snapshot.commands || [];
  const lines = commands
    .slice(-200)
    .map((entry) => {
      const time = new Date(entry.time * 1000).toLocaleTimeString();
      return `[${time}] ${entry.direction}: ${entry.text}`;
    });
  el("commandLog").textContent = lines.join("\n");
  const logElement = el("commandLog");
  logElement.scrollTop = logElement.scrollHeight;
}

let lastAppliedScenarioTime = 0;
function applyLastScenarioRunIfNew(lastRun) {
  if (!lastRun) return;
  if (lastRun.time <= lastAppliedScenarioTime) return;
  lastAppliedScenarioTime = lastRun.time;
  // The result was already applied locally when the request returned, but
  // when the user runs scenarios via /api/run_all_scenarios in a future
  // variant we still want the table to reflect the last one.
  drawDiffTable(lastRun);
}

function applyScenarioResults(resultsByName) {
  Object.entries(resultsByName).forEach(([name, result]) => {
    setScenarioPillState(name, result.passed ? "pass" : "fail");
  });
}

// ── Boot ──────────────────────────────────────────────────────────────────

initializePage().catch((exc) => {
  alert("Page failed to initialize: " + exc.message);
});
