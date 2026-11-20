// One component per module: the provider hooks (and their engine requests) only run while
// the browser is open.
import { useState } from 'react';
import { SYNTH_LAYER_NAMES } from '../data/constants';
import { Modal } from '../ui/Modal';
import { Dx7Browser } from '../views/synth/Dx7Browser';
import { Mt32Browser } from '../views/synth/Mt32Browser';
import { SoundBrowser } from './SoundBrowser';
import { useOrganSources, usePianoSources, useSynthSources } from './providers';

export function SynthSoundBrowser({ layer = 0, onClose }: { layer?: number; onClose: () => void }) {
  const provider = useSynthSources();
  const [source, setSource] = useState<'dx7' | 'mt32' | null>(null);
  const close = () => { setSource(null); provider.reload(); };
  const extra = (
    <>
      <button type="button" className="sb__facet" onClick={() => setSource('dx7')} title="Importar o borrar cartuchos .syx del DX7"><span>DX7 · CARTUCHOS…</span></button>
      <button type="button" className="sb__facet" onClick={() => setSource('mt32')} title="Estado de las ROMs del MT-32"><span>MT-32 · ROMS…</span></button>
    </>
  );
  return (
    <>
      <SoundBrowser module="synth" title="SYNTH · BROWSER" provider={provider} layer={layer} onClose={onClose} extra={extra} />
      {source && (
        <Modal title={`${source === 'dx7' ? 'DX7 · CARTUCHOS' : 'MT-32 · ROMS'} → CAPA ${SYNTH_LAYER_NAMES[layer]}`} onClose={close} wide>
          {source === 'dx7' ? <Dx7Browser layer={layer} /> : <Mt32Browser layer={layer} />}
        </Modal>
      )}
    </>
  );
}

export function OrganSoundBrowser({ onClose }: { onClose: () => void }) {
  return <SoundBrowser module="organ" title="ORGAN · BROWSER" provider={useOrganSources()} onClose={onClose} />;
}

export function PianoSoundBrowser({ layer, onClose }: { layer: number; onClose: () => void }) {
  return <SoundBrowser module="piano" title={`PIANO · BROWSER · CAPA L${layer + 1}`} provider={usePianoSources(layer)} layer={layer} onClose={onClose} />;
}
