import React, { useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { open as openDialog } from '@tauri-apps/plugin-dialog';
import {
  Parameter,
  ParsedBinInfo,
} from '../types/parameter';
import { ParamTable } from '../components/ParamTable';

interface StatusMessage {
  kind: 'info' | 'error';
  text: string;
}

export const ParserPage: React.FC = () => {
  const [status, setStatus] = useState<StatusMessage | null>({
    kind: 'info',
    text: '请选择由本工具生成的加密 bin 文件进行解析。',
  });
  const [boardName, setBoardName] = useState<string>('');
  const [parameters, setParameters] = useState<Parameter[]>([]);
  const [filePath, setFilePath] = useState<string>('');
  const [busy, setBusy] = useState(false);

  const handleSelectBin = async () => {
    try {
      setBusy(true);
      const path = await openDialog({
        title: '选择加密 bin 文件',
        multiple: false,
        filters: [{ name: 'Encrypted Param Bin', extensions: ['bin'] }],
      });
      if (!path || Array.isArray(path)) {
        setStatus({ kind: 'info', text: '已取消选择。' });
        return;
      }

      setFilePath(path);
      setBoardName('');
      setParameters([]);
      setStatus({ kind: 'info', text: `正在解析: ${path}` });

      const parsed = (await invoke('parse_encrypted_bin_cmd', { path })) as ParsedBinInfo;
      setBoardName(parsed.boardName || '未命名板卡');
      setParameters(parsed.parameters);
      setStatus(null);
    } catch (e) {
      setBoardName('');
      setParameters([]);
      setStatus({
        kind: 'error',
        text: `解析失败: 文件被篡改、格式错误或密钥错误 (${String(e)})`,
      });
    } finally {
      setBusy(false);
    }
  };

  const handleClear = () => {
    setBoardName('');
    setParameters([]);
    setFilePath('');
    setStatus({ kind: 'info', text: '已清空解析结果。' });
  };

  return (
    <section className="panel page-panel parser-panel">
      <div className="page-workspace">
        <aside className="page-side-panel parser-side-panel">
          <div className="side-panel-section">
            <div className="side-panel-title">解析操作</div>
            <div className="side-action-list">
              <button className="primary" onClick={handleSelectBin} disabled={busy}>
                选择 bin 文件
              </button>
              <button onClick={handleClear} disabled={busy}>
                清空结果
              </button>
            </div>
          </div>

          {boardName && (
            <div className="side-panel-section">
              <div className="side-panel-title">板卡名称</div>
              <div className="file-path-box" title={boardName}>
                {boardName}
              </div>
            </div>
          )}

          {filePath && (
            <div className="side-panel-section">
              <div className="side-panel-title">当前文件</div>
              <div className="file-path-box" title={filePath}>
                {filePath}
              </div>
            </div>
          )}

          {status && (
            <div className={`status-card side-status status-${status.kind}`}>
              {status.text}
            </div>
          )}
        </aside>

        <div className="page-main-panel parser-main-panel">
          {boardName && <div className="page-title-inline">{boardName}</div>}

          {parameters.length > 0 && (
            <div className="param-two-column">
              <ParamTable
                parameters={parameters.slice(0, 36)}
                indexOffset={0}
                readonly
                onChange={() => undefined}
              />
              <ParamTable
                parameters={parameters.slice(36, 72)}
                indexOffset={36}
                readonly
                onChange={() => undefined}
              />
            </div>
          )}

          {parameters.length === 0 && (
            <div className="empty-state">
              请在左侧选择加密 bin 文件进行解析
            </div>
          )}
        </div>
      </div>
    </section>
  );
};
