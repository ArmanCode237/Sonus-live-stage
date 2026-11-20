// DX7 layer (synthesis type 5): pick a voice from the factory cartridges (the Dexed
// banks) or from imported .syx cartridges. The voice is played by msfa, the DX7
// engine used by Dexed, so imported DX7 patches sound like on the original.
import { useEffect, useMemo, useRef, useState } from 'react';
import { api } from '../../api/client';
import type { Dx7Bank } from '../../api/types';
import { useEngine } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { Btn, Empty, Select } from '../../ui/controls';

let bankCache: Dx7Bank[] | null = null;

/** Voice name stored in the layer's DX7 voice (VCED bytes 145-154). */
function voiceNameFromHex(hex: string | undefined): string {
  if (!hex || hex.length < 310) return '';
  let name = '';
  for (let i = 145; i < 155; i++) name += String.fromCharCode(parseInt(hex.slice(i * 2, i * 2 + 2), 16));
  return name.trim();
}

const toHex = (bytes: Uint8Array) => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

export function Dx7Browser({ layer }: { layer: number }) {
  const [banks, setBanks] = useState<Dx7Bank[]>(bankCache ?? []);
  const [bankId, setBankId] = useState('');
  const [query, setQuery] = useState('');
  const fileRef = useRef<HTMLInputElement>(null);
  const hex = useEngine(s => (s?.params?.synthLayers?.[layer] as { dx7Voice?: string } | undefined)?.dx7Voice);
  const current = voiceNameFromHex(hex);

  const reload = () => api.dx7Banks().then(r => { bankCache = r.banks; setBanks(r.banks); }).catch(() => {});
  useEffect(() => { if (!bankCache) void reload(); }, []);
  useEffect(() => { if (!bankId && banks.length) setBankId(banks[0].id); }, [banks, bankId]);

  const bank = banks.find(b => b.id === bankId);
  // Search runs across every cartridge; without a query the selected one is shown.
  const results = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return bank ? bank.voices.map((name, voice) => ({ bank, voice, name })) : [];
    return banks.flatMap(b => b.voices.map((name, voice) => ({ bank: b, voice, name }))).filter(r => r.name.toLowerCase().includes(q)).slice(0, 96);
  }, [bank, banks, query]);

  const load = (b: Dx7Bank, voice: number) => void runAction(() => api.dx7Load(layer, b.id, voice), `DX7 · ${b.voices[voice]}`, true);
  const importFile = async (file: File) => {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (await runAction(() => api.dx7Import(file.name, toHex(bytes)), `Cartucho «${file.name}» importado`)) {
      await reload();
      setBankId(`user/${file.name.replace(/\.syx$/i, '').replace(/[^A-Za-z0-9 _.-]/g, '')}`);
    }
  };
  const removeBank = async () => {
    if (!bank || bank.source !== 'user') return;
    if (await runAction(() => api.dx7Delete(bank.id), 'Cartucho eliminado')) { setBankId(''); await reload(); }
  };

  return (
    <fieldset className="group group--wide dx7"><legend>DX7 · VOICE</legend>
      <div className="dx7__lcd lcd">
        <div className="lcd__bar"><span>DX7 · msfa</span><span>{bank ? `${bank.source === 'factory' ? 'FÁBRICA' : 'USUARIO'} · ${bank.name}` : ''}</span></div>
        <div className="mini-lcd__name">{current || 'INIT VOICE'}</div>
      </div>
      <div className="row dx7__bar">
        <Select label="Cartucho DX7" value={bankId} options={banks.map(b => ({ value: b.id, label: `${b.source === 'user' ? '★ ' : ''}${b.name}` }))}
          onChange={v => { setBankId(v); setQuery(''); }} />
        <input className="input grow" type="search" placeholder="Buscar en todos los cartuchos…" aria-label="Buscar voz DX7"
          value={query} onChange={e => setQuery(e.target.value)} />
        <Btn small variant="browse" onClick={() => fileRef.current?.click()} title="Importar un cartucho .syx (32 voces) o una voz">IMPORTAR .SYX</Btn>
        {bank?.source === 'user' && <Btn small variant="danger" onClick={() => void removeBank()}>BORRAR</Btn>}
        <input ref={fileRef} type="file" accept=".syx" hidden onChange={e => { const f = e.target.files?.[0]; if (f) void importFile(f); e.target.value = ''; }} />
      </div>
      <ul className="dx7__voices" aria-label="Voces DX7">
        {results.map(r => (
          <li key={`${r.bank.id}:${r.voice}`}>
            <button type="button" className={`dx7__voice${r.name === current ? ' is-active' : ''}`} onClick={() => load(r.bank, r.voice)}
              title={`${r.bank.name} · ${r.voice + 1}`}>
              <span className="dx7__num">{String(r.voice + 1).padStart(2, '0')}</span>
              <span className="dx7__name">{r.name}</span>
              {query && <span className="dx7__bank">{r.bank.name}</span>}
            </button>
          </li>
        ))}
        {results.length === 0 && <Empty>{banks.length ? 'Sin resultados.' : 'Cargando cartuchos…'}</Empty>}
      </ul>
      <p className="hint">Motor DX7 de 6 operadores (msfa, el núcleo de Dexed): algoritmos, envolventes, escalado y LFO como en el original.
        Incluye los 33 cartuchos de Dexed (Dexed_01 y SynprezFM). Volumen, pan, octava, inserts y efectos de la capa siguen activos.</p>
    </fieldset>
  );
}
