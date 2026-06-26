import React, { useState } from 'react';
import { BuilderPage } from './pages/BuilderPage';
import { ParserPage } from './pages/ParserPage';

type View = 'builder' | 'parser';

const App: React.FC = () => {
  const [view, setView] = useState<View>('builder');

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
        <nav className="nav-buttons">
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
        </nav>
      </header>
      <main className="main-content">
        {view === 'builder' ? <BuilderPage /> : <ParserPage />}
      </main>
      <footer className="bottom-bar">
        <span>UEPB Format · v1 · AES-256-GCM · 72 参数 · 地址 0~71</span>
      </footer>
    </div>
  );
};

export default App;