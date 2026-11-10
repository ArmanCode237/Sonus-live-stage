// ============================================================================
// ui/js/keyboard.js — Teclado visual de 88 teclas (A0–C8) jugable con ratón o dedo
//   · el resaltado de teclas pulsadas viene del polling (/api/state → activeKeys)
//   · arrastrar sobre el teclado hace glissando
// ============================================================================

const KB_WHITE_KEY_PX = 24;
let kbReleaseBound = false;

function sendNote(key, vel, on) {
  return fetch('/api/note', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ key, vel, on })
  }).catch(() => {});
}

function virtualNoteOn(midi) {
  if (state.mousePressedNote === midi) return;
  if (state.mousePressedNote >= 0) virtualNoteOff();     // glissando: suelta la anterior
  state.mousePressedNote = midi;
  const el = $(`vkey_${midi}`);
  if (el) el.classList.add('held');
  sendNote(midi, 105, true);
}

function virtualNoteOff() {
  const prev = state.mousePressedNote;
  if (prev < 0) return;
  state.mousePressedNote = -1;
  const el = $(`vkey_${prev}`);
  if (el) el.classList.remove('held');
  sendNote(prev, 0, false);
}

function build88KeyPiano() {
  const roll = $('pianoRoll');
  if (!roll) return;
  roll.innerHTML = '';
  const isBlack = [false, true, false, true, false, false, true, false, true, false, true, false];
  let whiteCount = 0;

  for (let midi = 21; midi <= 108; midi++) {
    const noteInOct = midi % 12, black = isBlack[noteInOct];
    const k = document.createElement('div');
    k.id = `vkey_${midi}`;
    if (!black) {
      k.className = 'white-key';
      if (noteInOct === 0) k.innerText = 'C' + (Math.floor(midi / 12) - 1);
      else if (midi === 21) k.innerText = 'A0';
      whiteCount++;
    } else {
      k.className = 'black-key';
      k.style.left = `${(whiteCount * KB_WHITE_KEY_PX) - 7.5}px`;
    }
    roll.appendChild(k);

    k.style.touchAction = 'none';
    k.addEventListener('pointerdown', e => {
      e.preventDefault();
      if (k.hasPointerCapture && k.hasPointerCapture(e.pointerId)) k.releasePointerCapture(e.pointerId); // permite glissando táctil
      e.stopPropagation();
      virtualNoteOn(midi);
    });
    k.addEventListener('pointerenter', e => {
      if (e.buttons & 1) virtualNoteOn(midi);
    });
  }

  // Un único listener global (build88KeyPiano puede llamarse más de una vez)
  if (!kbReleaseBound) {
    kbReleaseBound = true;
    window.addEventListener('pointerup', virtualNoteOff);
    window.addEventListener('pointercancel', virtualNoteOff);
    window.addEventListener('blur', virtualNoteOff);
  }
}
