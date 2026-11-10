// ============================================================================
// ui/js/library.js — Navegador de samples (modal), categorías, favoritos y carga de instrumentos
// ============================================================================
function scanLibrary() {
  const dirEl = $('libDirInput');
  const dir = dirEl ? dirEl.value : '';
  fetch(`/api/library?dir=${encodeURIComponent(dir)}`)
    .then(r => r.json())
    .then(data => {
      state.libraryItems = data.items || [];
      rebuildFilteredList();
      renderCategorySidebar();
      renderBrowserList();
    }).catch(err => console.error('[library]', err));
}
function onQuickCategoryChange(cat) {
  state.selectedCategory = cat;
  rebuildFilteredList();
  if (state.filteredList.length > 0) {
    loadInstrument(state.filteredList[0].path);
    setText('modelIndexNum', "1");
  }
}
function rebuildFilteredList() {
  const search = ($('searchSampleInput')?.value || '').trim().toLowerCase();
  const sortMode = $('sortSampleSelect')?.value || 'name_asc';
  const extFilter = $('extFilterSelect')?.value || 'ALL';

  let arr = state.libraryItems.filter(it => {
    if (state.selectedCategory === 'FAV' && !it.favorite) return false;
    if (state.selectedCategory !== 'ALL' && state.selectedCategory !== 'FAV' && it.group !== state.selectedCategory) return false;
    if (extFilter === '.npno' && it.ext !== '.npno') return false;
    if (extFilter === '.nsmp' && !it.ext.startsWith('.nsmp')) return false;
    if (extFilter === 'PROG' && !['.ns3f', '.ns2p', '.ns2f', '.nw2p'].includes(it.ext)) return false;
    if (search && !it.name.toLowerCase().includes(search) && !it.group.toLowerCase().includes(search)) return false;
    return true;
  });

  arr.sort((a, b) => {
    if (sortMode === 'name_asc') return a.name.localeCompare(b.name);
    if (sortMode === 'name_desc') return b.name.localeCompare(a.name);
    if (sortMode === 'size_desc') return b.sizeMB - a.sizeMB;
    if (sortMode === 'size_asc') return a.sizeMB - b.sizeMB;
    if (sortMode === 'ext_asc') return a.ext.localeCompare(b.ext) || a.name.localeCompare(b.name);
    return 0;
  });
  state.filteredList = arr;
}
function renderCategorySidebar() {
  const side = $('browserCategorySidebar');
  if (!side) return;
  side.innerHTML = '';
  const counts = { ALL: state.libraryItems.length, FAV: state.libraryItems.filter(x => x.favorite).length };
  SOUND_GROUPS.forEach(g => { counts[g] = state.libraryItems.filter(x => x.group === g).length; });

  const entries = [
    { id: 'ALL', label: '📁 Todos los Samples' },
    { id: 'FAV', label: '★ Favoritos' },
    { id: 'Acoustic Pianos', label: '🎹 Acoustic Pianos' },
    { id: 'Electric Pianos', label: '⚡ Electric Pianos' },
    { id: 'Accordion & Organ', label: '🪗 Accordion & Organ' },
    { id: 'Xylophone & Mallets', label: '🔔 Xylophone & Mallets' },
    { id: 'Strings, Brass & Pads', label: '🎻 Strings, Brass & Pads' },
    { id: 'Synth & Misc Samples', label: '🎛 Synth & Misc' },
    { id: 'Programs', label: '💾 Programs (.ns3f)' }
  ];
  entries.forEach(e => {
    const btn = document.createElement('button');
    btn.className = 'cat-nav-btn' + (state.selectedCategory === e.id ? ' active' : '');
    btn.innerHTML = `<span>${e.label}</span><span class="cat-count-pill">${counts[e.id] || 0}</span>`;
    btn.onclick = () => {
      state.selectedCategory = e.id;
      const qSel = $('quickCatSelect');
      if (qSel && [...qSel.options].some(o => o.value === e.id)) qSel.value = e.id;
      renderCategorySidebar();
      renderBrowserList();
    };
    side.appendChild(btn);
  });
}
function renderBrowserList() {
  rebuildFilteredList();
  const list = $('libraryList');
  if (!list) return;
  list.innerHTML = '';
  if (state.filteredList.length === 0) {
    list.innerHTML = '<div style="padding:18px; color:#94a3b8; text-align:center;">No hay samples que coincidan con el filtro actual.</div>';
    return;
  }
  state.filteredList.forEach((it, idx) => {
    const row = document.createElement('div');
    row.className = 'lib-item-row' + (it.path === state.currentSourcePath ? ' active' : '');

    const starBtn = document.createElement('button');
    starBtn.className = 'fav-star-btn' + (it.favorite ? ' fav' : '');
    starBtn.innerText = it.favorite ? '★' : '☆';
    starBtn.onclick = ev => {
      ev.stopPropagation();
      it.favorite = !it.favorite;
      saveSampleTag(it.name, it.group, it.favorite);
      renderCategorySidebar();
      renderBrowserList();
    };

    const nameSpan = document.createElement('div');
    nameSpan.style.cssText = 'overflow:hidden; text-overflow:ellipsis; white-space:nowrap;';
    nameSpan.innerText = it.name;

    const groupSel = document.createElement('select');
    groupSel.className = 'row-group-select';
    SOUND_GROUPS.forEach(g => {
      const opt = document.createElement('option');
      opt.value = g; opt.text = g;
      if (g === it.group) opt.selected = true;
      groupSel.appendChild(opt);
    });
    groupSel.onclick = ev => ev.stopPropagation();
    groupSel.onchange = ev => {
      it.group = ev.target.value;
      saveSampleTag(it.name, it.group, it.favorite);
      renderCategorySidebar();
    };

    const sizeSpan = document.createElement('div');
    sizeSpan.style.cssText = 'text-align:right; font-family:Consolas,monospace; color:#93c5fd;';
    sizeSpan.innerText = it.sizeMB >= 0.1 ? `${it.sizeMB.toFixed(1)} MB` : it.ext;

    row.append(starBtn, nameSpan, groupSel, sizeSpan);
    row.onclick = () => {
      setText('modelIndexNum', idx + 1);
      loadInstrument(it.path);
      closeModal('modalBrowser');
    };
    list.appendChild(row);
  });
}
function saveSampleTag(name, group, favorite) {
  fetch('/api/library/tag', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ name, group, favorite })
  }).catch(() => {});
}
function stepLibrary(dir) {
  rebuildFilteredList();
  if (!state.filteredList.length) return;
  let curIdx = state.filteredList.findIndex(x => x.path === state.currentSourcePath);
  if (curIdx < 0) curIdx = 0;
  curIdx = (curIdx + dir + state.filteredList.length) % state.filteredList.length;
  setText('modelIndexNum', curIdx + 1);
  loadInstrument(state.filteredList[curIdx].path);
}
function loadInstrument(path) {
  state.currentSourcePath = path;
  setText('lcdSampleName', "LOADING...");
  fetch('/api/load', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ path })
  }).then(r => r.json()).then(res => {
    state.currentInstId = res.instrumentId || state.currentInstId;
    state.currentSourcePath = res.sourcePath || path;
    setText('lcdSampleName', res.instrumentName || "No Sound Loaded");
    setText('zoneCountNum', res.zoneCount || 0);
    setText('ramTopText', (res.memoryMB || 0).toFixed(1) + " MB");
    if (res.params) syncFromServerParams(res.params);
  }).catch(() => {});
}
