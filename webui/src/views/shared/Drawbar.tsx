// Organ drawbar (engine 0..1, played in eighths 0–8), shared by ORGAN and MAIN.
import { useMemo } from 'react';
import { DRAWBAR_FOOTAGES } from '../../data/constants';
import { useParam } from '../../store/hooks';
import { useMapAttrs } from '../../store/mappings';
import { FaderView } from '../../ui/controls';

const DRAWBAR_COLORS = ['brown', 'brown', 'white', 'white', 'black', 'white', 'black', 'black', 'white'] as const;

export function Drawbar({ index }: { index: number }) {
  const { value, spec, set } = useParam(`drawbar${index}`);
  const eighths = useMemo(() => (spec ? { ...spec, step: 0.125 } : undefined), [spec]);
  const mapAttrs = useMapAttrs(`drawbar${index}`);
  return <FaderView label={DRAWBAR_FOOTAGES[index]} value={value} spec={eighths} onChange={set} drawbar mapAttrs={mapAttrs}
    color={DRAWBAR_COLORS[index]} format={v => String(Math.round(v * 8))} />;
}
