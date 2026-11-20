// v32.4 layer EQ, first unit of the synth INSERTS: high / low pass (12, 24 or 48 dB/oct)
// and three peaking bands (gain, frequency, Q). Same filters as the piano Sound EQ.
import { useParam } from '../../store/hooks';
import { EqScreen } from '../../ui/EqScreen';
import type { EqModel } from '../../ui/eqResponse';
import { ParamKnob } from '../../ui/Knob';
import { ParamSegmented, ParamToggle } from '../../ui/controls';

const SLOPES = ['12', '24', '48'];
const SLOPE_VALUES = [12, 24, 48];

function useLayerEqModel(L: string): EqModel {
  const v = (id: string) => useParam(`${L}${id}`).value;   // eslint-disable-line react-hooks/rules-of-hooks
  const enabled = (v('eqOn') ?? 0) >= .5;
  const hpHz = v('eqHp'), lpHz = v('eqLp'), hpSlope = v('eqHpSlope'), lpSlope = v('eqLpSlope');
  const bands = [0, 1, 2].map(b => ({ freqHz: v(`eqF${b}`) ?? 1000, q: v(`eqQ${b}`) ?? 1, gainDb: v(`eqG${b}`) ?? 0 }));
  return {
    enabled, hpHz, lpHz, bands,
    hpSlope: hpSlope === undefined ? undefined : Math.round(hpSlope),
    lpSlope: lpSlope === undefined ? undefined : Math.round(lpSlope),
  };
}

export function SynthEq({ L }: { L: string }) {
  const model = useLayerEqModel(L);
  return (
    <fieldset className="group synth-eq"><legend>EQ</legend>
      <EqScreen model={model} title={`EQ · CAPA ${'ABCD'[Number(L.slice(1, -1)) - 1]}`} className="synth-eq__screen" />
      <div className="synth-eq__cut">
        <ParamToggle id={`${L}eqOn`} label="EQ ON" small />
      </div>
      <div className="synth-eq__cut">
        <span className="synth-eq__title">LOW CUT · HP</span>
        <ParamKnob id={`${L}eqHp`} label="FREQ" size="sm" />
        <ParamSegmented id={`${L}eqHpSlope`} label="Pendiente del pasa altos (dB/oct)" options={SLOPES} values={SLOPE_VALUES} />
      </div>
      <div className="synth-eq__cut">
        <span className="synth-eq__title">HIGH CUT · LP</span>
        <ParamKnob id={`${L}eqLp`} label="FREQ" size="sm" />
        <ParamSegmented id={`${L}eqLpSlope`} label="Pendiente del pasa bajos (dB/oct)" options={SLOPES} values={SLOPE_VALUES} />
      </div>
      {['LOW', 'MID', 'HIGH'].map((name, b) => (
        <div key={name} className="synth-eq__band">
          <span className="synth-eq__title">{name}</span>
          <div className="knobs">
            <ParamKnob id={`${L}eqG${b}`} label="GAIN" size="sm" /><ParamKnob id={`${L}eqF${b}`} label="FREQ" size="sm" />
            <ParamKnob id={`${L}eqQ${b}`} label="Q" size="sm" />
          </div>
        </div>
      ))}
    </fieldset>
  );
}
