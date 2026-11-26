// Master section: module mix, master output and keyboard performance controls.
import { KEYBOARD_TOUCH } from '../../data/constants';
import { useEngine, useParam } from '../../store/hooks';
import { ParamKnob } from '../../ui/Knob';
import { Meter, Panel, ParamLedChoice, ParamToggle } from '../../ui/controls';

function Stepper({ id, label, min, max }: { id: string; label: string; min: number; max: number }) {
  const { value, set } = useParam(id);
  const v = Math.round(value ?? 0);
  return (
    <div className="stepper" role="group" aria-label={label}>
      <span className="stepper__label">{label}</span>
      {/* One hardware rocker: − key · value window · + key (the window shows when it is not 0). */}
      <div className="stepper__controls stepper__rocker">
        <button type="button" className="stepper__key stepper__key--down" aria-label={`${label} −`} disabled={v <= min} onClick={() => set(v - 1)}>
          <span aria-hidden="true">−</span></button>
        <output className={`stepper__value${v !== 0 ? ' is-shifted' : ''}`}>{v > 0 ? `+${v}` : v}</output>
        <button type="button" className="stepper__key stepper__key--up" aria-label={`${label} +`} disabled={v >= max} onClick={() => set(v + 1)}>
          <span aria-hidden="true">+</span></button>
      </div>
    </div>
  );
}

const MODULES = [['PIANO', 'pianoEnabled', 'pianoVolume'], ['SYNTH', 'synthEnabled', 'synthMasterVol'], ['ORGAN', 'organEnabled', 'organVolume']] as const;

export function MasterSection() {
  const peakL = useEngine(s => s?.peakL ?? 0);
  const peakR = useEngine(s => s?.peakR ?? 0);
  const clip = useEngine(s => s?.clipping ?? false);
  return (
    <Panel title="MASTER" variant="chassis" className="master" actions={<Meter left={peakL} right={peakR} clip={clip} />}>
      <div className="mix">
        {MODULES.map(([name, power, level]) => (
          <div key={name} className="mix__strip">
            <ParamToggle id={power} label={name} small />
            <ParamKnob id={level} label="LEVEL" size="sm" />
          </div>
        ))}
        <div className="mix__strip mix__strip--master">
          <ParamKnob id="volume" label="MASTER" />
          <div className="row"><ParamKnob id="pan" label="PAN" size="sm" /><ParamKnob id="tune" label="TUNE" size="sm" /></div>
        </div>
      </div>
      <div className="master__perf">
        <Stepper id="transpose" label="TRANSPOSE" min={-12} max={12} />
        <Stepper id="octave" label="OCTAVE SHIFT" min={-2} max={2} />
        <div className="master__touch"><ParamLedChoice id="touch" label="KB TOUCH" options={KEYBOARD_TOUCH} /></div>
        <div className="master__softrel"><ParamToggle id="softRelease" label="SOFT REL" small /></div>
      </div>
    </Panel>
  );
}
