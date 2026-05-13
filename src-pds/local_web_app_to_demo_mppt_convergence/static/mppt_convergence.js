const controls = {
  a: document.getElementById("coefficientA"),
  b: document.getElementById("coefficientB"),
  c: document.getElementById("coefficientC"),
  vMin: document.getElementById("minimumPanelVoltage"),
  vMax: document.getElementById("maximumPanelVoltage"),
  battery: document.getElementById("batteryVoltage"),
};

const outputs = {
  a: document.getElementById("coefficientAValue"),
  b: document.getElementById("coefficientBValue"),
  c: document.getElementById("coefficientCValue"),
  vMin: document.getElementById("minimumPanelVoltageValue"),
  vMax: document.getElementById("maximumPanelVoltageValue"),
  battery: document.getElementById("batteryVoltageValue"),
};

let latestSnapshot = {
  connected: false,
  connection_error: "",
  telemetry: {},
  history: [],
  commands: [],
};

function readCurveFromControls() {
  return {
    a: Number(controls.a.value),
    b: Number(controls.b.value),
    c: Number(controls.c.value),
    v_min: Number(controls.vMin.value),
    v_max: Number(controls.vMax.value),
    battery_voltage: Number(controls.battery.value),
  };
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
  if (curve.v_min >= curve.v_max) return "Vmin must be lower than Vmax";
  if (mpp.power <= 0) return "The curve has no positive power";
  if (Math.max(...samples.map(point => point.current)) > 5000) return "Current exceeds 5 A";
  const lowestReachablePanelVoltage = curve.battery_voltage / 0.95;
  if (mpp.voltage < lowestReachablePanelVoltage) {
    return "MPP is below the panel voltage reachable by the buck converter";
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
  const curve = readCurveFromControls();
  outputs.a.textContent = formatNumber(curve.a, 1);
  outputs.b.textContent = formatNumber(curve.b, 0);
  outputs.c.textContent = formatNumber(curve.c, 0);
  outputs.vMin.textContent = formatNumber(curve.v_min, 1);
  outputs.vMax.textContent = formatNumber(curve.v_max, 1);
  outputs.battery.textContent = formatNumber(curve.battery_voltage, 1);
}

function updateCurveState() {
  const curve = readCurveFromControls();
  const samples = buildCurveSamples(curve);
  const mpp = findMaximumPowerPoint(samples);
  const error = validateCurve(curve, samples, mpp);
  const state = document.getElementById("curveState");
  const startButton = document.getElementById("startButton");

  state.textContent = error || "Valid curve";
  state.classList.toggle("invalid", Boolean(error));
  startButton.disabled = Boolean(error);
  document.getElementById("commandPreview").textContent = startCommandText(curve);
  document.getElementById("mppValue").textContent =
    `${formatNumber(mpp.voltage, 2)} V / ${formatNumber(mpp.power / 1000, 2)} W`;
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
  const curve = readCurveFromControls();
  const samples = buildCurveSamples(curve);
  const mpp = findMaximumPowerPoint(samples);
  const boardPoint = latestBoardPoint();
  drawIvGraph(curve, samples, boardPoint);
  drawPvGraph(curve, samples, mpp, boardPoint);
}

function drawIvGraph(curve, samples, boardPoint) {
  const graph = canvasContext("ivGraph");
  const axes = plotAxes(graph, "Panel voltage (V)", "Panel current (mA)");
  const maxCurrent = Math.max(100, ...samples.map(point => point.current));

  drawGrid(graph, axes);
  drawLine(graph, axes, samples, point => point.voltage, point => point.current, {
    xMax: curve.v_max,
    yMax: maxCurrent,
    color: "#1264a3",
  });
  drawAxisLabels(graph, axes, curve.v_max, maxCurrent, "V", "mA");
  drawBoardPath(graph, axes, curve.v_max, maxCurrent, point => point.current);

  if (boardPoint) {
    drawPoint(graph, axes, boardPoint.voltage, boardPoint.current, curve.v_max, maxCurrent, "#b42318", "board");
  }
}

function drawPvGraph(curve, samples, mpp, boardPoint) {
  const graph = canvasContext("pvGraph");
  const axes = plotAxes(graph, "Panel voltage (V)", "Panel power (W)");
  const maxPower = Math.max(100, ...samples.map(point => point.power));

  drawGrid(graph, axes);
  drawLine(graph, axes, samples, point => point.voltage, point => point.power, {
    xMax: curve.v_max,
    yMax: maxPower,
    color: "#b7791f",
  });
  drawBoardPath(graph, axes, curve.v_max, maxPower, point => point.power);
  drawPoint(graph, axes, mpp.voltage, mpp.power, curve.v_max, maxPower, "#20845c", "MPP");
  if (boardPoint) {
    drawPoint(graph, axes, boardPoint.voltage, boardPoint.power, curve.v_max, maxPower, "#b42318", "board");
  }
  drawAxisLabels(graph, axes, curve.v_max, maxPower, "V", "mW");
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
  if (!telemetry.panel_voltage_mv) return null;
  return {
    voltage: Number(telemetry.panel_voltage_mv) / 1000,
    current: Number(telemetry.panel_current_ma || 0),
    power: Number(telemetry.panel_power_mw || 0),
  };
}

function boardHistoryPoints() {
  const history = latestSnapshot.history || [];
  return history.map(item => ({
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
    await refreshStatus();
  } catch (error) {
    showConnectionError(error);
  } finally {
    setButtonBusy("offButton", false);
  }
}

async function startMpptTest() {
  const curve = readCurveFromControls();
  setButtonBusy("startButton", true);
  try {
    await api("/api/start_mppt", curve);
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
  renderBoardValues();
  renderCommandLog();
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
  document.getElementById("modeValue").textContent = telemetry.mode || "-";
  document.getElementById("dutyValue").textContent = telemetry.mppt_duty ?? telemetry.applied_pwm ?? "-";
  document.getElementById("panelVoltageValue").textContent =
    telemetry.panel_voltage_mv ? `${(telemetry.panel_voltage_mv / 1000).toFixed(2)} V` : "-";
  document.getElementById("panelCurrentValue").textContent =
    telemetry.panel_current_ma ? `${telemetry.panel_current_ma} mA` : "-";
  document.getElementById("panelPowerValue").textContent =
    telemetry.panel_power_mw ? `${(telemetry.panel_power_mw / 1000).toFixed(2)} W` : "-";
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
  const log = document.getElementById("commandLog");
  log.textContent = [
    "Recent serial lines:",
    ...latest,
    "",
    "Commands sent by this page:",
    ...pcCommands,
  ].join("\n");
  log.scrollTop = log.scrollHeight;
}

function handleControlChange() {
  updateControlText();
  updateCurveState();
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
