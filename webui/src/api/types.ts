// Types for the C++ HTTP contract (src/app/routes/*.cpp). Fields the UI does not
// read are typed loosely on purpose so backend additions never break the client.
import type { StudioRack } from '../data/studio';

export type Json = Record<string, unknown>;

/** Synth factory metadata (user data): saved edits per slot, favourites, hidden slots. */
export interface FactoryMeta {
  overrides: Record<string, { name?: string; params?: Json }>;
  favorites: number[];
  hidden: number[];
}

export interface Zone { low: number; high: number; transpose: number; octave?: number }
export interface FxSends { modulation: number; delay: number; reverb: number }
export interface EqBand { freqHz: number; gainDb: number; q: number }

export interface PianoSampleConfig {
  attackSec: number; decaySec: number; sustainLevel: number; releaseSec: number; loopHold: number;
  pan: number; fineTuneCents: number; cutoffHz: number; resonanceQ: number; gainDb: number;
  monoMode: boolean; legatoEnabled: boolean;
  portamentoEnabled: boolean; portamentoSec: number; portamentoMode: number;
  wheelCutoffEnabled: boolean; wheelCutoffInvert: boolean; wheelCutoffOctaves: number;
  wheelAmpEnabled: boolean; wheelAmpDb: number;
  wheelVibratoEnabled: boolean; wheelVibratoCents: number; wheelVibratoRateHz: number;
  soundEqEnabled: boolean; soundEqHighPass: number; soundEqLowPass: number;
  soundEqHighPassSlope: number; soundEqLowPassSlope: number;
  soundEqFreq: number[]; soundEqQ: number[]; soundEqGain: number[];
}

export interface InsertFx {
  enabled: boolean; rateHz: number; depth: number; mix: number; feedback: number;
  timeMs: number; syncEnabled: boolean; syncDivision: number;
}

export interface SynthLayer {
  enabled: boolean; waveType: number; volume: number; pan: number; octave: number; semitones: number;
  detuneCents: number; cutoffHz: number; resonanceQ: number; attackSec: number; decaySec: number;
  sustainLevel: number; releaseSec: number; unisonVoices: number; unisonDetune: number; unisonSpread: number;
  phaseOffset: number; keyTrack: number; velToCutoff: number; velToAmp: number; wavetableIdx: number;
  wavetableMorph: number; filterType: number; filterEnvAmt: number; filterKeyTrack: number;
  filtAttackSec: number; filtDecaySec: number; filtSustainLvl: number; filtReleaseSec: number;
  sawLevel: number; pulseLevel: number; pulseWidth: number; pwmDepth: number; pwmRateHz: number;
  subLevel: number; noiseLevel: number; osc2Level: number; osc2Detune: number; osc2Semitones: number;
  osc2Wave: number; highpassHz: number; drive: number; synthesisType: number; fmIndex: number; fmRatio: number;
  wheelMode: number; wheelCutoffOctaves: number; wheelAmpDb: number; wheelVibratoCents: number;
  wheelVibratoRateHz: number; wheelWtMorph: number; voiceMode: number; portamentoEnabled: boolean;
  portamento: number; glideMode: number; slide: number; keyLow: number; keyHigh: number; keyTranspose: number;
  seqEnabled: boolean; seqBpm: number; seqSteps: number; seqGate: number; seqNotes: number[];
  insertFx: InsertFx[];
}

export interface SynthLfo {
  enabled: boolean; waveType: number; rateHz: number; depth: number; destPitch: number;
  destCutoff: number; destVolume: number; destPan: number; destPhase: number; delaySec: number; fadeSec: number;
}

/** serializeParams() output: the full engine edit buffer. */
export interface EngineParams extends Json {
  pianoEnabled: boolean; pianoVolume: number; pianoPan: number; pianoTune: number;
  pianoLayers: { enabled: boolean; volume: number }[];
  pianoSampleConfig: PianoSampleConfig[];
  pianoZone: Zone; pianoLayerZones: Zone[]; synthZone: Zone; organZone: Zone;
  fxSends: FxSends[]; synthFxSends: FxSends[]; organFxSends: FxSends;
  eqEnabled: boolean; eqBands: EqBand[];
  synthEnabled: boolean; synthPresetId: number; synthMasterVol: number; synthLayers: SynthLayer[];
  synthLfos: SynthLfo[]; synthFx: Json; organFx: Json;
  organEnabled: boolean; organModel: number; organVolume: number; drawbars: number[];
  organVibratoMode: number; organPercEnabled: boolean; organPercHarmonic: number; organRotaryFast: boolean;
  studioRacks: StudioRack[]; studioRoutingEnabled: boolean;
  masterBpm: number; volume: number; transposeSemis: number; octaveShift: number; kbdTouch: number;
  softRelease: boolean; maxVoices: number;
}

export interface PresetRef { name: string; kind: string }

export interface PianoLayerSource {
  layer: number; name: string; sourcePath: string; fileName: string; instrumentId: number; sf2Preset: number;
}

export interface EngineState {
  activeProgramSlot: number; programName: string; paramRev: number;
  pianoLayers: PianoLayerSource[];
  instrumentName: string; statusMessage: string; libraryRoot: string;
  activeVoices: number; maxVoices: number; audioUnderflows: number; audioOverBudgetCallbacks: number;
  peakL: number; peakR: number; clipping: boolean; pedalDown: boolean;
  /** v32.4 meters (linear peak): modules piano/synth/organ, sources piano 0-4 · synth 5-8 · organ 9. */
  modulePeaks?: number[]; sourcePeaks?: number[];
  /** v32.4 live safety: DSP load (render time / buffer, avg and decaying peak), MIDI clock,
   *  audio watchdog, session recovery and samples missing from the 8 programs. */
  dspLoad?: number; dspPeak?: number; maxRenderMicros?: number; midiDropped?: number;
  midiClockSync?: boolean; midiClockActive?: boolean; midiClockBpm?: number;
  audioRecovering?: boolean; audioRecoveries?: number;
  sessionRecovery?: { savedAt: string; programName: string } | null;
  missingSamples?: { slot: number; program: string; file: string }[];
  lastMidiNote: number; lastMidiVel: number; midiSeq: number; modWheel: number; pitchBend: number;
  activeKeys: { k: number; s: number }[];
  connectedMidiCount: number; disconnectedMidiCount: number; savedAudioMissing: boolean;
  memoryMB: number;
  /** Changes whenever any mapping scope changes (v31). */
  mappingRev?: number;
  /** Unsaved edits per module: piano, synth, organ (v31). */
  moduleDirty?: boolean[];
  /** v32.5: synth / organ preset playing now (null = factory synth patch / unnamed organ). */
  presetRefs?: { synth: PresetRef | null; organ: PresetRef | null };
  /** Unsaved edits per piano layer Sound (v31). */
  soundDirty?: boolean[];
  params: EngineParams;
}

export interface ProgramSlot { slot: number; name: string; soundRefs?: Json }

export interface LibraryItem {
  name: string; path: string; ext: string; group: string; category: string;
  favorite: boolean; ready: boolean; sizeMB: number;
}

export interface SoundSummary {
  id: string; name: string; module: 'piano' | 'synth' | 'organ'; category?: string;
  scope?: string; savedLayer?: number; defaultForSource?: boolean; instruments?: string[];
  tags?: string[]; updated?: number;
}

export interface NamedPreset { name: string; favorite?: boolean; timestamp?: number }

/** module: 0 piano, 1 synth, 2 organ, -1 global (master, programs). */
export interface ParameterCatalogEntry { id: string; min: number; max: number; module?: number }
export interface ControlState {
  parameters: ParameterCatalogEntry[]; oscRunning: boolean; oscPort: number; lastOscAddress: string;
}

// ---------------------------------------------------------------- MIDI / OSC mappings (v31)
export type MapScope = 'system' | 'sound' | 'module' | 'program';
export type Takeover = 'pickup' | 'jump' | 'relative';
export type ButtonMode = 'continuous' | 'toggle' | 'momentary';
/** port (v33): MIDI input port name; absent = any keyboard. */
export interface MidiSource { type: 'cc' | 'note'; channel: number; number: number; port?: string }
export interface Assignment {
  paramId: string; scope: MapScope; module?: number; layer?: number;
  midi: MidiSource | null; osc: string | null;
  min: number; max: number; invert: boolean; takeover: Takeover; button: ButtonMode;
  /** True when this scope's assignment is the one in effect (PROGRAM > MODULE > SOUND > SYSTEM). */
  effective: boolean; midiLabel?: string;
}
export interface MappingRef { paramId: string; scope: MapScope; module?: number; layer?: number }
export interface MappingConflict { type: 'midi' | 'osc'; source: string; system: MappingRef[]; others: MappingRef[] }
export interface LearnCapture { type: 'midi' | 'osc'; kind?: 'cc' | 'note'; channel?: number; number?: number; port?: string; address?: string; label: string }
export interface LearnState { active: boolean; captured: LearnCapture | null; revision: number }
export interface MappingState {
  assignments: Assignment[]; conflicts: MappingConflict[]; revision: number;
  learn: LearnState; lastInput: Json | null; osc: { port: number; running: boolean; lastAddress: string };
}

export interface AudioDevice {
  uid: number; api: string; name: string; channels: number; connected: boolean;
  preferredSr: number; sampleRates: number[];
}
export interface MidiPort { index: number; name: string; connected: boolean; enabled: boolean }
export interface HardwareStatus extends Json {
  activeApi: string; activeAudioDeviceName: string; activeAudioUid: number;
  asioCompiled: boolean; audioDevices: AudioDevice[]; availableApis: string[];
  sampleRate: number; bufferSize: number; latencyMs: number; lastAudioError: string;
  audioUnderflows: number; maxAudioRenderMicros: number;
  midiPorts: MidiPort[];
  /** v33: 'plugin' when the DAW owns audio and MIDI (VST3 / AU). */
  host?: 'app' | 'plugin';
}

export interface SampleLibraryStatus extends Json {
  configuration: { roots: string[]; files: string[]; cacheDir: string };
  progress: { running: boolean; cancelled: boolean; total: number; done: number; failed: number; skipped: number; current: string; lastError: string };
}

/** A DX7 32-voice cartridge (factory = Dexed banks, user = imported .syx). */
export interface Dx7Bank { id: string; name: string; source: 'factory' | 'user'; voices: string[] }

/** Emulated Roland MT-32 (Munt): ROM status and the 128 timbre names. */
export interface Mt32Status { ok: boolean; loaded: boolean; control: string; pcm: string; message: string; folders: string[]; sounds: string[] }

/** A Roland D-50 patch bank found under roms/ (64 patches, numbered 11-88 on the D-50). */
export interface D50Bank { id: string; name: string; patches: { n: number; name: string; upper: string; lower: string; mode: string }[] }
