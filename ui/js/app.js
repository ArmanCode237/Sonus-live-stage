// ============================================================================
// ui/js/app.js — Arranque de cada página (se carga el ÚLTIMO)
//
//  1. carga los partials (<div data-include>)
//  2. construye los componentes dinámicos que existan en la página
//  3. inicializa perillas y teclado
//  4. marca la pestaña activa del banner y arranca el polling
//
// Cada página declara cuál es con <body data-page="main|synth|organ">.
// Los builders hacen return si su contenedor no existe, así que se llaman todos.
// ============================================================================

document.addEventListener('DOMContentLoaded', async () => {
  await loadIncludes();

  // Componentes dinámicos (no-op si el contenedor no está en esta página)
  buildParametricEqModal();
  buildSynthPresetButtons();
  buildSynth3LayersPage();
  buildSynthGlobalControls();
  buildOrganDrawbarsPage();
  buildOrganOptions();
  if ($('synthUserPresetSelect')) refreshSynthUserPresets();
  if ($('organUserPresetSelect')) refreshOrganUserPresets();
  initProgramButtons();
  build88KeyPiano();

  // Perillas: DESPUÉS de construir todo lo que contiene .knob-cell
  initAllKnobs();

  // Estado inicial de la UI (solo visual; el servidor manda en el polling)
  const page = document.body.dataset.page;
  setActive(`pageBtn_${page}`, true);
  const pb = $(`pageBtn_${page}`);
  if (pb) pb.classList.add('active-toggle');
  const kb = $('keyboardSection');
  if (kb) kb.classList.toggle('hidden-kb', !state.keyboardVisible);
  const kbBtn = $('btnToggleKeyboard');
  if (kbBtn) kbBtn.classList.toggle('active-toggle', state.keyboardVisible);
  selectReverbType(state.reverbType, true);
  selectModTab(state.activeModTab);
  updateModLeds();
  if (typeof scanLibrary === 'function' && $('libDirInput')) scanLibrary();

  // Cerrar un modal al hacer clic en el fondo oscuro o con Esc
  document.addEventListener('click', e => {
    if (e.target.classList && e.target.classList.contains('modal-backdrop')) e.target.classList.remove('open');
  });
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape') document.querySelectorAll('.modal-backdrop.open').forEach(m => m.classList.remove('open'));
  });

  startWorkstationPolling();
});
