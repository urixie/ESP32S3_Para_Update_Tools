import React from 'react';
import {
  Parameter,
  ParamPermission,
  ParamType,
  limitNameChars,
  paramTypeLabel,
  permissionLabel,
} from '../types/parameter';

interface ParamTableProps {
  parameters: Parameter[];
  onChange: (index: number, data: Partial<Parameter>) => void;
  highlightAddress?: number | null;
  readonly?: boolean;
}

const clampU16 = (v: number): number => {
  if (Number.isNaN(v)) return 0;
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return Math.floor(v);
};

export const ParamTable: React.FC<ParamTableProps> = ({
  parameters,
  onChange,
  highlightAddress,
  readonly = false,
}) => {
  // In readonly mode the caller must still pass an `onChange`, but we ignore
  // it. We assert against accidental state writes by wrapping the callback.
  const safeOnChange = readonly
    ? (_index: number, _data: Partial<Parameter>) => undefined
    : onChange;

  const fieldClass = readonly ? 'readonly-field' : '';

  return (
    <div className={`param-table-wrapper ${readonly ? 'readonly' : ''}`}>
      <table className="param-table">
        <colgroup>
          <col className="col-address" />
          <col className="col-name" />
          <col className="col-value" />
          <col className="col-type" />
          <col className="col-permission" />
        </colgroup>
        <thead>
          <tr>
            <th className="col-address">地址</th>
            <th className="col-name">名称</th>
            <th className="col-value">默认值</th>
            <th className="col-type">类型</th>
            <th className="col-permission">权限</th>
          </tr>
        </thead>
        <tbody>
          {parameters.map((param, index) => {
            const isHighlighted = highlightAddress === param.address;
            return (
              <tr key={param.address} className={isHighlighted ? 'highlight' : undefined}>
                <td className="addr-cell">
                  <span className="addr-badge">{param.address}</span>
                </td>
                <td className="name-cell">
                  <input
                    className={`name-input ${fieldClass}`}
                    value={param.name}
                    maxLength={30}
                    title="最多 30 个字符"
                    onChange={(e) => safeOnChange(index, { name: limitNameChars(e.target.value) })}
                    placeholder={readonly ? '' : `参数 ${param.address}`}
                    readOnly={readonly}
                  />
                  {!readonly && (
                    <span className="name-counter">
                      {Array.from(param.name).length}/30
                    </span>
                  )}
                </td>
                <td>
                  <input
                    className={`value-input ${fieldClass}`}
                    type="number"
                    min={0}
                    max={65535}
                    value={param.defaultValue}
                    onChange={(e) =>
                      safeOnChange(index, { defaultValue: clampU16(Number(e.target.value)) })
                    }
                    readOnly={readonly}
                    disabled={readonly}
                  />
                </td>
                <td>
                  <div className={`segmented-control type ${fieldClass}`}>
                    <button
                      type="button"
                      className={param.paramType === 'control' ? 'active' : ''}
                      onClick={() => safeOnChange(index, { paramType: 'control' as ParamType })}
                      disabled={readonly}
                    >
                      {paramTypeLabel('control')}
                    </button>
                    <button
                      type="button"
                      className={param.paramType === 'protection' ? 'active' : ''}
                      onClick={() => safeOnChange(index, { paramType: 'protection' as ParamType })}
                      disabled={readonly}
                    >
                      {paramTypeLabel('protection')}
                    </button>
                  </div>
                </td>
                <td>
                  <div className={`segmented-control permission ${fieldClass}`}>
                    <button
                      type="button"
                      className={
                        param.permission === 'visible'
                          ? 'active is-visible'
                          : ''
                      }
                      onClick={() =>
                        safeOnChange(index, { permission: 'visible' as ParamPermission })
                      }
                      disabled={readonly}
                    >
                      {permissionLabel('visible')}
                    </button>
                    <button
                      type="button"
                      className={
                        param.permission === 'hidden'
                          ? 'active is-hidden'
                          : ''
                      }
                      onClick={() =>
                        safeOnChange(index, { permission: 'hidden' as ParamPermission })
                      }
                      disabled={readonly}
                    >
                      {permissionLabel('hidden')}
                    </button>
                  </div>
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
};