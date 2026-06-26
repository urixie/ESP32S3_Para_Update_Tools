import React, { useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { save as saveDialog, open as openDialog } from '@tauri-apps/plugin-dialog';
import {
  createDefaultProject,
  Parameter,
  ProjectFile,
  ValidationError,
  PARAM_COUNT,
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
    // Clear stale validation feedback when the user edits something.
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
      // Defensive: always keep 72 rows.
      if (!loaded.parameters || loaded.parameters.length !== PARAM_COUNT) {
        const fresh = (await invoke('new_project')) as ProjectFile;
        loaded.parameters = fresh.parameters;
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

  const handleValidate = async () => {
    try {
      setBusy(true);
      await invoke('validate_parameters_cmd', { params: project.parameters });
      setValidationErrors([]);
      setStatus({ kind: 'success', text: '校验通过，可以生成加密 bin 文件。' });
    } catch (e) {
      const errs = Array.isArray(e) ? (e as ValidationError[]) : [];
      setValidationErrors(errs);
      setStatus({ kind: 'error', text: `校验失败: ${errs.length} 项问题` });
    } finally {
      setBusy(false);
    }
  };

  const handleExportBin = async () => {
    try {
      setBusy(true);
      // Always re-validate before exporting — refuse to ship a broken file.
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
        params: project.parameters,
        productId: project.productId,
        keyId: project.keyId,
      });
      setStatus({ kind: 'success', text: `加密 bin 已生成: ${path}` });
    } catch (e) {
      setStatus({ kind: 'error', text: `导出失败: ${String(e)}` });
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="panel">
      <div className="panel-header">
        <div>
          <h2 className="section-title">参数构建</h2>
          <p className="section-subtitle">
            编辑 72 个固定参数，保存工程文件或生成加密 bin 文件。加密使用 AES-256-GCM。
          </p>
        </div>
      </div>

      <div className="meta-row">
        <label className="meta-field">
          <span>工程名称</span>
          <input
            value={project.projectName}
            onChange={(e) => handleMetaChange('projectName', e.target.value)}
          />
        </label>
        <label className="meta-field">
          <span>Product ID</span>
          <input
            type="number"
            min={1}
            value={project.productId}
            onChange={(e) => handleMetaChange('productId', Number(e.target.value) || 1)}
          />
        </label>
        <label className="meta-field">
          <span>Key ID</span>
          <input
            type="number"
            min={1}
            value={project.keyId}
            onChange={(e) => handleMetaChange('keyId', Number(e.target.value) || 1)}
          />
        </label>
        <label className="meta-field meta-field-wide">
          <span>说明</span>
          <input
            value={project.description}
            onChange={(e) => handleMetaChange('description', e.target.value)}
            placeholder="工程描述（可选）"
          />
        </label>
      </div>

      <div className="action-row">
        <button onClick={handleNewProject} disabled={busy}>新建工程</button>
        <button onClick={handleSaveProject} disabled={busy}>保存工程文件</button>
        <button onClick={handleLoadProject} disabled={busy}>加载工程文件</button>
        <button className="primary" onClick={handleValidate} disabled={busy}>一键校验</button>
        <button className="primary" onClick={handleExportBin} disabled={busy}>生成加密 bin</button>
      </div>

      {status && (
        <div className={`status-card status-${status.kind}`}>{status.text}</div>
      )}

      {validationErrors.length > 0 && (
        <div className="error-panel">
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

      <div className="param-two-column">
        <ParamTable
          title="参数 0 ~ 35"
          parameters={project.parameters.slice(0, 36)}
          indexOffset={0}
          onChange={handleParamChange}
        />
        <ParamTable
          title="参数 36 ~ 71"
          parameters={project.parameters.slice(36, 72)}
          indexOffset={36}
          onChange={handleParamChange}
        />
      </div>
    </section>
  );
};