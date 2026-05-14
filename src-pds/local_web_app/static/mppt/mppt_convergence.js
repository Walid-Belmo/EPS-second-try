const controls = {
  isc: document.getElementById("shortCircuitCurrent"),
  voc: document.getElementById("openCircuitVoltage"),
  vMpp: document.getElementById("peakPowerVoltage"),
  vMin: document.getElementById("minimumPanelVoltage"),
  vMax: document.getElementById("maximumPanelVoltage"),
  battery: document.getElementById("batteryVoltage"),
};

const outputs = {
  isc: document.getElementById("shortCircuitCurrentValue"),
  voc: document.getElementById("openCircuitVoltageValue"),
  vMpp: document.getElementById("peakPowerVoltageValue"),
  vMin: document.getElementById("minimumPanelVoltageValue"),
  vMax: document.getElementById("maximumPanelVoltageValue"),
  battery: document.getElementById("batteryVoltageValue"),
};

const vMppFractionMin = 0.52;
const vMppFractionMax = 0.65;

let latestSnapshot = {
  connected: false,
  connection_error: "",
  telemetry: {},
  active_curve: null,
  requested_curve: null,
  history: [],
  commands: [],
  debug_events: [],
};

// Off-before-switch UX gate: blocks Start until the user clicks Off once on
// this page load. Switching pages (full page navigation) resets the flag.
let offClickedSinceLoad = false;

const fixedGraphScale = {
  voltageMaxV: 18,
  currentMaxMa: 5000,
  powerMaxW: 125,
};

function readCurveFromControls() {
  const isc = Number(controls.isc.value);
  const voc = Number(controls.voc.value);
  const vMpp = Number(controls.vMpp.value);
  const { a, b, c } = quadraticFromPhysicalParameters(isc, voc, vMpp);
  return {
    a,
    b,
    c,
    v_min: Number(controls.vMin.value),
    v_max: Number(controls.vMax.value),
    battery_voltage: Number(controls.battery.value),
  };
}

function quadraticFromPhysicalParameters(isc, voc, vMpp) {
  const denominator = vMpp * (3 * vMpp - 2 * voc) * voc;
  const a = (isc * (2 * vMpp - voc)) / denominator;
  const b = -a * voc - isc / voc;
  const c = isc;
  return { a, b, c };
}

function physicalFromQuadratic(curve) {
  const a = Number(curve.a);
  const b = Number(curve.b);
  const c = Number(curve.c);
  const discriminantVoc = b * b - 4 * a * c;
  const voc = discriminantVoc >= 0
    ? (-b - Math.sqrt(discriminantVoc)) / (2 * a)
    : NaN;
  const discriminantMpp = 4 * b * b - 12 * a * c;
  const vMpp = discriminantMpp >= 0
    ? (-2 * b - Math.sqrt(discriminantMpp)) / (6 * a)
    : NaN;
  return { isc: c, voc, v_mpp: vMpp };
}

function currentForVoltage(voltage, curve) {
  return Math.max(0, curve.a * voltage * voltage + curve.b * voltage + curve.c);
}

function powerForVoltage(voltage, curve) {
  return voltage * currentForVoltage(voltage, curve);
}

function buildCurveSamples(curve) {
  const samples = [];
  const steps = 360;
  for (let index = 0; index <= steps; index += 1) {
    const ratio = index / steps;
    const voltage = curve.v_min + (curve.v_max - curve.v_min) * ratio;
    const current = currentForVoltage(voltage, curve);
    samples.push({ voltage, current, power: voltage * current });
  }
  return samples;
}

function findMaximumPowerPoint(samples) {
  return samples.reduce((best, point) => (point.power > best.power ? point : best), samples[0]);
}

function validateCurve(curve, samples, mpp) {
  const voc = Number(controls.voc.value);
  const vMpp = Number(controls.vMpp.value);
  if (vMpp <= voc / 2 || vMpp >= (2 * voc) / 3) {
    return "V_mpp must be between Voc/2 and 2*Voc/3";
  }
  if (curve.v_min >= curve.v_max) return "Vmin must be lower than Vmax";
  if (mpp.power <= 0) return "The curve has no positive power";
  if (Math.max(...samples.map(point => point.current)) > 5000) return "Current exceeds 5 A";
  const lowestReachablePanelVoltage = curve.battery_voltage / 0.95;
  if (mpp.voltage < lowestReachablePanelVoltage) {
    return "MPP is below the panel voltage reachable by the buck converter";
  }
  const startingPanelVoltage = curve.battery_voltage / 0.5;
  const startingCurrent = currentForVoltage(startingPanelVoltage, curve);
  if (startingCurrent <= 100) {
    return "Curve has almost no current at the MPPT starting voltage";
  }
  if (Math.abs(mpp.voltage - curve.v_min) < 0.25 || Math.abs(mpp.voltage - curve.v_max) < 0.25) {
    return "MPP is too close to a voltage limit";
  }
  return "";
}

function startCommandText(curve) {
  return [
    "start_mppt_demo curve=quadratic",
    `a=${formatNumber(curve.a, 1)}`,
    `b=${formatNumber(curve.b, 0)}`,
    `c=${formatNumber(curve.c, 0)}`,
    `v_min=${Math.round(curve.v_min * 1000)}`,
    `v_max=${Math.round(curve.v_max * 1000)}`,
    `battery_voltage=${Math.round(curve.battery_voltage * 1000)}`,
  ].join(" ");
}

function formatNumber(value, decimals) {
  return Number(value).toFixed(decimals).replace(/\.0$/, "");
}

function updateControlText() {
  outputs.isc.textContent = formatNumber(Number(controls.isc.value), 0);
  outputs.voc.textContent = formatNumber(Number(controls.voc.value), 1);
  outputs.vMpp.textContent = formatNumber(Number(controls.vMpp.value), 1);
  outputs.vMin.textContent = formatNumber(Number(controls.vMin.value), 1);
  outputs.vMax.textContent = formatNumber(Number(controls.vMax.value), 1);
  outputs.battery.textContent = formatNumber(Number(controls.battery.value), 1);
}

function clampVMppToVocRange() {
  const voc = Number(controls.voc.value);
  const newMin = voc * vMppFractionMin;
  const newMax = voc * vMppFractionMax;
  controls.vMpp.min = newMin.toFixed(2);
  controls.vMpp.max = newMax.toFixed(2);
  const current = Number(controls.vMpp.value);
  if (current < newMin) controls.vMpp.value = newMin.toFixed(2);
  else if (current > newMax) controls.vMpp.value = newMax.toFixed(2);
}

function updateCurveState() {
  const controlCurve = readCurveFromControls();
  const controlSamples = buildCurveSamples(controlCurve);
  const controlMpp = findMaximumPowerPoint(controlSamples);
  const error = validateCurve(controlCurve, controlSamples, controlMpp);
  const state = document.getElementById("curveState");
  const startButton = document.getElementById("startButton");
  const running = Boolean(latestSnapshot.mppt_running);
  const boardPoint = latestBoardPoint();

  state.textContent = running
    ? (boardPoint ? "Running" : (latestSnapshot.active_curve ? "Waiting" : "Starting"))
    : (error || "Valid curve");
  state.classList.toggle("invalid", Boolean(error) && !running);
  startButton.disabled = running || Boolean(error) || !offClickedSinceLoad;
  document.getElementById("commandPreview").textContent = startCommandText(controlCurve);
  document.getElementById("mppValue").textContent = boardPoint ? "valid" : "-";
}

function canvasContext(id) {
  const canvas = document.getElementById(id);
  const rect = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.round(rect.width * scale);
  canvas.height = Math.round(rect.height * scale);
  const context = canvas.getContext("2d");
  context.setTransform(scale, 0, 0, scale, 0, 0);
  return { canvas, context, width: rect.width, height: rect.height };
}

function drawBothGraphs() {
  const boardPoint = latestBoardPoint();
  const sliderCurve = readCurveFromControls();
  const modelSamples = buildCurveSamples(sliderCurve);
  drawIvGraph(boardPoint, sliderCurve, modelSamples);
  drawPvGraph(boardPoint, sliderCurve, modelSamples);
}

function curvesMatch(first, second) {
  return (
    Math.abs(Number(first.a) - Number(second.a)) < 0.02 &&
    Math.abs(Number(first.b) - Number(second.b)) < 0.02 &&
    Math.round(Number(first.c)) === Math.round(Number(second.c)) &&
    Math.abs(Number(first.v_min) - Number(second.v_min)) < 0.002 &&
    Math.abs(Number(first.v_max) - Number(second.v_max)) < 0.002 &&
    Math.abs(Number(first.battery_voltage) - Number(second.battery_voltage)) < 0.002
  );
}

function drawIvGraph(boardPoint, modelCurve, modelSamples) {
  const graph = canvasContext("ivGraph");
  const axes = plotAxes(graph, "Panel voltage (V)", "Panel current (mA)");
  const maxVoltage = graphVoltageMax(modelCurve, boardPoint);
  const maxCurrent = fixedGraphScale.currentMaxMa;

  drawGrid(graph, axes);
  drawLine(
    graph,
    axes,
    modelSamples,
    point => point.voltage,
    point => point.current,
    { color: "#1264a3", xMax: maxVoltage, yMax: maxCurrent },
  );
  drawBoardPath(graph, axes, maxVoltage, maxCurrent, point => point.current);

  if (boardPoint) {
    drawPoint(graph, axes, boardPoint.voltage, boardPoint.current, maxVoltage, maxCurrent, "#b42318", "board");
  }
  drawAxisLabels(graph, axes, maxVoltage, maxCurrent, "V", "mA");
}

function drawPvGraph(boardPoint, modelCurve, modelSamples) {
  const graph = canvasContext("pvGraph");
  const axes = plotAxes(graph, "Panel voltage (V)", "Panel power (W)");
  const maxVoltage = graphVoltageMax(modelCurve, boardPoint);
  const maxPower = fixedGraphScale.powerMaxW;

  drawGrid(graph, axes);
  drawLine(
    graph,
    axes,
    modelSamples,
    point => point.voltage,
    point => point.power / 1000,
    { color: "#b7791f", xMax: maxVoltage, yMax: maxPower },
  );
  drawBoardPath(graph, axes, maxVoltage, maxPower, point => point.power / 1000);
  if (boardPoint) {
    drawPoint(graph, axes, boardPoint.voltage, boardPoint.power / 1000, maxVoltage, maxPower, "#b42318", "board");
  }
  drawAxisLabels(graph, axes, maxVoltage, maxPower, "V", "W");
}

function graphVoltageMax(modelCurve, boardPoint) {
  const values = [fixedGraphScale.voltageMaxV];
  if (modelCurve) values.push(Number(modelCurve.v_max || 0));
  if (boardPoint) values.push(Number(boardPoint.voltage || 0));
  return Math.max(...values);
}

function plotAxes(graph, xLabel, yLabel) {
  const axes = { left: 58, right: 18, top: 18, bottom: 44 };
  const context = graph.context;
  context.clearRect(0, 0, graph.width, graph.height);
  context.fillStyle = "#ffffff";
  context.fillRect(0, 0, graph.width, graph.height);
  context.strokeStyle = "#95a3b3";
  context.lineWidth = 1;
  context.strokeRect(axes.left, axes.top, plotWidth(graph, axes), plotHeight(graph, axes));
  context.fillStyle = "#536170";
  context.font = "12px Arial";
  context.fillText(xLabel, axes.left, graph.height - 12);
  context.save();
  context.translate(15, graph.height - axes.bottom);
  context.rotate(-Math.PI / 2);
  context.fillText(yLabel, 0, 0);
  context.restore();
  return axes;
}

function drawGrid(graph, axes) {
  const context = graph.context;
  context.strokeStyle = "#e2e8ee";
  context.lineWidth = 1;
  for (let tick = 1; tick < 5; tick += 1) {
    const x = axes.left + (plotWidth(graph, axes) * tick) / 5;
    const y = axes.top + (plotHeight(graph, axes) * tick) / 5;
    context.beginPath();
    context.moveTo(x, axes.top);
    context.lineTo(x, axes.top + plotHeight(graph, axes));
    context.stroke();
    context.beginPath();
    context.moveTo(axes.left, y);
    context.lineTo(axes.left + plotWidth(graph, axes), y);
    context.stroke();
  }
}

function drawLine(graph, axes, samples, xValue, yValue, style) {
  if (samples.length === 0) return;
  const context = graph.context;
  context.strokeStyle = style.color;
  context.lineWidth = 2;
  context.beginPath();
  samples.forEach((point, index) => {
    const x = xCoordinate(graph, axes, xValue(point), style.xMax);
    const y = yCoordinate(graph, axes, yValue(point), style.yMax);
    if (index === 0) context.moveTo(x, y);
    else context.lineTo(x, y);
  });
  context.stroke();
}

function drawPoint(graph, axes, xValue, yValue, xMax, yMax, color, label) {
  const context = graph.context;
  const x = xCoordinate(graph, axes, xValue, xMax);
  const y = yCoordinate(graph, axes, yValue, yMax);
  context.fillStyle = color;
  context.beginPath();
  context.arc(x, y, 5, 0, Math.PI * 2);
  context.fill();
  context.font = "12px Arial";
  context.fillText(label, x + 8, Math.max(15, y - 8));
}

function drawBoardPath(graph, axes, xMax, yMax, yValue) {
  const points = boardHistoryPoints();
  const context = graph.context;
  context.fillStyle = "rgba(180, 35, 24, 0.35)";
  points.forEach(point => {
    const x = xCoordinate(graph, axes, point.voltage, xMax);
    const y = yCoordinate(graph, axes, yValue(point), yMax);
    context.beginPath();
    context.arc(x, y, 3, 0, Math.PI * 2);
    context.fill();
  });
}

function drawAxisLabels(graph, axes, xMax, yMax, xUnit, yUnit) {
  const context = graph.context;
  context.fillStyle = "#637083";
  context.font = "11px Arial";
  for (let tick = 0; tick <= 5; tick += 1) {
    const xValue = (xMax * tick) / 5;
    const yValue = (yMax * tick) / 5;
    const x = axes.left + (plotWidth(graph, axes) * tick) / 5;
    const y = axes.top + plotHeight(graph, axes) - (plotHeight(graph, axes) * tick) / 5;
    context.fillText(`${formatTick(xValue)} ${xUnit}`, x - 16, graph.height - 25);
    context.fillText(`${formatTick(yValue)} ${yUnit}`, 20, y + 4);
  }
}

function formatTick(value) {
  if (value >= 1000) {
    return (value / 1000).toFixed(1).replace(/\.0$/, "") + "k";
  }
  if (value >= 10) return String(Math.round(value));
  return Number(value).toFixed(1);
}

function xCoordinate(graph, axes, value, maxValue) {
  return axes.left + (Math.max(0, Math.min(value, maxValue)) / maxValue) * plotWidth(graph, axes);
}

function yCoordinate(graph, axes, value, maxValue) {
  return axes.top + plotHeight(graph, axes) -
    (Math.max(0, Math.min(value, maxValue)) / maxValue) * plotHeight(graph, axes);
}

function plotWidth(graph, axes) {
  return graph.width - axes.left - axes.right;
}

function plotHeight(graph, axes) {
  return graph.height - axes.top - axes.bottom;
}

function latestBoardPoint() {
  const telemetry = latestSnapshot.telemetry || {};
  if (telemetry.mppt_sample_valid === 0) return null;
  if (telemetry.panel_voltage_mv === undefined) return null;
  return {
    voltage: Number(telemetry.panel_voltage_mv) / 1000,
    current: Number(telemetry.panel_current_ma || 0),
    power: Number(telemetry.panel_power_mw || 0),
    duty: telemetry.mppt_duty ?? telemetry.applied_pwm ?? null,
  };
}

function boardHistoryPoints() {
  const history = latestSnapshot.history || [];
  return history
    .filter(item => item.panel_voltage_mv !== undefined)
    .map(item => ({
      voltage: Number(item.panel_voltage_mv || 0) / 1000,
      current: Number(item.panel_current_ma || 0),
      power: Number(item.panel_power_mw || 0),
    }));
}

async function api(path, body) {
  const response = await fetch(path, {
    method: body === undefined ? "GET" : "POST",
    headers: body === undefined ? {} : { "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const value = await response.json();
  if (!response.ok || value.ok === false) {
    throw new Error(value.error || `HTTP ${response.status}`);
  }
  return value;
}

async function connectToEsp32() {
  setButtonBusy("connectButton", true);
  try {
    await api("/api/connect", { port: document.getElementById("serialPort").value });
    await refreshStatus();
  } catch (error) {
    showConnectionError(error);
  } finally {
    setButtonBusy("connectButton", false);
  }
}

async function sendOff() {
  setButtonBusy("offButton", true);
  try {
    await api("/api/off", {});
    offClickedSinceLoad = true;
    updateOffGateBanner();
    await refreshStatus();
  } catch (error) {
    showConnectionError(error);
  } finally {
    setButtonBusy("offButton", false);
  }
}

function updateOffGateBanner() {
  const banner = document.getElementById("offGateBanner");
  if (!banner) return;
  banner.classList.toggle("hidden", offClickedSinceLoad);
}

async function startMpptTest() {
  const curve = readCurveFromControls();
  setButtonBusy("startButton", true);
  try {
    await api("/api/mppt/start_mppt", curve);
    await refreshStatus();
  } catch (error) {
    showConnectionError(error);
  } finally {
    setButtonBusy("startButton", false);
  }
}

function setButtonBusy(id, busy) {
  const button = document.getElementById(id);
  button.disabled = busy;
  if (!busy) updateCurveState();
}

function showConnectionError(error) {
  document.getElementById("connectionStatus").textContent = `Error: ${error.message}`;
}

async function refreshStatus() {
  latestSnapshot = await api("/api/status");
  renderStatus();
  drawBothGraphs();
}

function renderStatus() {
  renderConnectionStatus();
  updateCurveState();
  renderBoardValues();
  renderCommandLog();
  renderControlLockState();
}

function renderConnectionStatus() {
  const status = document.getElementById("connectionStatus");
  if (latestSnapshot.connected) {
    status.textContent = `Connected to ${latestSnapshot.serial_port}`;
    return;
  }
  const error = latestSnapshot.connection_error ? `: ${latestSnapshot.connection_error}` : "";
  status.textContent = `Disconnected${error}`;
}

function renderBoardValues() {
  const telemetry = latestSnapshot.telemetry || {};
  const hasSample = latestBoardPoint() !== null;
  document.getElementById("modeValue").textContent = telemetry.mode || "-";
  document.getElementById("dutyValue").textContent = telemetry.mppt_duty ?? telemetry.applied_pwm ?? "-";
  document.getElementById("panelVoltageValue").textContent =
    hasSample ? `${(telemetry.panel_voltage_mv / 1000).toFixed(2)} V` : "-";
  document.getElementById("panelCurrentValue").textContent =
    hasSample ? `${telemetry.panel_current_ma} mA` : "-";
  document.getElementById("panelPowerValue").textContent =
    hasSample ? `${(telemetry.panel_power_mw / 1000).toFixed(2)} W` : "-";
}

function renderControlLockState() {
  const running = Boolean(latestSnapshot.mppt_running);
  Object.values(controls).forEach(control => {
    control.disabled = running;
  });
  document.getElementById("startButton").disabled =
    running
    || document.getElementById("curveState").classList.contains("invalid")
    || !offClickedSinceLoad;
  document.getElementById("offButton").disabled = false;
  updateOffGateBanner();
}

function renderCommandLog() {
  const commands = latestSnapshot.commands || [];
  const pcCommands = commands
    .filter(item => item.direction === "PC -> ESP32")
    .slice(-8)
    .map(item => `${item.direction}: ${item.text}`);
  const latest = commands
    .slice(-24)
    .map(item => `${item.direction}: ${item.text}`);
  const debugEvents = (latestSnapshot.debug_events || [])
    .slice(-8)
    .map(formatDebugEvent);
  const log = document.getElementById("commandLog");
  log.textContent = [
    "Run diagnostics:",
    ...runDiagnosticsLines(),
    "",
    "Server events:",
    ...(debugEvents.length ? debugEvents : ["-"]),
    "",
    "Recent serial lines:",
    ...latest,
    "",
    "Commands sent by this page:",
    ...pcCommands,
  ].join("\n");
  log.scrollTop = log.scrollHeight;
}

function runDiagnosticsLines() {
  const boardPoint = latestBoardPoint();
  const sliderCurve = readCurveFromControls();
  const requestedCurve = latestSnapshot.requested_curve || null;
  const activeCurve = latestSnapshot.active_curve || null;
  const lines = [];

  if (!requestedCurve) {
    lines.push("Graph source: slider curve (Start not pressed)");
  } else if (!activeCurve) {
    lines.push("Graph source: slider curve (ESP32 not yet confirmed)");
  } else if (curvesMatch(activeCurve, sliderCurve)) {
    lines.push("Graph source: slider curve (ESP32 confirmed)");
  } else {
    const echoed = physicalFromQuadratic(activeCurve);
    lines.push(
      `Graph source: slider curve (ESP32 confirmed DIFFERENT curve - board is simulating Isc=${formatNumber(echoed.isc, 0)} mA Voc=${formatNumber(echoed.voc, 1)} V V_mpp=${formatNumber(echoed.v_mpp, 1)} V)`,
    );
  }

  lines.push(
    `Slider curve: Isc=${formatNumber(Number(controls.isc.value), 0)} mA Voc=${formatNumber(Number(controls.voc.value), 1)} V V_mpp=${formatNumber(Number(controls.vMpp.value), 1)} V`,
  );

  if (activeCurve) {
    const echoed = physicalFromQuadratic(activeCurve);
    lines.push(
      `ESP32 model: Isc=${formatNumber(echoed.isc, 0)} mA Voc=${formatNumber(echoed.voc, 1)} V V_mpp=${formatNumber(echoed.v_mpp, 1)} V (a=${formatNumber(activeCurve.a, 1)} b=${formatNumber(activeCurve.b, 0)} c=${formatNumber(activeCurve.c, 0)} battery=${formatNumber(activeCurve.battery_voltage, 1)} V)`,
    );
  } else if (requestedCurve) {
    lines.push("ESP32 model: not confirmed yet");
  }

  if (boardPoint) {
    const boardPowerW = boardPoint.power / 1000;
    const dutyText = boardPoint.duty === null ? "-" : String(boardPoint.duty);
    lines.push(
      `Board point: ${formatNumber(boardPoint.voltage, 2)} V / ${boardPoint.current} mA / ${formatNumber(boardPowerW, 2)} W / duty ${dutyText}`,
    );
  } else {
    lines.push("Board point: none yet");
  }

  return lines;
}

function formatDebugEvent(event) {
  const timeText = new Date(Number(event.time) * 1000).toLocaleTimeString();
  return `${timeText}: ${event.text}`;
}

function handleControlChange() {
  clampVMppToVocRange();
  updateControlText();
  updateCurveState();
  if (!latestSnapshot.active_curve) {
    latestSnapshot.history = [];
  }
  drawBothGraphs();
}

function installHandlers() {
  Object.values(controls).forEach(control => {
    control.addEventListener("input", handleControlChange);
  });
  document.getElementById("connectButton").addEventListener("click", connectToEsp32);
  document.getElementById("offButton").addEventListener("click", sendOff);
  document.getElementById("startButton").addEventListener("click", startMpptTest);
  window.addEventListener("resize", drawBothGraphs);
}

function startPolling() {
  window.setInterval(() => {
    refreshStatus().catch(error => showConnectionError(error));
  }, 500);
}

installHandlers();
handleControlChange();
refreshStatus().catch(error => showConnectionError(error));
startPolling();
