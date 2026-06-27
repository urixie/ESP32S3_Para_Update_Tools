import React, { useState } from 'react';
import { BuilderPage } from './pages/BuilderPage';
import { ParserPage } from './pages/ParserPage';
import { AboutPage } from './pages/AboutPage';

type View = 'builder' | 'parser' | 'about';

const App: React.FC = () => {
  const [view, setView] = useState<View>('builder');

  return (
    <div className="app-shell">
      <header className="top-bar" />

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

          <button
            className={view === 'about' ? 'active' : ''}
            onClick={() => setView('about')}
          >
            关于
          </button>
        </aside>

        <main className="main-content">
          {view === 'builder' ? (
            <BuilderPage />
          ) : view === 'parser' ? (
            <ParserPage />
          ) : (
            <AboutPage />
          )}
        </main>
      </div>
    </div>
  );
};

export default App;
