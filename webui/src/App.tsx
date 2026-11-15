import { lazy, Suspense } from 'react';
import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom';
import { Shell } from './layout/Shell';
import { MainView } from './views/main/MainView';

// MAIN (performance screen) loads first; editors are split into separate chunks.
const PianoView = lazy(() => import('./views/piano/PianoView').then(m => ({ default: m.PianoView })));
const SynthView = lazy(() => import('./views/synth/SynthView'));
const OrganView = lazy(() => import('./views/organ/OrganView'));
const FxView = lazy(() => import('./views/fx/FxView'));
const ZonesView = lazy(() => import('./views/zones/ZonesView'));
const LibraryView = lazy(() => import('./views/library/LibraryView'));
const SettingsView = lazy(() => import('./views/settings/SettingsView'));

const Loading = () => <div className="view-loading" aria-busy="true">CARGANDO…</div>;

export function App() {
  return (
    <BrowserRouter future={{ v7_startTransition: true, v7_relativeSplatPath: true }}>
      <Routes>
        <Route element={<Shell />}>
          <Route index element={<MainView />} />
          <Route path="piano" element={<Suspense fallback={<Loading />}><PianoView /></Suspense>} />
          <Route path="synth" element={<Suspense fallback={<Loading />}><SynthView /></Suspense>} />
          <Route path="organ" element={<Suspense fallback={<Loading />}><OrganView /></Suspense>} />
          <Route path="fx" element={<Suspense fallback={<Loading />}><FxView /></Suspense>} />
          <Route path="zones" element={<Suspense fallback={<Loading />}><ZonesView /></Suspense>} />
          <Route path="library" element={<Suspense fallback={<Loading />}><LibraryView /></Suspense>} />
          <Route path="settings" element={<Suspense fallback={<Loading />}><SettingsView /></Suspense>} />
          <Route path="library-manager" element={<Navigate to="/library" replace />} />
          <Route path="*" element={<Navigate to="/" replace />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
