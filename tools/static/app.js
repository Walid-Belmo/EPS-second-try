// ═══════════════════════════════════════════════════════════════════════════
// CHESS EPS Simulation — Frontend Application
// Orbit animation, Chart.js plots, playback controller, live variables.
// ═══════════════════════════════════════════════════════════════════════════

// ── State ────────────────────────────────────────────────────────────────────

let simData = [];          // Array of data points from the API
let currentIndex = 0;      // Current playback position
let isPlaying = false;
let playSpeed = 10;        // Simulation seconds per real second
let animFrameId = null;
let lastTimestamp = null;
let timeAccumulator = 0;

const ORBIT_PERIOD_SECONDS = 94 * 60;  // 94 minutes
const SUN_PERIOD_SECONDS = 57 * 60;    // 57 minutes sunlit

const MODE_NAMES = ['MPPT_CHARGE', 'CV_FLOAT', 'SA_LOAD_FOLLOW', 'BATTERY_DISCHARGE'];
const MODE_COLORS = ['#22c55e', '#3b82f6', '#f59e0b', '#ef4444'];
const MODE_CLASSES = ['mode-mppt', 'mode-cvfloat', 'mode-safollow', 'mode-discharge'];

// ── Charts (Chart.js) ───────────────────────────────────────────────────────

let chartMode, chartVbat, chartSoc, chartPower, chartCurrent;

function initCharts() {
    // Global Chart.js dark theme
    Chart.defaults.backgroundColor = 'rgba(255,255,255,0.05)';
    Chart.defaults.borderColor = 'rgba(255,255,255,0.06)';
    Chart.defaults.color = '#6b7280';

    const commonOptions = {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        plugins: { legend: { display: false } },
        elements: { point: { radius: 0 }, line: { borderWidth: 1.5 } },
        scales: {
            x: {
                type: 'linear',
                grid: { color: 'rgba(255,255,255,0.04)' },
                ticks: { font: { size: 10 }, maxTicksLimit: 10 },
                title: { display: false }
            },
            y: {
                grid: { color: 'rgba(255,255,255,0.04)' },
                ticks: { font: { size: 10 } },
                grace: '10%'
            }
        }
    };

    // PCU Mode timeline (bar-style)
    chartMode = new Chart(document.getElementById('chart-mode'), {
        type: 'bar',
        data: {
            labels: [],
            datasets: [{
                data: [],
                backgroundColor: [],
                barPercentage: 1.0,
                categoryPercentage: 1.0,
            }]
        },
        options: {
            ...commonOptions,
            scales: {
                x: { ...commonOptions.scales.x, display: false },
                y: { display: false, min: 0, max: 1 }
            },
            plugins: {
                legend: { display: false },
                title: {
                    display: true, text: 'PCU Operating Mode',
                    color: '#6b7280', font: { size: 11 }, padding: 2
                }
            }
        }
    });

    // Battery Voltage
    chartVbat = new Chart(document.getElementById('chart-vbat'), {
        type: 'line',
        data: {
            datasets: [{
                label: 'Battery Voltage (mV)',
                data: [],
                borderColor: '#a855f7',
                tension: 0.1,
                parsing: false
            }]
        },
        options: {
            ...commonOptions,
            plugins: {
                legend: { display: true, labels: { color: '#a855f7', font: { size: 10 } } }
            }
        }
    });

    // SOC
    chartSoc = new Chart(document.getElementById('chart-soc'), {
        type: 'line',
        data: {
            datasets: [{
                label: 'State of Charge (%)',
                data: [],
                borderColor: '#3b82f6',
                tension: 0.1,
                parsing: false
            }]
        },
        options: {
            ...commonOptions,
            plugins: {
                legend: { display: true, labels: { color: '#3b82f6', font: { size: 10 } } }
            }
        }
    });

    // Panel Power + Duty Cycle
    chartPower = new Chart(document.getElementById('chart-power'), {
        type: 'line',
        data: {
            datasets: [
                {
                    label: 'Panel Power (W)',
                    data: [],
                    borderColor: '#22c55e',
                    tension: 0.1,
                    yAxisID: 'y',
                    parsing: false
                },
                {
                    label: 'Duty Cycle (%)',
                    data: [],
                    borderColor: '#f59e0b',
                    tension: 0.1,
                    yAxisID: 'y1',
                    parsing: false
                }
            ]
        },
        options: {
            ...commonOptions,
            plugins: {
                legend: { display: true, labels: { color: '#6b7280', font: { size: 10 } } }
            },
            scales: {
                x: commonOptions.scales.x,
                y: { ...commonOptions.scales.y, position: 'left' },
                y1: {
                    ...commonOptions.scales.y,
                    position: 'right',
                    grid: { drawOnChartArea: false }
                }
            }
        }
    });

    // Battery Current
    chartCurrent = new Chart(document.getElementById('chart-current'), {
        type: 'line',
        data: {
            datasets: [{
                label: 'Battery Current (mA)',
                data: [],
                borderColor: '#ef4444',
                tension: 0.1,
                parsing: false,
                fill: {
                    target: { value: 0 },
                    above: 'rgba(34,197,94,0.1)',
                    below: 'rgba(239,68,68,0.1)'
                }
            }]
        },
        options: {
            ...commonOptions,
            plugins: {
                legend: { display: true, labels: { color: '#ef4444', font: { size: 10 } } }
            }
        }
    });
}

function updateCharts(upToIndex) {
    const subset = simData.slice(0, upToIndex + 1);

    // PCU Mode timeline
    chartMode.data.labels = subset.map(d => d.t);
    chartMode.data.datasets[0].data = subset.map(() => 1);
    chartMode.data.datasets[0].backgroundColor = subset.map(d => MODE_COLORS[d.mode] || '#666');
    chartMode.update('none');

    // Battery Voltage
    chartVbat.data.datasets[0].data = subset.map(d => ({ x: d.t, y: d.vbat }));
    chartVbat.update('none');

    // SOC
    chartSoc.data.datasets[0].data = subset.map(d => ({ x: d.t, y: d.soc }));
    chartSoc.update('none');

    // Power + Duty
    chartPower.data.datasets[0].data = subset.map(d => ({ x: d.t, y: d.psol }));
    chartPower.data.datasets[1].data = subset.map(d => ({ x: d.t, y: d.duty }));
    chartPower.update('none');

    // Current
    chartCurrent.data.datasets[0].data = subset.map(d => ({ x: d.t, y: d.ibat }));
    chartCurrent.update('none');
}

// ── Orbit Canvas ────────────────────────────────────────────────────────────

const orbitCanvas = document.getElementById('orbit-canvas');
const ctx = orbitCanvas.getContext('2d');

// Deterministic stars
const stars = [];
for (let i = 0; i < 80; i++) {
    stars.push({
        x: ((i * 137.5 + 50) % 280),
        y: ((i * 97.3 + 30) % 280),
        r: (i % 3 === 0) ? 1.2 : 0.6,
        a: 0.3 + (i % 5) * 0.12
    });
}

function drawOrbit(simTime) {
    const W = 280, H = 280;
    const cx = W / 2, cy = H / 2;
    const earthR = 40;
    const orbitR = 90;

    ctx.clearRect(0, 0, W, H);

    // Background
    ctx.fillStyle = '#050510';
    ctx.fillRect(0, 0, W, H);

    // Stars
    stars.forEach(s => {
        ctx.fillStyle = `rgba(255,255,255,${s.a})`;
        ctx.beginPath();
        ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2);
        ctx.fill();
    });

    // Sun (right side)
    const sunX = W - 30, sunY = cy;
    const sunGlow = ctx.createRadialGradient(sunX, sunY, 5, sunX, sunY, 50);
    sunGlow.addColorStop(0, 'rgba(255,220,50,0.9)');
    sunGlow.addColorStop(0.3, 'rgba(255,180,30,0.3)');
    sunGlow.addColorStop(1, 'rgba(255,180,30,0)');
    ctx.fillStyle = sunGlow;
    ctx.fillRect(sunX - 50, sunY - 50, 100, 100);

    ctx.fillStyle = '#ffdd44';
    ctx.beginPath();
    ctx.arc(sunX, sunY, 8, 0, Math.PI * 2);
    ctx.fill();

    // Sun rays (subtle)
    ctx.strokeStyle = 'rgba(255,220,50,0.2)';
    ctx.lineWidth = 0.5;
    for (let a = 0; a < Math.PI * 2; a += Math.PI / 6) {
        ctx.beginPath();
        ctx.moveTo(sunX + Math.cos(a) * 12, sunY + Math.sin(a) * 12);
        ctx.lineTo(sunX + Math.cos(a) * 25, sunY + Math.sin(a) * 25);
        ctx.stroke();
    }

    // Orbit path (dashed)
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.arc(cx, cy, orbitR, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);

    // Earth
    const earthGrad = ctx.createRadialGradient(cx - 10, cy - 10, 5, cx, cy, earthR);
    earthGrad.addColorStop(0, '#4488cc');
    earthGrad.addColorStop(0.7, '#2255aa');
    earthGrad.addColorStop(1, '#112244');
    ctx.fillStyle = earthGrad;
    ctx.beginPath();
    ctx.arc(cx, cy, earthR, 0, Math.PI * 2);
    ctx.fill();

    // Earth shadow (left side, away from sun)
    ctx.save();
    ctx.beginPath();
    ctx.arc(cx, cy, earthR, 0, Math.PI * 2);
    ctx.clip();
    ctx.fillStyle = 'rgba(0,0,0,0.5)';
    ctx.fillRect(cx - earthR * 2, cy - earthR, earthR * 2 - 5, earthR * 2);
    ctx.restore();

    // Earth atmosphere glow
    ctx.strokeStyle = 'rgba(100,180,255,0.15)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(cx, cy, earthR + 2, 0, Math.PI * 2);
    ctx.stroke();

    // Shadow cone (extending from Earth, opposite to sun)
    ctx.fillStyle = 'rgba(0,0,20,0.2)';
    ctx.beginPath();
    ctx.moveTo(cx - earthR * 0.8, cy - earthR);
    ctx.lineTo(0, cy - earthR * 1.5);
    ctx.lineTo(0, cy + earthR * 1.5);
    ctx.lineTo(cx - earthR * 0.8, cy + earthR);
    ctx.closePath();
    ctx.fill();

    // Satellite position
    // Satellite starts at top (12 o'clock) and moves clockwise
    // Sun is on the right, so shadow is on the left
    const orbitFraction = (simTime % ORBIT_PERIOD_SECONDS) / ORBIT_PERIOD_SECONDS;
    const angle = -Math.PI / 2 + orbitFraction * Math.PI * 2; // start at top
    const satX = cx + Math.cos(angle) * orbitR;
    const satY = cy + Math.sin(angle) * orbitR;

    // Is satellite in eclipse? (left hemisphere = shadow)
    const sunFraction = SUN_PERIOD_SECONDS / ORBIT_PERIOD_SECONDS;
    const inEclipse = orbitFraction > sunFraction;

    // Satellite glow
    const satColor = inEclipse ? '#ff6666' : '#66ff99';
    const satGlow = ctx.createRadialGradient(satX, satY, 1, satX, satY, 12);
    satGlow.addColorStop(0, satColor);
    satGlow.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = satGlow;
    ctx.beginPath();
    ctx.arc(satX, satY, 12, 0, Math.PI * 2);
    ctx.fill();

    // Satellite dot
    ctx.fillStyle = '#ffffff';
    ctx.beginPath();
    ctx.arc(satX, satY, 3, 0, Math.PI * 2);
    ctx.fill();

    // Eclipse/Sun indicator
    ctx.fillStyle = inEclipse ? '#ff6666' : '#66ff99';
    ctx.font = '10px monospace';
    ctx.textAlign = 'center';
    ctx.fillText(inEclipse ? '● ECLIPSE' : '● SUNLIT', cx, H - 8);
}

// ── Live Variables ──────────────────────────────────────────────────────────

function updateLiveVars(d) {
    const modeEl = document.getElementById('var-mode');
    modeEl.textContent = MODE_NAMES[d.mode] || 'UNKNOWN';
    modeEl.className = 'var-value ' + (MODE_CLASSES[d.mode] || '');

    const safeEl = document.getElementById('var-safe');
    safeEl.textContent = d.safe ? 'YES — ACTIVE' : 'No';
    safeEl.className = 'var-value ' + (d.safe ? 'safe-yes' : 'safe-no');

    document.getElementById('var-vbat').textContent = d.vbat + ' mV';

    const ibatEl = document.getElementById('var-ibat');
    ibatEl.textContent = (d.ibat >= 0 ? '+' : '') + d.ibat + ' mA';
    ibatEl.className = 'var-value ' + (d.ibat >= 0 ? 'charging' : 'discharging');

    document.getElementById('var-soc').textContent = d.soc.toFixed(2) + ' %';
    document.getElementById('var-vsol').textContent = d.vsol + ' mV';
    document.getElementById('var-psol').textContent = d.psol.toFixed(1) + ' W';
    document.getElementById('var-duty').textContent = d.duty.toFixed(1) + ' %';
    document.getElementById('var-temp').textContent = (d.temp / 10).toFixed(1) + ' C';

    const heaterEl = document.getElementById('var-heater');
    heaterEl.textContent = d.heater ? 'ON' : 'OFF';
    heaterEl.className = 'var-value ' + (d.heater ? 'heater-on' : 'heater-off');

    document.getElementById('var-loads').textContent = d.loads + ' / 5';

    const efuseEl = document.getElementById('var-efuse');
    efuseEl.textContent = d.efuse ? 'CLOSED' : 'OPEN';
    efuseEl.className = 'var-value ' + (d.efuse ? 'charging' : 'discharging');
}

// ── Clock ───────────────────────────────────────────────────────────────────

function updateClock(simTime) {
    const minutes = Math.floor(simTime / 60);
    const seconds = simTime % 60;
    document.getElementById('clock-value').textContent =
        String(minutes).padStart(2, '0') + ':' +
        seconds.toFixed(1).padStart(4, '0');
}

// ── Playback Controller ─────────────────────────────────────────────────────

function animationLoop(timestamp) {
    if (!isPlaying || simData.length === 0) return;

    if (lastTimestamp === null) lastTimestamp = timestamp;
    const dtReal = (timestamp - lastTimestamp) / 1000; // seconds
    lastTimestamp = timestamp;

    // Advance simulation time by dtReal * playSpeed
    timeAccumulator += dtReal * playSpeed;

    // Find the data index closest to the accumulated time
    const targetTime = simData[currentIndex].t + timeAccumulator;

    // Advance currentIndex
    while (currentIndex < simData.length - 1 && simData[currentIndex + 1].t <= targetTime) {
        currentIndex++;
    }

    timeAccumulator = targetTime - simData[currentIndex].t;

    // Update everything
    updateFrame(currentIndex);

    // Continue or stop
    if (currentIndex >= simData.length - 1) {
        stopPlayback();
    } else {
        animFrameId = requestAnimationFrame(animationLoop);
    }
}

function updateFrame(index) {
    if (index < 0 || index >= simData.length) return;
    currentIndex = index;

    const d = simData[index];
    updateLiveVars(d);
    updateClock(d.t);
    drawOrbit(d.t);
    updateCharts(index);

    // Update slider
    const slider = document.getElementById('time-slider');
    slider.max = simData.length - 1;
    slider.value = index;

    document.getElementById('slider-time-left').textContent = formatTime(d.t);
    document.getElementById('slider-time-right').textContent =
        formatTime(simData[simData.length - 1].t);
}

function formatTime(seconds) {
    const m = Math.floor(seconds / 60);
    const s = Math.floor(seconds % 60);
    return String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}

// ── Controls ────────────────────────────────────────────────────────────────

function loadScenario() {
    const scenario = document.getElementById('scenario-select').value;
    const statusEl = document.getElementById('status-text');
    statusEl.textContent = 'Loading scenario ' + scenario + '...';
    statusEl.className = 'loading';

    stopPlayback();

    fetch('/api/simulate', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scenario: parseInt(scenario) })
    })
    .then(r => r.json())
    .then(result => {
        if (result.error) {
            statusEl.textContent = 'Error: ' + result.error;
            statusEl.className = 'error';
            return;
        }

        simData = result.data;
        currentIndex = 0;
        timeAccumulator = 0;

        statusEl.textContent = `Scenario ${scenario} loaded — ${simData.length} data points, ` +
            `${formatTime(simData[simData.length - 1].t)} total. ${result.stderr}`;
        statusEl.className = 'ready';

        updateFrame(0);
    })
    .catch(err => {
        statusEl.textContent = 'Network error: ' + err.message;
        statusEl.className = 'error';
    });
}

function togglePlay() {
    if (simData.length === 0) return;

    if (isPlaying) {
        stopPlayback();
    } else {
        isPlaying = true;
        lastTimestamp = null;
        timeAccumulator = 0;
        document.getElementById('btn-play').textContent = '⏸ Pause';
        document.getElementById('btn-play').classList.add('playing');

        // If at end, reset to beginning
        if (currentIndex >= simData.length - 1) {
            currentIndex = 0;
        }

        animFrameId = requestAnimationFrame(animationLoop);
    }
}

function stopPlayback() {
    isPlaying = false;
    lastTimestamp = null;
    if (animFrameId) {
        cancelAnimationFrame(animFrameId);
        animFrameId = null;
    }
    document.getElementById('btn-play').textContent = '▶ Play';
    document.getElementById('btn-play').classList.remove('playing');
}

function resetPlayback() {
    stopPlayback();
    currentIndex = 0;
    timeAccumulator = 0;
    if (simData.length > 0) {
        updateFrame(0);
    }
}

function setSpeed(val) {
    playSpeed = parseInt(val);
}

function onSliderInput(val) {
    if (simData.length === 0) return;
    stopPlayback();
    currentIndex = parseInt(val);
    timeAccumulator = 0;
    updateFrame(currentIndex);
}

// ── Init ────────────────────────────────────────────────────────────────────

initCharts();
drawOrbit(0);

// Auto-load scenario 1
window.addEventListener('load', () => {
    setTimeout(loadScenario, 500);
});
