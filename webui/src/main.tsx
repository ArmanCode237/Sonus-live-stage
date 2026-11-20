import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { engine } from './store/engine';
import { applyTheme } from './store/theme';
import { getUiZoom } from './store/uiZoom';
import './styles/theme.css';
import './styles/components.css';
import './styles/views.css';
import './styles/pro-console.css';
import './styles/perform-polish.css';
import './styles/stage-buttons.css';
import './styles/synth-workspace.css';
import './browser/browser.css';
import './styles/viewport-guard.css';

applyTheme();
getUiZoom(); // aplica el zoom guardado antes de pintar la interfaz // before the first paint: no flash of the wrong theme
engine.start();
createRoot(document.getElementById('root')!).render(<StrictMode><App /></StrictMode>);

