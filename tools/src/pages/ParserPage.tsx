import React, { useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { open as openDialog } from '@tauri-apps/plugin-dialog';
import {
  BinHeaderInfo,
  Parameter,
  ParsedBinInfo,
} from '../types/parameter';
import { ParamTable } from '../components/ParamTable';

interface StatusMessage {
  kind: 'info' | 'success' | 'error';
  text: string;
}

export const ParserPage: React.FC = () => {
  const [status, setStatus] = useState<StatusMessage>({
    kind: 'info',
    text: '请选择由本工具生成的加密 bin 文件进行解析。',
  });
  const [header, setHeader] = useState<BinHeaderInfo | null>(null);
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
      setStatus({ kind: 'info', text: `正在解析: ${path}` });

      const parsed = (await invoke('parse_encrypted_bin_cmd', { path })) as ParsedBinInfo;
      setHeader(parsed.header);
      setParameters(parsed.parameters);
      setStatus({
        kind: 'success',
        text: `解析成功，共 ${parsed.parameters.length} 个参数。`,
      });
    } catch (e) {
      setHeader(null);
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
    setHeader(null);
    setParameters([]);
    setFilePath('');
    setStatus({ kind: 'info', text: '已清空解析结果。' });
  };

  return (
    <section className="panel page-panel">
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

          {filePath && (
            <div className="side-panel-section">
              <div className="side-panel-title">当前文件</div>
              <div className="file-path-box" title={filePath}>
                {filePath}
              </div>
            </div>
          )}

          <div className={`status-card side-status status-${status.kind}`}>
            {status.text}
          </div>
        </aside>

        <div className="page-main-panel">
          {header && (
            <div className="header-card">
              <div className="header-card-title">Header 信息</div>
              <div className="header-grid">
                <Field label="Magic" value={'"UEPB"'} />
                <Field label="Header Len" value={`${header.headerLen} 字节`} />
                <Field label="Format Version" value={`${header.formatVersion}`} />
                <Field label="Crypto Algo" value={`${header.cryptoAlgo} (AES-256-GCM)`} />
                <Field label="Param Count" value={`${header.paramCount}`} />
                <Field label="Addr Range" value={`${header.addrMin} ~ ${header.addrMax}`} />
                <Field label="Product ID" value={`${header.productId}`} />
                <Field label="Key ID" value={`${header.keyId}`} />
                <Field label="Flags" value={`${header.flags}`} />
                <Field label="Nonce" value={header.nonceHex} mono />
                <Field label="Payload Len" value={`${header.payloadLen} 字节`} />
                <Field label="Tag Len" value={`${header.tagLen} 字节`} />
                <Field label="File Size" value={`${header.fileSize} 字节`} />
              </div>
            </div>
          )}

          {parameters.length > 0 && (
            <>
              <div className="sub-section-title">
                参数表 ({parameters.length}) ·{' '}
                <span className="muted">
                  控制: {parameters.filter((p) => p.paramType === 'control').length} / 保护:{' '}
                  {parameters.filter((p) => p.paramType === 'protection').length} · 可见:{' '}
                  {parameters.filter((p) => p.permission === 'visible').length} / 隐藏:{' '}
                  {parameters.filter((p) => p.permission === 'hidden').length}
                </span>
              </div>

              <div className="param-two-column">
                <ParamTable
                  title="参数 0 ~ 35"
                  parameters={parameters.slice(0, 36)}
                  indexOffset={0}
                  readonly
                  onChange={() => undefined}
                />
                <ParamTable
                  title="参数 36 ~ 71"
                  parameters={parameters.slice(36, 72)}
                  indexOffset={36}
                  readonly
                  onChange={() => undefined}
                />
              </div>

              <details className="raw-card">
                <summary>查看原始参数 JSON</summary>
                <pre>{JSON.stringify(parameters, null, 2)}</pre>
              </details>
            </>
          )}

          {!header && parameters.length === 0 && (
            <div className="empty-state">
              请在左侧选择加密 bin 文件进行解析
            </div>
          )}
        </div>
      </div>
    </section>
  );
};

interface FieldProps {
  label: string;
  value: string;
  mono?: boolean;
}

const Field: React.FC<FieldProps> = ({ label, value, mono }) => (
  <div className="header-field">
    <span className="header-field-label">{label}</span>
    <span className={`header-field-value ${mono ? 'mono' : ''}`}>{value}</span>
  </div>
);
