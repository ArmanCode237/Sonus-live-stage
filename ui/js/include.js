// ============================================================================
// ui/js/include.js — Cargador de fragmentos HTML (partials)
//
// Uso en una página:   <div data-include="/partials/topbar.html"></div>
// Cada placeholder se reemplaza por el contenido del archivo indicado.
// loadIncludes() devuelve una Promise que se resuelve cuando TODO está en el DOM
// (app.js espera a esto antes de inicializar perillas, teclado, etc.).
// ============================================================================

async function loadIncludes(root = document) {
  const slots = [...root.querySelectorAll('[data-include]')];
  await Promise.all(slots.map(async slot => {
    const url = slot.dataset.include;
    try {
      const res = await fetch(url, { cache: 'no-cache' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const tpl = document.createElement('template');
      tpl.innerHTML = await res.text();
      slot.replaceWith(tpl.content);
    } catch (err) {
      console.error('[include] No se pudo cargar', url, err);
      slot.innerHTML = `<div style="color:#f87171;padding:8px;">Error cargando ${url}</div>`;
    }
  }));
  // Un partial puede contener a su vez otros includes
  if (root.querySelector('[data-include]')) await loadIncludes(root);
}
