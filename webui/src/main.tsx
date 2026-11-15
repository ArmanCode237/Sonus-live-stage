import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { engine } from './store/engine';
import { applyTheme } from './store/theme';
import './styles/theme.css';
import './styles/components.css';
import './styles/views.css';
import './styles/pro-console.css';
import './styles/perform-polish.css';
import './styles/nord-buttons.css';
import './styles/synth-workspace.css';

applyTheme(); // before the first paint: no flash of the wrong theme
engine.start();
createRoot(document.getElementById('root')!).render(<StrictMode><App /></StrictMode>);

