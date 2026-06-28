import React, { useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { save as saveDialog, open as openDialog } from '@tauri-apps/plugin-dialog';
import {
  createDefaultProject,
  Parameter,
  ProjectFile,
  ValidationError,
  PARAM_COUNT,
  BOARD_NAME_MAX_CHARS,
  limitBoardNameChars,
} from '../types/parameter';
import { ParamTable } from '../components/ParamTable';

interface StatusMessage {
  kind: 'info' | 'success' | 'error';
  text: string;
}

export const BuilderPage: React.FC = () => {
  const [project, setProject] = useState<ProjectFile>(createDefaultProject());
  const [status, setStatus] = useState<StatusMessage | null>(null);
  const [validationErrors, setValidationErrors] = useState<ValidationError[]>([]);
  const [busy, setBusy] = useState(false);

  const handleParamChange = (index: number, data: Partial<Parameter>) => {
    setProject((prev) => {
      const nextParams = [...prev.parameters];
      nextParams[index] = { ...nextParams[index], ...data };
      return { ...prev, parameters: nextParams };
    });

    if (validationErrors.length > 0) {
      setValidationErrors([]);
    }
  };

  const handleMetaChange = (field: keyof ProjectFile, value: string | number) => {
    setProject((prev) => ({ ...prev, [field]: value as never }));
  };

  const handleNewProject = () => {
    setProject(createDefaultProject());
    setValidationErrors([]);
    setStatus({ kind: 'info', text: '已新建默认工程（72 个参数）。' });
  };

  const handleSaveProject = async () => {
    try {
      setBusy(true);
      const path = await saveDialog({
        title: '保存工程文件',
        defaultPath: `${project.projectName || 'project'}.ueproj.json`,
        filters: [{ name: 'Param Project', extensions: ['json'] }],
      });
      if (!path) {
        setStatus({ kind: 'info', text: '已取消保存。' });
        return;
      }
      await invoke('save_project_file_cmd', { path, project });
      setStatus({ kind: 'success', text: `工程已保存到 ${path}` });
    } catch (e) {
      setStatus({ kind: 'error', text: `保存失败: ${String(e)}` });
    } finally {
      setBusy(false);
    }
  };

  const handleLoadProject = async () => {
    try {
      setBusy(true);
      const path = await openDialog({
        title: '打开工程文件',
        multiple: false,
        filters: [{ name: 'Param Project', extensions: ['json'] }],
      });
      if (!path || Array.isArray(path)) {
        setStatus({ kind: 'info', text: '已取消加载。' });
        return;
      }
      const loaded = (await invoke('load_project_file_cmd', { path })) as ProjectFile;
      if (!loaded.parameters || loaded.parameters.length !== PARAM_COUNT) {
        const fresh = (await invoke('new_project')) as ProjectFile;
        loaded.parameters = fresh.parameters;
      }
      if (!loaded.boardName) {
        loaded.boardName = '默认板卡';
      }
      setProject(loaded);
      setValidationErrors([]);
      setStatus({ kind: 'success', text: `工程已加载: ${path}` });
    } catch (e) {
      setStatus({ kind: 'error', text: `加载失败: ${String(e)}` });
    } finally {
      setBusy(false);
    }
  };

  const handleExportBin = async () => {
    try {
      setBusy(true);

      const boardName = project.boardName.trim();
      if (!boardName) {
        setStatus({ kind: 'error', text: '请先输入板卡名称。' });
        return;
      }

      try {
        await invoke('validate_parameters_cmd', { params: project.parameters });
        setValidationErrors([]);
      } catch (e) {
        const errs = Array.isArray(e) ? (e as ValidationError[]) : [];
        setValidationErrors(errs);
        setStatus({
          kind: 'error',
          text: `参数未通过校验，请先修复 ${errs.length} 项问题`,
        });
        return;
      }

      const path = await saveDialog({
        title: '导出加密 bin 文件',
        defaultPath: `${project.projectName || 'params'}.bin`,
        filters: [{ name: 'Encrypted Param Bin', extensions: ['bin'] }],
      });
      if (!path) {
        setStatus({ kind: 'info', text: '已取消导出。' });
        return;
      }

      await invoke('export_encrypted_bin_cmd', {
        path,
        boardName,
        params: project.parameters,
      });
      setStatus({ kind: 'success', text: `加密 bin 已生成: ${path}` });
    } catch (e) {
      setStatus({ kind: 'error', text: `导出失败: ${String(e)}` });
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="panel page-panel builder-panel">
      <div className="page-workspace">
        <aside className="page-side-panel builder-side-panel">
          <div className="side-panel-section">
            <div className="side-panel-title">工程信息</div>
            <label className="side-field">
              <span>工程名称</span>
              <input
                value={project.projectName}
                onChange={(e) => handleMetaChange('projectName', e.target.value)}
              />
            </label>
            <label className="side-field">
              <span>板卡名称</span>
              <input
                value={project.boardName}
                maxLength={BOARD_NAME_MAX_CHARS}
                onChange={(e) => handleMetaChange('boardName', limitBoardNameChars(e.target.value))}
                placeholder="请输入中文板卡名称"
                title={`最多 ${BOARD_NAME_MAX_CHARS} 个字符，写入加密 bin Payload`}
              />
            </label>
            <label className="side-field">
              <span>说明</span>
              <textarea
                value={project.description}
                onChange={(e) => handleMetaChange('description', e.target.value)}
                placeholder="工程描述（可选）"
                rows={5}
              />
            </label>
          </div>

          <div className="side-panel-section action-section">
            <div className="side-panel-title">工程操作</div>
            <div className="side-action-list secondary-actions">
              <button onClick={handleNewProject} disabled={busy}>新建工程</button>
              <button onClick={handleSaveProject} disabled={busy}>保存工程文件</button>
              <button onClick={handleLoadProject} disabled={busy}>加载工程文件</button>
            </div>
          </div>

          <div className="side-panel-section action-section">
            <div className="side-panel-title">导出</div>
            <div className="side-action-list export-actions">
              <button className="primary primary-main" onClick={handleExportBin} disabled={busy}>
                生成加密 bin
              </button>
            </div>
          </div>

          {status && (
            <div className={`status-card side-status status-${status.kind}`}>
              {status.text}
            </div>
          )}

          {validationErrors.length > 0 && (
            <div className="error-panel side-error-panel">
              <div className="error-panel-title">校验问题 ({validationErrors.length})</div>
              <ul className="error-list">
                {validationErrors.map((err, i) => (
                  <li key={i}>
                    {err.address !== undefined && (
                      <span className="error-addr">addr {err.address}</span>
                    )}
                    {err.field && <span className="error-field">{err.field}</span>}
                    <span className="error-message">{err.message}</span>
                  </li>
                ))}
              </ul>
            </div>
          )}
        </aside>

        <div className="page-main-panel">
          <div className="param-two-column">
            <ParamTable
              parameters={project.parameters.slice(0, 36)}
              indexOffset={0}
              onChange={handleParamChange}
            />
            <ParamTable
              parameters={project.parameters.slice(36, 72)}
              indexOffset={36}
              onChange={handleParamChange}
            />
          </div>
        </div>
      </div>
    </section>
  );
};
