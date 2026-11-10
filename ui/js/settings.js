// ui/js/settings.js — Lógica de la ventana Audio & MIDI (settings.html)

function checkJson(r) {
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

function postJson(url, body) {
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  }).then(checkJson);
}

function showHwError(err) {
  console.error('[settings]', err);
  const box = document.getElementById('audioStatusBox');
  if (box) box.innerText = 'AVISO: Error de comunicación con el servidor: ' + err.message;
}

let hwData = null;

function loadHardware() {
  fetch('/api/hardware')
    .then(checkJson)
    .then(hw => {
      hwData = hw;
      document.getElementById('bufSelect').value = hw.bufferSize || 128;

      const banner = document.getElementById('missingAudioBanner');
      if (hw.savedAudioMissing && hw.savedAudioDeviceName) {
        banner.style.display = 'block';
        banner.innerText = `AVISO: El dispositivo guardado [${hw.savedAudioApi}] "${hw.savedAudioDeviceName}" no está conectado. Usando salida de respaldo.`;
      } else {
        banner.style.display = 'none';
      }

      const apiSel = document.getElementById('apiSelect');
      apiSel.innerHTML = '';
      (hw.availableApis || ['WASAPI', 'ASIO']).forEach(api => {
        const opt = document.createElement('option');
        opt.value = api;
        opt.text = api;
        if (api === hw.activeApi) opt.selected = true;
        apiSel.appendChild(opt);
      });

      onApiFilterChange(hw.activeAudioUid, hw.sampleRate);
      renderMidi(hw.midiPorts);
      document.getElementById('audioStatusBox').innerText =
        `ACTIVO: [${hw.activeApi}] ${hw.activeAudioDeviceName} @ ${hw.sampleRate} Hz (${hw.bufferSize} samples — ${(hw.latencyMs || 0).toFixed(2)} ms)`;
    })
    .catch(showHwError);
}

function onApiFilterChange(preferredUid = null, preferredSr = null) {
  if (!hwData) return;
  const chosenApi = document.getElementById('apiSelect').value;
  const devSel = document.getElementById('deviceSelect');
  devSel.innerHTML = '';

  hwData.audioDevices.filter(d => d.api === chosenApi).forEach(d => {
    const opt = document.createElement('option');
    opt.value = d.uid;
    opt.text = `${d.name} (${d.channels} ch)`;
    if (preferredUid !== null && d.uid === preferredUid) opt.selected = true;
    devSel.appendChild(opt);
  });

  onDeviceSelectionChange(preferredSr);
}

function onDeviceSelectionChange(preferredSr = null) {
  if (!hwData) return;
  const uid = parseInt(document.getElementById('deviceSelect').value);
  const dev = hwData.audioDevices.find(d => d.uid === uid);
  const srSel = document.getElementById('srSelect');
  srSel.innerHTML = '';

  const rates = (dev && dev.sampleRates && dev.sampleRates.length) ? dev.sampleRates : [44100, 48000, 88200, 96000];
  const targetSr = preferredSr || (dev ? dev.preferredSr : 48000);

  rates.forEach(r => {
    const opt = document.createElement('option');
    opt.value = r;
    opt.text = `${r} Hz`;
    if (r === targetSr) opt.selected = true;
    srSel.appendChild(opt);
  });
  updateLatencyPreview();
}

function updateLatencyPreview() {
  const sr = parseInt(document.getElementById('srSelect').value) || 48000;
  const buf = parseInt(document.getElementById('bufSelect').value) || 128;
  document.getElementById('latencyPreview').innerText = `${((buf / sr) * 1000).toFixed(2)} ms (${buf} smp @ ${sr} Hz)`;
}

function applyAudioSettings() {
  const uid = parseInt(document.getElementById('deviceSelect').value);
  const sr = parseInt(document.getElementById('srSelect').value);
  const buf = parseInt(document.getElementById('bufSelect').value);

  document.getElementById('audioStatusBox').innerText = "Aplicando configuración...";
  postJson('/api/hardware/audio', { uid, sampleRate: sr, bufferSize: buf })
    .then(hw => {
      hwData = hw;
      document.getElementById('audioStatusBox').innerText = hw.message;
      onApiFilterChange(hw.activeAudioUid, hw.sampleRate);
    })
    .catch(showHwError);
}

function rescanAudio() {
  fetch('/api/hardware/rescan', { method: 'POST' }).then(() => loadHardware()).catch(showHwError);
}

function testTone() { fetch('/api/testtone', { method: 'POST' }).catch(showHwError); }

function renderMidi(ports) {
  const box = document.getElementById('midiList');
  box.innerHTML = '';
  if (!ports || !ports.length) {
    box.innerHTML = '<div style="color:#888;">No se detectaron entradas MIDI USB.</div>';
    return;
  }
  ports.forEach((p, idx) => {
    const div = document.createElement('div');
    div.className = 'midi-item' + (p.connected ? '' : ' disconnected');

    const leftWrap = document.createElement('div');
    leftWrap.style.cssText = 'display:flex; align-items:center; gap:8px; flex:1;';

    const chk = document.createElement('input');
    chk.type = 'checkbox';
    chk.id = `m_${idx}`;
    chk.checked = !!p.enabled;
    chk.onchange = () => toggleMidi(p.name, p.index, chk.checked);

    const lbl = document.createElement('label');
    lbl.htmlFor = `m_${idx}`;
    lbl.style.cssText = 'cursor:pointer; flex:1;';
    lbl.innerText = p.connected ? `Puerto [${p.index}]: ${p.name}` : `${p.name} (Guardado)`;

    leftWrap.append(chk, lbl);

    const rightWrap = document.createElement('div');
    rightWrap.style.cssText = 'display:flex; align-items:center; gap:6px;';

    const badge = document.createElement('span');
    badge.className = 'conn-badge ' + (p.connected ? 'ok' : 'missing');
    badge.innerText = p.connected ? 'CONECTADO' : 'DESCONECTADO';
    rightWrap.appendChild(badge);

    if (!p.connected) {
      const fBtn = document.createElement('button');
      fBtn.className = 'forget-btn';
      fBtn.innerText = 'Olvidar';
      fBtn.onclick = () => forgetMidi(p.name);
      rightWrap.appendChild(fBtn);
    }

    div.append(leftWrap, rightWrap);
    box.appendChild(div);
  });
}

function toggleMidi(name, index, enabled) {
  postJson('/api/hardware/midi', { name, index, enabled })
    .then(hw => renderMidi(hw.midiPorts))
    .catch(showHwError);
}

function forgetMidi(name) {
  postJson('/api/hardware/midi', { forgetName: name })
    .then(hw => renderMidi(hw.midiPorts))
    .catch(showHwError);
}

function refreshMidi() {
  postJson('/api/hardware/midi', { refresh: true })
    .then(hw => renderMidi(hw.midiPorts))
    .catch(showHwError);
}

loadHardware();
