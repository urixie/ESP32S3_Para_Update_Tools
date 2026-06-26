import React from 'react';
import { Parameter } from '../types/parameter';

interface ParamTableProps {
  parameters: Parameter[];
  onChange: (index: number, data: Partial<Parameter>) => void;
}

export const ParamTable: React.FC<ParamTableProps> = ({ parameters, onChange }) => {
  return (
    <table className="param-table">
      <thead>
        <tr>
          <th>地址</th>
          <th>名称</th>
          <th>默认值</th>
          <th>类型</th>
          <th>权限</th>
        </tr>
      </thead>
      <tbody>
        {parameters.map((param, index) => (
          <tr key={param.address}>
            <td>{param.address}</td>
            <td>
              <input
                value={param.name}
                onChange={(e) => onChange(index, { name: e.target.value })}
              />
            </td>
            <td>
              <input
                type="number"
                min={0}
                max={65535}
                value={param.defaultValue}
                onChange={(e) => onChange(index, { defaultValue: Number(e.target.value) })}
              />
            </td>
            <td>
              <select
                value={param.type}
                onChange={(e) => onChange(index, { type: e.target.value as Parameter['type'] })}
              >
                <option value="control">控制</option>
                <option value="protection">保护</option>
              </select>
            </td>
            <td>
              <select
                value={param.permission}
                onChange={(e) => onChange(index, { permission: e.target.value as Parameter['permission'] })}
              >
                <option value="visible">可见</option>
                <option value="hidden">隐藏</option>
              </select>
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
};
