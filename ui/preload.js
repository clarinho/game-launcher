const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('api', {
  runCampfire: (args, stdinText = "") => ipcRenderer.invoke('run-campfire', args, stdinText),
  findArtwork: (exePath) => ipcRenderer.invoke("find-artwork", exePath),
});
