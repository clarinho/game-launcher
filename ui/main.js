const { app, BrowserWindow, Menu, ipcMain } = require('electron');
const path = require('path');
const { spawn } = require('child_process');
const fs = require("fs");

function resolveCampfireBinary() {
  if (app.isPackaged) {
    return path.join(process.resourcesPath, "campfire", "campfire.exe");
  }

  const candidates = [
    path.join(__dirname, "..", "build", "campfire_dev.exe"),
    path.join(__dirname, "..", "build", "campfire.exe"),
  ];
  for (const candidate of candidates) {
    try {
      require("fs").accessSync(candidate);
      return candidate;
    } catch (_) {
      // Try next candidate.
    }
  }
  return candidates[0];
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 800,
    backgroundColor: '#0a0a0a',
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
    },
  });
  win.setMenuBarVisibility(false);
  win.loadFile(path.join(__dirname, "index.html"));
}

ipcMain.handle('run-campfire', async (event, args, stdinText = "") => {
  return new Promise((resolve) => {
    const exePath = resolveCampfireBinary();
    const child = spawn(exePath, args, { shell: false });

    let stdout = '';
    let stderr = '';

    child.stdout.on('data', (data) => stdout += data.toString());
    child.stderr.on('data', (data) => stderr += data.toString());

    child.on('close', (code) => {
      resolve({ code, stdout, stderr });
    });

    child.on("error", (err) => {
      resolve({
        code: 1,
        stdout,
        stderr: `${stderr}\nFailed to start Campfire backend (${exePath}): ${err.message}`,
      });
    });

    if (stdinText) {
      child.stdin.write(stdinText);
    }
    child.stdin.end();
  });
});

ipcMain.handle("find-artwork", async (event, exePath) => {
  try {
    if (!exePath) {
      return "";
    }

    const dir = path.dirname(exePath);
    const base = path.parse(exePath).name;
    const candidates = [
      "cover.jpg",
      "cover.png",
      "poster.jpg",
      "poster.png",
      "banner.jpg",
      "banner.png",
      `${base}.jpg`,
      `${base}.png`,
    ];

    for (const file of candidates) {
      const full = path.join(dir, file);
      if (fs.existsSync(full)) {
        return full;
      }
    }

    return "";
  } catch (_) {
    return "";
  }
});

app.whenReady().then(() => {
  Menu.setApplicationMenu(null);
  createWindow();
});
