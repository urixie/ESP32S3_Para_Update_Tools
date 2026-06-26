import React, { useState } from 'react';
import { ParamTable } from './components/ParamTable';
import { BuilderPage } from './pages/BuilderPage';
import { ParserPage } from './pages/ParserPage';

const App: React.FC = () => {
  const [view, setView] = useState<'builder' | 'parser'>('builder');

  return (
    <div className="app-shell">
      <header className="top-bar">
        <div className="brand">Param Bin Tool</div>
        <div className="nav-buttons">
          <button className={view === 'builder' ? 'active' : ''} onClick={() => setView('builder')}>
            参数构建
          </button>
          <button className={view === 'parser' ? 'active' : ''} onClick={() => setView('parser')}>
            参数解析
          </button>
        </div>
      </header>
      <main className="main-content">
        {view === 'builder' ? <BuilderPage /> : <ParserPage />}
      </main>
    </div>
  );
};

export default App;
