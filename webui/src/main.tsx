import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { engine } from './store/engine';
import { applyTheme } from './store/theme';
import { installUiZoom } from './store/uiZoom';
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
installUiZoom(); // escala (nativa o CSS) antes del primer pintado
engine.start();
createRoot(document.getElementById('root')!).render(<StrictMode><App /></StrictMode>);

