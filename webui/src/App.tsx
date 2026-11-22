import { lazy, Suspense, type ReactNode } from 'react';
import { BrowserRouter, Navigate, Route, Routes, useLocation } from 'react-router-dom';
import { Shell } from './layout/Shell';
import { MainView } from './views/main/MainView';
import { ViewBoundary } from './ui/ViewBoundary';

// MAIN (performance screen) loads first; editors are split into separate chunks.
const PianoView = lazy(() => import('./views/piano/PianoView').then(m => ({ default: m.PianoView })));
const SynthView = lazy(() => import('./views/synth/SynthView'));
const OrganView = lazy(() => import('./views/organ/OrganView'));
const FxView = lazy(() => import('./views/fx/FxView'));
const ZonesView = lazy(() => import('./views/zones/ZonesView'));
const LibraryView = lazy(() => import('./views/library/LibraryView'));
const SettingsView = lazy(() => import('./views/settings/SettingsView'));

const Loading = () => <div className="view-loading" aria-busy="true">CARGANDO…</div>;

/** Each view in its own error boundary (reset when the route changes). */
function Page({ children }: { children: ReactNode }) {
  const { pathname } = useLocation();
  return <ViewBoundary key={pathname}><Suspense fallback={<Loading />}>{children}</Suspense></ViewBoundary>;
}

export function App() {
  return (
    <BrowserRouter future={{ v7_startTransition: true, v7_relativeSplatPath: true }}>
      <Routes>
        <Route element={<Shell />}>
          <Route index element={<Page><MainView /></Page>} />
          <Route path="piano" element={<Page><PianoView /></Page>} />
          <Route path="synth" element={<Page><SynthView /></Page>} />
          <Route path="organ" element={<Page><OrganView /></Page>} />
          <Route path="fx" element={<Page><FxView /></Page>} />
          <Route path="zones" element={<Page><ZonesView /></Page>} />
          <Route path="library" element={<Page><LibraryView /></Page>} />
          <Route path="settings" element={<Page><SettingsView /></Page>} />
          <Route path="library-manager" element={<Navigate to="/library" replace />} />
          <Route path="*" element={<Navigate to="/" replace />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
