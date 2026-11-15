// Per-layer insert chain, shared by synth layers (L1_fx{u}_*) and piano layer
// Sounds (pianoFx{layer}_{u}_*). Units that are off cost no processing.
import { NOTE_DIVISIONS } from '../../data/constants';
import { ParamKnob } from '../../ui/Knob';
import { ParamLed, ParamSegmented, ParamSelect, ParamToggle, Row } from '../../ui/controls';

// Engine order (dsp/SynthLayerInsertFx.h): Chorus I/II, Phaser I/II, echo, reverb.
const UNITS = ['CHORUS I', 'CHORUS II', 'PHASER I', 'PHASER II', 'ECHO', 'REVERB'] as const;
const ECHO = 4, REVERB = 5;
export const INSERT_REVERB_TYPES: readonly string[] = ['ROOM', 'PLATE', 'HALL'];

/** `unitPrefix(u)` returns the parameter prefix of unit u, e.g. "L1_fx5_" or "pianoFx0_5_". */
export function InsertChain({ unitPrefix }: { unitPrefix: (unit: number) => string }) {
  return (
    <div className="inserts">
      {UNITS.map((name, i) => {
        const fx = unitPrefix(i);
        return (
          <fieldset key={name} className="group"><legend><ParamLed id={`${fx}on`} label={name} /> {name}</legend>
            {i === REVERB && <ParamSegmented id={`${fx}type`} label="Tipo de reverb" options={INSERT_REVERB_TYPES} />}
            <div className="knobs">
              <ParamKnob id={`${fx}mix`} label="MIX" size="sm" />
              {i === REVERB ? (
                <>
                  <ParamKnob id={`${fx}depth`} label="SIZE" size="sm" />
                  <ParamKnob id={`${fx}damp`} label="DAMP" size="sm" />
                  <ParamKnob id={`${fx}predelay`} label="PRE-DLY" size="sm" />
                </>
              ) : (
                <>
                  <ParamKnob id={`${fx}rate`} label="RATE" size="sm" /><ParamKnob id={`${fx}depth`} label="DEPTH" size="sm" />
                  <ParamKnob id={`${fx}feedback`} label="FDBK" size="sm" />
                  {i === ECHO && <ParamKnob id={`${fx}time`} label="TIME" size="sm" />}
                </>
              )}
            </div>
            {i === ECHO && <Row wrap><ParamToggle id={`${fx}sync`} label="SYNC" small /><ParamSelect id={`${fx}division`} label="NOTA" options={NOTE_DIVISIONS} /></Row>}
          </fieldset>
        );
      })}
    </div>
  );
}
