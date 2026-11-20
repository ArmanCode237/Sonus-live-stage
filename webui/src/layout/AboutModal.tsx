// ACERCA DE: opened from the logo. App info, license, credits and the configurable links
// (config/links.json): APOYAR (support), SITIO WEB, CÓDIGO FUENTE. Links always open in the
// system's browser: inside the app window or a DAW plug-in (embedded WebView) the engine
// opens them; in a normal browser they open in a new tab.
import { useEffect, useState } from 'react';
import { api, type AboutInfo } from '../api/client';
import { notify } from '../store/notify';
import { BrandLogo } from '../ui/BrandLogo';
import { Modal } from '../ui/Modal';

type LinkKey = 'support' | 'website' | 'source';

/** True inside WebView2 (Windows app window / plug-in editor) or WKWebView (macOS). */
function embeddedWebView(): boolean {
  const w = window as unknown as { chrome?: { webview?: unknown }; webkit?: { messageHandlers?: unknown } };
  return Boolean(w.chrome?.webview || w.webkit?.messageHandlers);
}

export async function openExternalLink(link: LinkKey, url: string): Promise<void> {
  if (!url) { notify('Este enlace todavía no está configurado.', 'error'); return; }
  if (!embeddedWebView()) {
    window.open(url, '_blank', 'noopener,noreferrer');
    return;
  }
  try {
    await api.openAboutLink(link);
    notify('Abierto en tu navegador', 'ok');
  } catch (e) {
    notify((e as Error).message, 'error');
  }
}

const MODULES = [
  ['PIANO', '5 capas de muestras con zonas, octava y semitonos por capa'],
  ['SYNTH', '4 capas: analógico, wavetable, FM, DX7, MT-32 y LA32'],
  ['ORGAN', 'drawbars, percusión, vibrato / chorus y rotary'],
] as const;

export function AboutModal({ onClose }: { onClose: () => void }) {
  const [info, setInfo] = useState<AboutInfo | null>(null);
  useEffect(() => { api.about().then(setInfo).catch(() => setInfo(null)); }, []);
  const links = info?.links ?? { support: '', website: '', source: '' };
  return (
    <Modal title="ACERCA DE SONUS LIVE STAGE" onClose={onClose}>
      <div className="about">
        <header className="about__head">
          <BrandLogo size={84} />
          <div>
            <h2 className="about__name">Sonus <span>Live Stage</span></h2>
            <p className="about__version">
              Versión <b>{info?.version ?? '…'}</b> · {info?.host === 'plugin' ? 'plug-in VST3' : 'aplicación'}
            </p>
            <p className="about__tagline">Instrumento virtual de escenario</p>
          </div>
        </header>

        <ul className="about__modules">
          {MODULES.map(([name, text]) => <li key={name}><b>{name}</b><span>{text}</span></li>)}
        </ul>

        <section className="about__support">
          <p>Sonus Live Stage lo desarrolla un pequeño equipo. Si te resulta útil en tus ensayos o en el escenario,
            tu apoyo nos ayuda a seguir mejorándolo.</p>
          <button type="button" className="about__support-btn" disabled={!links.support}
            title={links.support ? 'Se abre en tu navegador' : 'Enlace pendiente de configurar (config/links.json)'}
            onClick={() => void openExternalLink('support', links.support)}>
            <span aria-hidden="true">♥</span> APOYAR A SONUS LIVE STAGE
          </button>
          {!links.support && <p className="about__hint">Enlace de apoyo pendiente de configurar.</p>}
        </section>

        {(links.website || links.source) && (
          <div className="about__links">
            {links.website && <button type="button" className="about__link" onClick={() => void openExternalLink('website', links.website)}>SITIO WEB ↗</button>}
            {links.source && <button type="button" className="about__link" onClick={() => void openExternalLink('source', links.source)}>CÓDIGO FUENTE ↗</button>}
          </div>
        )}

        <dl className="about__facts">
          <dt>Licencia</dt><dd>Software libre · GNU AGPL-3.0 o posterior</dd>
          <dt>Autoría</dt><dd>Armando García y colaboradores de Sonus Live Stage</dd>
          <dt>Componentes</dt><dd>JUCE, Munt (mt32emu), msfa / Dexed, RtAudio, RtMidi, cpp-httplib, nlohmann/json, React (detalle en NOTICE.md)</dd>
          {info?.dataDir && <><dt>Tus datos</dt><dd className="about__path">{info.dataDir}</dd></>}
        </dl>
        <p className="about__legal">Sonus Live Stage no incluye muestras, ROMs ni firmware de terceros, y no está afiliado a ningún
          fabricante de instrumentos. Los nombres de instrumentos clásicos describen el estilo sonoro de los presets.</p>
      </div>
    </Modal>
  );
}
