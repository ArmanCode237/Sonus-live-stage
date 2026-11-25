// A small screen-printed emblem per studio effect (STUDIO_TYPES order), drawn in the
// slot accent colour (currentColor). Each effect gets its own faceplate in pro-console.css §12.
const PATHS: Record<number, string> = {
  1: 'M2 12 C7 2 11 2 16 12 S25 22 30 12 M2 14 C7 6 11 6 16 14 S25 22 30 14',             // CHORUS: two detuned waves
  2: 'M2 8 H8 C10 8 10 16 12 16 C14 16 14 8 16 8 C18 8 18 16 20 16 C22 16 22 8 24 8 H30',  // PHASER: moving notches
  3: 'M2 12 L4 6 L6 18 L8 7 L10 17 L12 9 L14 15 L16 11 L18 13 L20 11 L22 15 L24 9 L26 17 L28 7 L30 12', // TREMOLO: AM envelope
  4: 'M2 18 L5 6 L8 18 L11 6 L14 18 L17 6 L20 18 L23 6 L26 18 L29 6',                      // FLANGER: comb
  5: 'M3 20 V4 M10 20 V8 M17 20 V12 M24 20 V15 M30 20 V17',                                // STEREO DELAY: decaying taps
  6: 'M2 20 V6 M4 20 V7 M6 20 V8 M8 20 V10 M10 20 V11 M12 20 V12 M14 20 V14 M16 20 V15 M18 20 V16 M20 20 V17 M22 20 V18 M24 20 V18.5 M26 20 V19 M28 20 V19.5', // REVERB: dense tail
  7: 'M2 12 C4 4 6 4 8 4 H13 C15 4 15 20 18 20 H23 C25 20 26 12 30 12',                     // ANALOG DRIVE: clipped sine
  8: 'M3 12 H29 M3 12 L8 7 M3 12 L8 17 M29 12 L24 7 M29 12 L24 17',                         // AUTO-PAN: L ↔ R
  9: 'M3 21 L15 9 C18 6 22 5 29 4',                                                         // COMPRESSOR: knee
  10: 'M2 20 H7 V15 H12 V10 H17 V6 H22 V10 H27 V15 H30',                                     // BITCRUSHER: steps
  11: 'M16 12 L3 4 M16 12 L3 20 M16 12 L29 4 M16 12 L29 20 M16 12 H3 M16 12 H29',             // STEREO WIDTH: spread
  12: 'M2 16 C9 16 10 5 14 5 C18 5 19 16 24 16 C26 16 27 14 30 13',                          // PARAMETRIC EQ: bell
};

export function FxEmblem({ type }: { type: number }) {
  const d = PATHS[type];
  if (!d) return null;
  return (
    <svg className="fx-emblem" viewBox="0 0 32 24" aria-hidden="true" focusable="false">
      <path d={d} fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
