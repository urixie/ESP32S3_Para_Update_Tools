import React from 'react';
import { Parameter, ParamType, ParamPermission, paramTypeLabel, permissionLabel } from '../types/parameter';

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

export const ParamTable: React.FC<ParamTableProps> = ({ parameters, onChange, highlightAddress, readonly = false }) => {
  // In readonly mode the caller must still pass an `onChange`, but we ignore
  // it. We assert against accidental state writes by wrapping the callback.
  const safeOnChange = readonly
    ? (_index: number, _data: Partial<Parameter>) => undefined
    : onChange;

  const fieldClass = readonly ? 'readonly-field' : '';

  return (
    <div className={`param-table-wrapper ${readonly ? 'readonly' : ''}`}>
      <table className="param-table">
        <thead>
          <tr>
            <th style={{ width: 64 }}>地址</th>
            <th>名称</th>
            <th style={{ width: 120 }}>默认值</th>
            <th style={{ width: 100 }}>类型</th>
            <th style={{ width: 100 }}>权限</th>
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
                <td>
                  <input
                    className={`name-input ${fieldClass}`}
                    value={param.name}
                    onChange={(e) => safeOnChange(index, { name: e.target.value })}
                    placeholder={readonly ? '' : `参数 ${param.address}`}
                    readOnly={readonly}
                  />
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
                  <select
                    className={`type-select ${fieldClass}`}
                    value={param.paramType}
                    onChange={(e) => safeOnChange(index, { paramType: e.target.value as ParamType })}
                    disabled={readonly}
                  >
                    <option value="control">{paramTypeLabel('control')}</option>
                    <option value="protection">{paramTypeLabel('protection')}</option>
                  </select>
                </td>
                <td>
                  <select
                    className={`type-select ${fieldClass}`}
                    value={param.permission}
                    onChange={(e) => safeOnChange(index, { permission: e.target.value as ParamPermission })}
                    disabled={readonly}
                  >
                    <option value="visible">{permissionLabel('visible')}</option>
                    <option value="hidden">{permissionLabel('hidden')}</option>
                  </select>
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
};