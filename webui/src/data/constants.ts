// Display catalogues shared with the C++ engine. Indices are engine values.

export const SYNTH_WAVES: readonly string[] = [
  'SAW', 'PULSE', 'TRI', 'SINE', 'SUPERSAW', 'NOISE', 'WT', 'HARD SYNC', 'JUNO DCO', 'JUPITER DUAL',
  '2-OP FM', 'SUPERPULSE', 'RING MOD', 'VOX CHOIR', 'WAVEFOLDER', 'HARMONIC SINE', 'RAZOR', 'PHASE WARP',
  'HARMONIC ORGAN', 'SPECTRAL SAW', 'SAT SAW', 'PWM ENSEMBLE', 'BELL METAL', 'OCTAVE FIFTH', 'REESE DUAL',
  'FM GLASS', 'VOCAL SPECTRAL', 'BRIGHT SYNC', 'TRI ENSEMBLE',
  // v31
  'PD SAW', 'PD RESO', 'TRIPLE VCO', 'SYNC SWEEP', 'HYPERSAW', 'PINK NOISE', '8-BIT PULSE',
];
/** Waveforms offered as quick buttons; the rest live in the full list. */
export const SYNTH_QUICK_WAVES: readonly number[] = [0, 1, 2, 3, 4, 6, 10];
export const SYNTH_FILTER_TYPES: readonly string[] = ['LP12', 'HP12', 'BP12', 'NOTCH', 'LP24 LADDER', 'LP24 OTA'];
export const SYNTH_OSC2_WAVES: readonly string[] = ['SAW', 'PULSE', 'TRI'];
export const SYNTH_LFO_WAVES: readonly string[] = ['SINE', 'TRI', 'SAW', 'SQUARE', 'S&H', 'RANDOM'];
export const SYNTH_SYNTHESIS_TYPES: readonly string[] = ['SUBTRACTIVE', 'FM 2-OP', 'WAVETABLE', 'FM4', 'LA HYBRID', 'DX7', 'MT-32', 'LA32'];
/** FM4 algorithms (engine order). "4>3" means OP4 modulates OP3; carriers are listed after the colon. */
export const FM4_ALGORITHMS: readonly string[] = [
  '1 · 4>3>2>1', '2 · (3+4)>2>1', '3 · 4>3>1 + 2>1', '4 · 2>1 + 4>3', '5 · 4>(1,2,3)', '6 · 4>3>2 + 1', '7 · 4>3 + 1 + 2', '8 · ADDITIVE',
];
export const SYNTH_TRANSIENTS: readonly string[] = ['BREATH', 'MALLET', 'PLUCK', 'CHIFF', 'CLICK', 'GLASS', 'BOW', 'SPIT FM'];
export const SYNTH_VOICE_MODES: readonly string[] = ['POLY', 'MONO', 'LEGATO'];
export const SYNTH_GLIDE_MODES: readonly string[] = ['TIME', 'RATE'];
/** Engine order (SynthLayerConfig::wheelMode): 0 vibrato, 1 custom routes, 2 both, 3 off. */
export const SYNTH_WHEEL_MODES: readonly string[] = ['VIBRATO', 'CUSTOM', 'VIB+CUSTOM', 'OFF'];
export const SYNTH_LAYER_NAMES: readonly string[] = ['A', 'B', 'C', 'D'];
export const SYNTH_WAVETABLES: readonly string[] = [
  'Sine', 'Triangle', 'Square', 'Saw', 'Organ', 'Bell', 'Warm Pulse', 'Shimmer', 'Glass', 'Vowel A', 'Vowel O',
  'Metal', 'Analog Formant', 'Hollow', 'Octave', 'Bright Strings', 'Soft Reed', 'Lunar', 'Crystal', 'Nasal',
  'Spectrum', 'Fifth', 'Harmonic', 'Motion', 'Vox Bloom', 'PWM Silk', 'Celesta', 'Reese Spectrum', 'Reed Brass',
  'Digital Choir', 'Sparkle', 'Reso Motion', 'Glass Reed', 'Soft Mallet', 'Aero Pad', 'Acid Formant',
  'Pulse 25', 'Pulse 12', 'Choir Ahh', 'Choir Ooh', 'Clav', 'Harpsichord', 'Vibes', 'Breath Flute', 'Digital Bass',
  'E-Piano Tine', 'Nasal Brass', 'Hollow Square',
];

export const ORGAN_MODELS: readonly string[] = [
  'B3 TONEWHEEL', 'VOX CONTINENTAL', 'FARFISA COMPACT', 'PIPE ORGAN',
  'B3 VINTAGE MELLOW', 'B3 CONCERT BRIGHT', 'COMBO TRANSISTOR', 'THEATRE PIPE',
];
export const ORGAN_VIBRATO_MODES: readonly string[] = ['OFF', 'V1', 'V2', 'V3', 'C1', 'C2', 'C3'];
export const DRAWBAR_FOOTAGES: readonly string[] = ["16'", "5⅓'", "8'", "4'", "2⅔'", "2'", "1⅗'", "1⅓'", "1'"];
/** Percussion harmonic values understood by the engine (2nd / 3rd). */
export const ORGAN_PERC_HARMONICS: readonly { value: number; label: string }[] = [
  { value: 2, label: '2ND' }, { value: 3, label: '3RD' },
];

export const REVERB_TYPES: readonly string[] = ['ROOM', 'PLATE', 'HALL', 'SHIMMER'];
export const SHIMMER_PITCH_MODES: readonly string[] = ['BYPASS', 'SINGLE', 'DUAL', 'SINGLE REV', 'DUAL REV'];
export const SHIMMER_SPATIAL_MODES: readonly string[] = ['MONO', 'SMALL ST', 'MEDIUM ST', 'BIG ST'];
export const SHIMMER_COLOR_MODES: readonly string[] = ['BRIGHT', 'DARK'];
export const NOTE_DIVISIONS: readonly string[] = [
  'REDONDA', 'REDONDA ·', 'BLANCA ·', 'BLANCA', 'BLANCA 3', 'NEGRA', 'NEGRA ·', 'CORCHEA', 'CORCHEA ·',
  'SEMICORCHEA', 'SEMICORCHEA ·', 'FUSA', 'FUSA ·', 'NEGRA 3', 'CORCHEA 3', 'SEMICORCHEA 3',
];
export const KEYBOARD_TOUCH: readonly string[] = ['LIGHT', 'NORMAL', 'HEAVY'];

export const PIANO_LAYER_COUNT = 5;
export const SYNTH_LAYER_COUNT = 4;
/** Global program bank: 8 slots shown as 2 rows × 4 pads (backend kProgramSlots). */
export const PROGRAM_SLOTS = 8;

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
/** MIDI note number → name (60 = C4). */
export function noteName(note: number): string {
  const n = Math.max(0, Math.min(127, Math.round(note)));
  return `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
}
