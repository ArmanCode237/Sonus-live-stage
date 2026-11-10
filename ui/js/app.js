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

  // Independent builders: an exception in one panel must not prevent the
  // other panels, physical controls, or the virtual keyboard from loading.
  const safely = (name, fn) => {
    try { fn(); } catch (error) { console.error(`[boot] ${name}:`, error); }
  };
  safely('EQ', buildParametricEqModal);
  safely('synth factory', buildSynthPresetButtons);
  if ($('synthFactoryEngineStatus') && typeof verifySynthFactoryStatus === 'function') verifySynthFactoryStatus();
  safely('synth layers', buildSynth3LayersPage);
  safely('synth global', buildSynthGlobalControls);
  safely('organ drawbars', buildOrganDrawbarsPage);
  safely('organ mini drawbars', buildMainOrganDrawbars);
  safely('organ options', buildOrganOptions);
  safely('aux sends', buildFxSendMatrix);
  if ($('synthUserPresetSelect')) refreshSynthUserPresets();
  if ($('organUserPresetSelect')) refreshOrganUserPresets();
  safely('programs', initProgramButtons);
  safely('88-key keyboard', build88KeyPiano);

  // Perillas: DESPUÉS de construir todo lo que contiene .knob-cell
  safely('physical knobs', initAllKnobs);
  if (typeof renderStageIcons === 'function') renderStageIcons();
  if (typeof updateMainDashboard === 'function') updateMainDashboard();

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
  window.addEventListener('pageshow', event => {
    if (!event.persisted) return;
    // B/F cache restores DOM; do not destroy controls or register duplicate events.
    initAllKnobs();
    if (!$('vkey_60')) build88KeyPiano();
  });
});
