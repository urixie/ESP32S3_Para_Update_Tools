import React, { useState } from 'react';
import { BuilderPage } from './pages/BuilderPage';
import { ParserPage } from './pages/ParserPage';
import { AboutPage } from './pages/AboutPage';
import { createDefaultProject, Parameter, ProjectFile } from './types/parameter';

type View = 'builder' | 'parser' | 'about';

const App: React.FC = () => {
  const [view, setView] = useState<View>('builder');
  const [builderProject, setBuilderProject] = useState<ProjectFile>(createDefaultProject());
  const [builderNotice, setBuilderNotice] = useState<string | null>(null);

  const handleReuseParsedParameters = (boardName: string, parameters: Parameter[]) => {
    setBuilderProject((prev) => ({
      ...prev,
      boardName,
      parameters: parameters.map((param) => ({ ...param })),
    }));
    setBuilderNotice('已将解析结果复用到参数配置界面，可继续编辑或直接导出。');
    setView('builder');
  };

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
            <BuilderPage
              project={builderProject}
              setProject={setBuilderProject}
              importedNotice={builderNotice}
              onImportedNoticeShown={() => setBuilderNotice(null)}
            />
          ) : view === 'parser' ? (
            <ParserPage onReuseToBuilder={handleReuseParsedParameters} />
          ) : (
            <AboutPage />
          )}
        </main>
      </div>
    </div>
  );
};

export default App;
