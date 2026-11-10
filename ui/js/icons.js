// Iconografía vectorial local, sin CDN ni librerías. Los atributos data-ui-icon
// se convierten en SVG accesibles (decorativos) una vez insertados los partials.
const STAGE_ICON_PATHS = {
  expand: '<path d="M8 3H5a2 2 0 0 0-2 2v3M16 3h3a2 2 0 0 1 2 2v3M3 16v3a2 2 0 0 0 2 2h3M21 16v3a2 2 0 0 1-2 2h-3"/>',
  sliders: '<path d="M4 21v-7m0-4V3m8 18v-9m0-4V3m8 18v-5m0-4V3"/><path d="M1 14h6M9 8h6m2 8h6"/>',
  settings: '<path d="M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z"/><path d="M19.4 15.1l1.4 1.1-1.7 2.9-1.7-.6-1.8 1-.3 1.8H9.7l-.3-1.8-1.8-1-1.7.6-1.7-2.9 1.4-1.1v-2.2l-1.4-1.1 1.7-2.9 1.7.6 1.8-1 .3-1.8h5.6l.3 1.8 1.8 1 1.7-.6 1.7 2.9-1.4 1.1z" transform="translate(0,-1) scale(1,.9)"/>',
  piano: '<path d="M3 5h18v14H3zM8 5v10m4-10v10m4-10v10M7 5v8h3V5m4 0v8h3V5"/>',
  synth: '<path d="M4 17V7m-2 5h4m6 9V3m-2 6h4m6 9V6m-2 5h4"/>',
  organ: '<path d="M3 8h18v12H3zM4 4h16v4H4zM7 10v8m4-8v8m4-8v8m4-8v8M3 15h18"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v10H3z"/>',
  search: '<circle cx="10.7" cy="10.7" r="6.6"/><path d="m16 16 5 5"/>',
  'arrow-left': '<path d="M19 12H5m7-7-7 7 7 7"/>',
  headphones: '<path d="M3 14v-3a9 9 0 0 1 18 0v3M3 14h4v7H5a2 2 0 0 1-2-2zm14 0h4v5a2 2 0 0 1-2 2h-2z"/>',
  grid: '<path d="M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h6v6h-6z"/>',
  circle: '<circle cx="12" cy="12" r="7"/>',
  save: '<path d="M5 3h13l3 3v15H3V3zM7 3v6h10V3M7 21v-9h10v9"/>',
  keyboard: '<path d="M3 6h18v12H3zM7 6v8m5-8v8m5-8v8M3 14h18"/>'
};
function renderStageIcons(){
  document.querySelectorAll('[data-ui-icon]').forEach(el=>{
    const name=el.getAttribute('data-ui-icon');
    const path=STAGE_ICON_PATHS[name];
    if(!path || el.querySelector('svg'))return;
    el.innerHTML=`<svg class="ui-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false">${path}</svg>`;
  });
}
