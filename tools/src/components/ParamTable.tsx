import React from 'react';
import { Parameter, ParamType, ParamPermission, paramTypeLabel, permissionLabel } from '../types/parameter';

interface ParamTableProps {
  parameters: Parameter[];
  onChange: (index: number, data: Partial<Parameter>) => void;
  highlightAddress?: number | null;
}

const clampU16 = (v: number): number => {
  if (Number.isNaN(v)) return 0;
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return Math.floor(v);
};

export const ParamTable: React.FC<ParamTableProps> = ({ parameters, onChange, highlightAddress }) => {
  return (
    <div className="param-table-wrapper">
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
                    className="name-input"
                    value={param.name}
                    onChange={(e) => onChange(index, { name: e.target.value })}
                    placeholder={`参数 ${param.address}`}
                  />
                </td>
                <td>
                  <input
                    className="value-input"
                    type="number"
                    min={0}
                    max={65535}
                    value={param.defaultValue}
                    onChange={(e) => onChange(index, { defaultValue: clampU16(Number(e.target.value)) })}
                  />
                </td>
                <td>
                  <select
                    className="type-select"
                    value={param.paramType}
                    onChange={(e) => onChange(index, { paramType: e.target.value as ParamType })}
                  >
                    <option value="control">{paramTypeLabel('control')}</option>
                    <option value="protection">{paramTypeLabel('protection')}</option>
                  </select>
                </td>
                <td>
                  <select
                    className="type-select"
                    value={param.permission}
                    onChange={(e) => onChange(index, { permission: e.target.value as ParamPermission })}
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