// PIANO: everything about the piano module only — layers and sound bank, layer
// mixer, per-layer sample editor (with layer Sounds) and piano Performances.
import { useState } from 'react';
import { PIANO_LAYER_COUNT } from '../../data/constants';
import { ParamKnob } from '../../ui/Knob';
import { Panel, ParamFader, ParamLed } from '../../ui/controls';
import { PianoLayersModule } from '../shared/PianoLayers';
import { PianoPerformances } from '../shared/PianoPerformances';
import { SampleEditor } from './SampleEditor';
import { ModuleTabs } from '../shared/ModuleTabs';

function LayerMixer({ selected, onSelect }: { selected: number; onSelect: (l: number) => void }) {
  return (
    <Panel title="LAYER MIX" variant="chassis" className="layer-mixer">
      <div className="layer-mixer__strips">
        {Array.from({ length: PIANO_LAYER_COUNT }, (_, l) => (
          <div key={l} className={`mixer-strip${selected === l ? ' is-selected' : ''}`}>
            <ParamFader id={`pianoLayerVol${l}`} label={`L${l + 1}`} ladder />
            <div className="mixer-strip__foot">
              <ParamLed id={`pianoLayerEnabled${l}`} label={`Capa L${l + 1}`} />
              <button type="button" className="mixer-strip__select" aria-pressed={selected === l} onClick={() => onSelect(l)}>EDIT</button>
            </div>
          </div>
        ))}
        <div className="mixer-strip mixer-strip--master">
          <ParamFader id="pianoVolume" label="PIANO" ladder />
          <div className="mixer-strip__foot"><ParamLed id="pianoEnabled" label="Piano" /></div>
        </div>
        <div className="layer-mixer__knobs">
          <ParamKnob id="pianoPan" label="PAN" size="sm" />
          <ParamKnob id="pianoTune" label="TUNE" size="sm" />
        </div>
      </div>
    </Panel>
  );
}

export function PianoView() {
  const [selected, setSelected] = useState(0);
  return (
    <ModuleTabs module={0}>
      <div className="view view--piano">
        <div className="stack piano-left">
          <PianoLayersModule selected={selected} onSelect={setSelected} />
          <LayerMixer selected={selected} onSelect={setSelected} />
        </div>
        <div className="stack piano-right">
          <SampleEditor layer={selected} onLayer={setSelected} />
          <PianoPerformances className="piano-perf" />
        </div>
      </div>
    </ModuleTabs>
  );
}
