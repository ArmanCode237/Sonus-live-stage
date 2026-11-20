// Library: sample sources & PCM cache, Sounds of every module, piano Performances.
import { useCallback, useEffect, useState } from 'react';
import { api } from '../../api/client';
import { chooseSamplePath, hasNativeSamplePicker } from '../../api/nativePicker';
import type { SampleLibraryStatus, SoundSummary } from '../../api/types';
import { notify, runAction } from '../../store/notify';
import { PianoPerformances } from '../shared/PianoPerformances';
import { Btn, Empty, Field, Panel, Row, Segmented } from '../../ui/controls';

function Sources() {
  const [status, setStatus] = useState<SampleLibraryStatus | null>(null);
  const [error, setError] = useState('');
  const [path, setPath] = useState('');
  const [pickerBusy, setPickerBusy] = useState(false);
  const [type, setType] = useState<'root' | 'file'>('root');
  const [cacheDir, setCacheDir] = useState<string | null>(null);
  const [oldRoot, setOldRoot] = useState('');
  const [newRoot, setNewRoot] = useState('');
  const load = useCallback(() => api.sampleLibraryStatus().then(s => { setStatus(s); setError(''); }).catch(e => setError((e as Error).message)), []);
  useEffect(() => { void load(); }, [load]);
  const running = status?.progress.running ?? false;
  useEffect(() => {
    if (!running) return;
    const t = setInterval(() => void load(), 700);
    return () => clearInterval(t);
  }, [running, load]);

  const pick = async (kind: 'folder' | 'file', onPicked: (value: string) => void) => {
    setPickerBusy(true);
    try {
      const selected = await chooseSamplePath(kind);
      if (selected !== null) onPicked(selected);
    } catch (e) {
      notify(`No se pudo abrir el explorador: ${(e as Error).message}`, 'error');
    } finally { setPickerBusy(false); }
  };
  const nativePicker = hasNativeSamplePicker();
  const add = async () => {
    const r = await runAction(() => api.sampleLibrarySource('add', type, path.trim()), 'Fuente añadida');
    if (r) { setStatus(r); setPath(''); }
  };
  const remove = async (kind: 'root' | 'file', p: string) => { const r = await runAction(() => api.sampleLibrarySource('remove', kind, p), 'Fuente eliminada'); if (r) setStatus(r); };
  const progress = status?.progress;
  const pct = progress && progress.total > 0 ? Math.round((progress.done / progress.total) * 100) : 0;
  const sources = [...(status?.configuration.roots ?? []).map(p => ['root', p] as const), ...(status?.configuration.files ?? []).map(p => ['file', p] as const)];

  return (
    <Panel title="SAMPLE SOURCES · PCM CACHE" className="lib-sources">
      {error && <Empty>{error}</Empty>}
      <form className="row" onSubmit={e => { e.preventDefault(); if (path.trim()) void add(); }}>
        <Segmented label="Tipo de fuente" options={['CARPETA', 'ARCHIVO']} value={type === 'root' ? 0 : 1} onChange={i => setType(i === 0 ? 'root' : 'file')} />
        <input className="input grow" placeholder={type === 'root' ? 'Ruta absoluta de una carpeta, p. ej. D:\\Samples' : 'Ruta absoluta de un .npno, .nsmp, .sf2 o .sfz'}
          value={path} onChange={e => setPath(e.target.value)} aria-label="Ruta de la fuente" />
        {nativePicker && <Btn type="button" disabled={pickerBusy} onClick={() => void pick(type === 'root' ? 'folder' : 'file', setPath)}
          title={type === 'root' ? 'Seleccionar carpeta de samples' : 'Seleccionar archivo de samples'}>EXPLORAR…</Btn>}
        <Btn variant="primary" type="submit" disabled={!path.trim()}>AÑADIR</Btn>
      </form>
      <ul className="list">
        {sources.map(([kind, p]) => (
          <li key={kind + p} className="list__row">
            <span className="list__main list__main--static"><span className="tag">{kind === 'root' ? 'CARPETA' : 'ARCHIVO'}</span>{p}</span>
            <button type="button" className="icon-btn" aria-label={`Quitar ${p}`} onClick={() => remove(kind, p)}>×</button>
          </li>
        ))}
        {status && sources.length === 0 && <Empty>Solo se usa la carpeta de samples por defecto. Los originales nunca se copian.</Empty>}
      </ul>
      <div className="progress" aria-label="Preparación de la caché">
        <div className="progress__bar"><div style={{ width: `${pct}%` }} /></div>
        <span className="progress__text">
          {running ? `${progress?.done}/${progress?.total} · ${progress?.current ?? ''}` : progress && progress.total > 0 ? `Terminado: ${progress.done}/${progress.total}${progress.failed ? ` · ${progress.failed} con error` : ''}` : 'Caché inactiva'}
        </span>
      </div>
      {progress?.lastError && <p className="hint hint--error">{progress.lastError}</p>}
      <Row>
        <Btn variant="primary" disabled={running} onClick={() => runAction(() => api.sampleLibraryPrepare(), 'Preparación iniciada').then(() => load())}>PREPARAR BIBLIOTECA</Btn>
        <Btn disabled={!running} onClick={() => runAction(() => api.sampleLibraryCancel(), 'Cancelado').then(() => load())}>CANCELAR</Btn>
      </Row>
      <form className="row" onSubmit={e => { e.preventDefault(); if (cacheDir?.trim()) void runAction(() => api.sampleLibraryCacheDir(cacheDir.trim()), 'Carpeta de caché actualizada').then(() => { setCacheDir(null); void load(); }); }}>
        <Field label="Caché PCM16">
          <input className="input" value={cacheDir ?? status?.configuration.cacheDir ?? ''} onChange={e => setCacheDir(e.target.value)} />
        </Field>
        {nativePicker && <Btn type="button" disabled={pickerBusy} onClick={() => void pick('folder', setCacheDir)}>EXPLORAR…</Btn>}
        <Btn type="submit" disabled={cacheDir === null}>GUARDAR</Btn>
      </form>
      <details className="details">
        <summary>Reubicar una carpeta movida o renombrada</summary>
        <form className="form" onSubmit={e => { e.preventDefault(); void runAction(() => api.sampleLibraryRelocate(oldRoot.trim(), newRoot.trim()), 'Referencias reparadas'); }}>
          <Field label="Carpeta anterior"><input className="input" value={oldRoot} onChange={e => setOldRoot(e.target.value)} placeholder="D:\Samples Antiguos" /></Field>
          <Field label="Carpeta actual"><input className="input" value={newRoot} onChange={e => setNewRoot(e.target.value)} placeholder="D:\Samples Nuevos" /></Field>
          {nativePicker && <Btn type="button" disabled={pickerBusy} onClick={() => void pick('folder', setNewRoot)}>EXPLORAR CARPETA…</Btn>}
          <Btn variant="primary" type="submit" disabled={!oldRoot.trim() || !newRoot.trim() || running}>REPARAR REFERENCIAS</Btn>
        </form>
      </details>
    </Panel>
  );
}

const MODULES = ['piano', 'synth', 'organ'] as const;

function SoundsManager() {
  const [module, setModule] = useState(0);
  const [sounds, setSounds] = useState<SoundSummary[] | null>(null);
  const [error, setError] = useState('');
  const [name, setName] = useState('');
  const [category, setCategory] = useState('');
  const mod = MODULES[module];
  const reload = useCallback(() => { api.sounds(mod).then(r => setSounds(r.sounds)).catch(e => setError((e as Error).message)); }, [mod]);
  useEffect(() => { setSounds(null); reload(); }, [reload]);
  const save = async () => {
    if (await runAction(() => api.saveSound({ module: mod, name: name.trim(), category: category.trim() || undefined }), `«${name.trim()}» guardado`)) { setName(''); reload(); }
  };
  const act = async (fn: () => Promise<unknown>, msg: string) => { if (await runAction(fn, msg)) reload(); };
  return (
    <Panel title="SOUNDS" className="lib-sounds" actions={<Segmented label="Módulo" options={['PIANO', 'SYNTH', 'ORGAN']} value={module} onChange={setModule} />}>
      <form className="row" onSubmit={e => { e.preventDefault(); if (name.trim()) void save(); }}>
        <input className="input grow" placeholder={`Guardar ${mod.toUpperCase()} actual como Sound…`} maxLength={64} value={name} onChange={e => setName(e.target.value)} aria-label="Nombre del Sound" />
        <input className="input" placeholder="Categoría" maxLength={64} value={category} onChange={e => setCategory(e.target.value)} aria-label="Categoría" />
        <Btn variant="primary" type="submit" disabled={!name.trim()}>GUARDAR</Btn>
      </form>
      {error && <Empty>{error}</Empty>}
      {sounds && sounds.length === 0 && <Empty>No hay Sounds de {mod}.</Empty>}
      <ul className="list list--scroll">
        {sounds?.map(s => (
          <li key={s.id} className="list__row">
            <button type="button" className="list__main" onClick={() => act(() => api.loadSound(s.id, s.scope === 'layer' ? s.savedLayer : undefined), `«${s.name}» cargado`)}>
              <span>{s.defaultForSource ? '◆ ' : ''}{s.name}</span>
              <span className="muted">{[s.category, s.scope === 'layer' ? `L${(s.savedLayer ?? 0) + 1}` : '', s.instruments?.[0]].filter(Boolean).join(' · ')}</span>
            </button>
            {mod === 'piano' && <Btn small onClick={() => act(() => api.setDefaultSound(s.id), 'Sound por defecto del instrumento')} title="Usar al cargar este instrumento">DEFAULT</Btn>}
            <Btn small onClick={() => act(() => api.duplicateSound(s.id, `${s.name} copy`), 'Duplicado')}>DUP</Btn>
            <button type="button" className="icon-btn" aria-label={`Eliminar ${s.name}`} onClick={() => act(() => api.deleteSound(s.id), `«${s.name}» eliminado`)}>×</button>
          </li>
        ))}
      </ul>
    </Panel>
  );
}

/** Backup of programs.json: export downloads the 8 slots, import replaces them. */
function ProgramsBackup() {
  const exportPrograms = async () => {
    const programs = await runAction(() => api.programs());
    if (!programs) return;
    const blob = new Blob([JSON.stringify({ programs }, null, 2)], { type: 'application/json' });
    const link = Object.assign(document.createElement('a'), {
      href: URL.createObjectURL(blob), download: `live-stage-programs-${new Date().toISOString().slice(0, 10)}.json`,
    });
    link.click();
    URL.revokeObjectURL(link.href);
  };
  const importPrograms = async (file: File | undefined) => {
    if (!file) return;
    let data: unknown;
    try { data = JSON.parse(await file.text()); } catch { notify('El archivo no es JSON válido', 'error'); return; }
    await runAction(() => api.importPrograms(data), 'Programas importados (8 slots reemplazados)');
  };
  return (
    <Row wrap className="backup">
      <span className="subhead">GLOBAL PROGRAMS</span>
      <Btn small onClick={exportPrograms}>EXPORTAR JSON</Btn>
      <label className="btn btn--panel btn--sm">IMPORTAR JSON
        <input type="file" accept="application/json,.json" hidden onChange={e => { void importPrograms(e.target.files?.[0]); e.target.value = ''; }} />
      </label>
      <span className="hint">Importar reemplaza los 8 slots (un archivo antiguo de 16 aporta sus 8 primeros).</span>
    </Row>
  );
}

export default function LibraryView() {
  return (
    <div className="view view--library">
      <Sources />
      <SoundsManager />
      <PianoPerformances className="lib-perf"><ProgramsBackup /></PianoPerformances>
    </div>
  );
}
