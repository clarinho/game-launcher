function parseListOutput(output) {
    const games = [];
    const lines = output.split('\n');
    for (const line of lines) {
        const match = line.match(/^\s*(\d+)\s+\|\s*(.*?)\s+\|\s*(\d+)\s+\|\s*(.*?)\s*$/);
        if (!match) {
            continue;
        }
        games.push({
            id: match[1],
            name: match[2],
            plays: Number(match[3]) || 0,
            time: match[4]
        });
    }
    return games;
}

let modalMode = "add";
let viewMode = "panel";
const forceFallbackById = loadForceFallbackMap();
let scanInProgress = false;

function loadForceFallbackMap() {
    try {
        const raw = window.localStorage.getItem("campfire_force_fallback_by_id");
        if (!raw) {
            return {};
        }
        const parsed = JSON.parse(raw);
        return parsed && typeof parsed === "object" ? parsed : {};
    } catch (_) {
        return {};
    }
}

function persistForceFallbackMap() {
    try {
        window.localStorage.setItem("campfire_force_fallback_by_id", JSON.stringify(forceFallbackById));
    } catch (_) {
        // Ignore storage failures.
    }
}

function formatDurationLabel(totalSeconds) {
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    if (hours > 0) {
        return `${hours}h ${minutes}m`;
    }
    return `${minutes}m`;
}

function formatHoursFromTimeText(text) {
    const hours = parseTimeToSeconds(text) / 3600;
    return `${hours.toFixed(1)}h`;
}

function parseTimeToSeconds(text) {
    let seconds = 0;
    const dayMatch = text.match(/(\d+(?:\.\d+)?)d/);
    const hourMatch = text.match(/(\d+(?:\.\d+)?)h/);
    const minuteMatch = text.match(/(\d+)m/);
    const secondMatch = text.match(/(\d+)s/);
    if (dayMatch) {
        seconds += Number(dayMatch[1]) * 86400;
    }
    if (hourMatch) {
        seconds += Number(hourMatch[1]) * 3600;
    }
    if (minuteMatch) {
        seconds += Number(minuteMatch[1]) * 60;
    }
    if (secondMatch) {
        seconds += Number(secondMatch[1]);
    }
    return seconds;
}

function renderPanelCard(game) {
    const displayHours = formatHoursFromTimeText(game.time);
    return `
        <div class="game-card">
            <div class="game-stripe"></div>
            <div class="game-info">
                <span class="game-title">${game.name}</span>
                <span class="game-meta">Time Played: ${displayHours}</span>
                <span class="game-meta">Times Launched: ${game.plays}</span>
                <div class="game-actions">
                    <button class="neon-button" onclick="launchGame('${game.id}')">Play</button>
                    <button class="neon-button edit-button" onclick="openEditModal('${game.id}')">Edit</button>
                </div>
            </div>
        </div>
    `;
}

function renderListRow(game) {
    const displayHours = formatHoursFromTimeText(game.time);
    return `
        <div class="game-list-row">
            <div class="row-main">
                <span class="game-title">${game.name}</span>
            </div>
            <div class="row-meta">
                <span class="game-meta">Time Played: ${displayHours}</span>
                <span class="game-meta">Times Launched: ${game.plays}</span>
            </div>
            <div class="row-actions">
                <button class="neon-button" onclick="launchGame('${game.id}')">Play</button>
                <button class="neon-button edit-button" onclick="openEditModal('${game.id}')">Edit</button>
            </div>
        </div>
    `;
}

function getActivityEl() {
    return document.getElementById("status-output");
}

function setActivity(text) {
    const el = getActivityEl();
    el.textContent = text;
}

function appendActivity(line) {
    const el = getActivityEl();
    el.textContent += `${line}\n`;
    el.scrollTop = el.scrollHeight;
}

async function refreshLibrary() {
    const res = await window.api.runCampfire(['list']);
    const grid = document.getElementById('game-grid');
    const countEl = document.getElementById('stat-count');
    const timeEl = document.getElementById('stat-time');
    const logEl = getActivityEl();

    if (res.code !== 0) {
        grid.innerHTML = '';
        countEl.innerText = '0';
        timeEl.innerText = '0m';
        logEl.textContent = res.stderr || 'Failed to load library';
        return;
    }

    const games = parseListOutput(res.stdout);
    grid.classList.toggle("list-mode", viewMode === "list");
    grid.innerHTML = '';

    if (games.length === 0) {
        grid.innerHTML = `
            <div class="game-card">
                <div class="game-stripe"></div>
                <div class="game-info">
                    <span class="game-title">No games yet</span>
                    <span class="game-meta">Use Add Game to create your first entry.</span>
                </div>
            </div>
        `;
    }

    for (const game of games) {
        if (viewMode === "list") {
            grid.insertAdjacentHTML("beforeend", renderListRow(game));
        } else {
            grid.insertAdjacentHTML("beforeend", renderPanelCard(game));
        }
    }

    countEl.innerText = String(games.length);
    const totalSeconds = games.reduce((sum, game) => sum + parseTimeToSeconds(game.time), 0);
    timeEl.innerText = formatDurationLabel(totalSeconds);
    logEl.textContent = `Loaded ${games.length} games`;
}

function setViewMode(mode) {
    viewMode = mode === "list" ? "list" : "panel";
    document.getElementById("view-panel").classList.toggle("active", viewMode === "panel");
    document.getElementById("view-list").classList.toggle("active", viewMode === "list");
    refreshLibrary();
}

async function launchGame(id) {
    const useForceFallback = Boolean(forceFallbackById[id]);
    const args = ['launch', '--id', id];
    if (useForceFallback) {
        args.push('--force-fallback');
    }
    const res = await window.api.runCampfire(args);
    setActivity(res.stdout || res.stderr);
    refreshLibrary();
}

async function saveGame() {
    const name = document.getElementById('add-name').value;
    const exe = document.getElementById('add-path').value;
    const args = document.getElementById('add-args').value;
    const editId = document.getElementById('edit-id').value;

    let cmdArgs = [];
    if (modalMode === "edit" && editId) {
        cmdArgs = ['edit', '--id', editId, '--name', name, '--exe', exe];
        if (args) cmdArgs.push('--args', args);
        forceFallbackById[editId] = document.getElementById("edit-force-fallback").checked;
        persistForceFallbackMap();
    } else {
        cmdArgs = ['add', '--name', name, '--exe', exe];
        if (args) cmdArgs.push('--args', args);
    }

    const res = await window.api.runCampfire(cmdArgs);
    setActivity(res.stdout || res.stderr);
    closeModal();
    refreshLibrary();
}

async function runScan() {
    if (scanInProgress) {
        return;
    }
    scanInProgress = true;
    const outputEl = getActivityEl();
    const frames = ['.', '..', '...', '..'];
    let frameIndex = 0;

    const extractScanEntries = (text) => {
        const entries = [];
        const lines = text.split('\n');
        for (const rawLine of lines) {
            const line = rawLine.trim();
            if (!line) {
                continue;
            }
            if (line.includes('+') && line.includes('-')) {
                continue;
            }
            if (/^name\s*\|\s*executable$/i.test(line)) {
                continue;
            }

            const pipeIndex = rawLine.indexOf('|');
            if (pipeIndex < 0) {
                continue;
            }
            const left = rawLine.slice(0, pipeIndex).trim();
            const right = rawLine.slice(pipeIndex + 1).trim();
            if (!left || !right) {
                continue;
            }
            entries.push({ name: left, path: right });
        }
        return entries;
    };

    const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

    outputEl.textContent = "Scanning...";
    const ticker = setInterval(() => {
        outputEl.textContent = `Scanning${frames[frameIndex]}`;
        frameIndex = (frameIndex + 1) % frames.length;
    }, 90);

    const res = await window.api.runCampfire(['scan'], 'n\n');
    clearInterval(ticker);

    if (res.code !== 0) {
        outputEl.textContent = (res.stderr || res.stdout || "Scan failed").trim();
        scanInProgress = false;
        return;
    }

    const entries = extractScanEntries(res.stdout);
    outputEl.textContent = "";

    if (entries.length === 0) {
        outputEl.textContent = "No games detected.";
        scanInProgress = false;
        return;
    }

    const nameWidth = entries.reduce((max, entry) => Math.max(max, entry.name.length), 0);
    for (const entry of entries) {
        appendActivity(`${entry.name.padEnd(nameWidth)} | ${entry.path}`);
        await sleep(28);
    }
    scanInProgress = false;
}

async function runDoctor() {
    const outputEl = getActivityEl();
    const frames = ['.', '..', '...', '..'];
    let frameIndex = 0;

    const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

    outputEl.textContent = "Running...";
    const ticker = setInterval(() => {
        outputEl.textContent = `Running${frames[frameIndex]}`;
        frameIndex = (frameIndex + 1) % frames.length;
    }, 90);

    const startMs = Date.now();
    const res = await window.api.runCampfire(['doctor']);
    const elapsedMs = Date.now() - startMs;
    if (elapsedMs < 2000) {
        await sleep(2000 - elapsedMs);
    }
    clearInterval(ticker);

    const combined = `${res.stdout}\n${res.stderr}`.trim();
    if (!combined) {
        setActivity("Doctor finished with no output.");
        return;
    }

    setActivity("");
    const lines = combined.split('\n').map((line) => line.trim()).filter((line) => line.length > 0);
    for (const line of lines) {
        appendActivity(line);
        await sleep(22);
    }
}

function setPageHeader(pageId) {
    const title = document.getElementById('page-title');
    const subtitle = document.getElementById('page-subtitle');
    title.textContent = 'Your Library';
    subtitle.textContent = 'Launch and manage local games';
}

function showPage(pageId, navEl) {
    document.querySelectorAll('.page').forEach(p => p.style.display = 'none');
    document.getElementById(pageId).style.display = 'block';
    const libraryNav = document.querySelector('.nav-item');
    if (libraryNav) {
        if (pageId === 'library') {
            libraryNav.classList.add('active');
        } else {
            libraryNav.classList.remove('active');
        }
    }
    if (navEl && pageId === 'library') {
        navEl.classList.add('active');
    }

    setPageHeader(pageId);
}

function openScanFromMenu() {
    showPage('library');
    runScan();
}

function openDoctorFromMenu() {
    showPage('library');
    runDoctor();
}

function openHelpFromMenu() {
    const helpText = [
        "Campfire",
        "",
        "Tools > Scan Library: scans common game directories",
        "Tools > Run Doctor: checks library/runtime health",
        "+ > Add Game: manually add a game entry",
        "",
        "Launch tip: open a game's Edit panel and enable force fallback for wrapper-heavy installs"
    ].join("\n");
    setActivity(helpText);
    showPage('library');
}

async function openEditModal(id) {
    const res = await window.api.runCampfire(['info', '--id', id]);
    if (res.code !== 0) {
        setActivity(res.stderr || 'Failed to load game details');
        return;
    }

    const nameMatch = res.stdout.match(/^name:\s*(.*)$/m);
    const pathMatch = res.stdout.match(/^path:\s*(.*)$/m);
    modalMode = "edit";
    document.getElementById('modal-title').textContent = 'Edit Game';
    document.getElementById('modal-save').textContent = 'UPDATE';
    document.getElementById('edit-id').value = id;
    document.getElementById('add-name').value = nameMatch ? nameMatch[1] : '';
    document.getElementById('add-path').value = pathMatch ? pathMatch[1] : '';
    document.getElementById('add-args').value = '';
    document.getElementById('edit-force-wrap').style.display = 'flex';
    document.getElementById('edit-force-fallback').checked = Boolean(forceFallbackById[id]);
    document.getElementById('modal').style.display = 'flex';
}

function openModal() {
    modalMode = "add";
    document.getElementById('modal-title').textContent = 'Add New Game';
    document.getElementById('modal-save').textContent = 'SAVE';
    document.getElementById('edit-id').value = '';
    document.getElementById('add-name').value = '';
    document.getElementById('add-path').value = '';
    document.getElementById('add-args').value = '';
    document.getElementById('edit-force-wrap').style.display = 'none';
    document.getElementById('edit-force-fallback').checked = false;
    document.getElementById('modal').style.display = 'flex';
}

function closeModal() { document.getElementById('modal').style.display = 'none'; }
function clearActivity() { setActivity(""); }

function setupActivityResizeHandle() {
    const output = document.getElementById("status-output");
    const handle = document.getElementById("activity-resize-handle");
    if (!output || !handle) {
        return;
    }

    let dragging = false;
    let startY = 0;
    let startHeight = 0;
    const minHeight = 84;
    const maxHeight = Math.max(260, window.innerHeight - 220);

    handle.addEventListener("mousedown", (event) => {
        dragging = true;
        startY = event.clientY;
        startHeight = output.getBoundingClientRect().height;
        document.body.style.userSelect = "none";
        event.preventDefault();
    });

    window.addEventListener("mousemove", (event) => {
        if (!dragging) {
            return;
        }
        const delta = event.clientY - startY;
        const nextHeight = Math.max(minHeight, Math.min(maxHeight, startHeight - delta));
        output.style.height = `${nextHeight}px`;
    });

    window.addEventListener("mouseup", () => {
        if (!dragging) {
            return;
        }
        dragging = false;
        document.body.style.userSelect = "";
    });
}

window.showPage = showPage;
window.openModal = openModal;
window.closeModal = closeModal;
window.saveGame = saveGame;
window.openEditModal = openEditModal;
window.launchGame = launchGame;
window.runScan = runScan;
window.refreshLibrary = refreshLibrary;
window.openScanFromMenu = openScanFromMenu;
window.openDoctorFromMenu = openDoctorFromMenu;
window.openHelpFromMenu = openHelpFromMenu;
window.setViewMode = setViewMode;
window.clearActivity = clearActivity;

window.addEventListener('DOMContentLoaded', () => {
    setupActivityResizeHandle();
    refreshLibrary();
});
