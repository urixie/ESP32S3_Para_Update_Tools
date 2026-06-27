import React, { useState } from 'react';
import { BuilderPage } from './pages/BuilderPage';
import { ParserPage } from './pages/ParserPage';
import { AboutDialog } from './components/AboutDialog';

type View = 'builder' | 'parser';

const App: React.FC = () => {
  const [view, setView] = useState<View>('builder');
  const [aboutOpen, setAboutOpen] = useState(false);

  return (
    <div className="app-shell">
      <header className="top-bar">
        <div className="brand-block">
          <span className="brand-dot" />
          <div className="brand-text">
            <div className="brand">Param Bin Tool</div>
            <div className="brand-sub">ESP32 参数加密 bin 上位机 · AES-256-GCM</div>
          </div>
        </div>
        <button
          className="about-button"
          onClick={() => setAboutOpen(true)}
        >
          关于
        </button>
      </header>

      <div className="app-body">
        <aside className="side-nav">
          <button
            className={view === 'builder' ? 'active' : ''}
            onClick={() => setView('builder')}
          >
            参数构建
          </button>

          <button
            className={view === 'parser' ? 'active' : ''}
            onClick={() => setView('parser')}
          >
            参数解析
          </button>
        </aside>

        <main className="main-content">
          {view === 'builder' ? <BuilderPage /> : <ParserPage />}
        </main>
      </div>

      {aboutOpen && <AboutDialog onClose={() => setAboutOpen(false)} />}
    </div>
  );
};

export default App;
