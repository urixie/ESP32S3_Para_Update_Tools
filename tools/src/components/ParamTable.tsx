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
  /**
   * Offset added to the row's local index when reporting a change back via
   * `onChange`. Lets one page render two tables side by side, each operating
   * on a different slice of the same underlying parameter array.
   */
  indexOffset?: number;
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
  indexOffset = 0,
}) => {
  const emitChange = (localIndex: number, data: Partial<Parameter>) => {
    if (readonly) return;
    onChange(indexOffset + localIndex, data);
  };

  const fieldClass = readonly ? 'readonly-field' : '';

  return (
    <div className={`param-table-card ${readonly ? 'readonly' : ''}`}>
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
                      onChange={(e) =>
                        emitChange(index, { name: limitNameChars(e.target.value) })
                      }
                      placeholder={readonly ? '' : `参数 ${param.address}`}
                      readOnly={readonly}
                    />
                  </td>

                  <td className="value-cell">
                    <input
                      className={`value-input compact-number ${fieldClass}`}
                      type="number"
                      min={0}
                      max={65535}
                      value={param.defaultValue}
                      onChange={(e) =>
                        emitChange(index, { defaultValue: clampU16(Number(e.target.value)) })
                      }
                      readOnly={readonly}
                      disabled={readonly}
                    />
                  </td>

                  <td className="type-cell">
                    {readonly ? (
                      <span className={`readonly-tag type-tag ${param.paramType}`}>
                        {paramTypeLabel(param.paramType)}
                      </span>
                    ) : (
                      <div className="checkbox-group type">
                        <label
                          className={`checkbox-option ${param.paramType === 'control' ? 'is-checked' : ''}`}
                        >
                          <input
                            type="checkbox"
                            checked={param.paramType === 'control'}
                            readOnly
                            onClick={() => {
                              if (param.paramType !== 'control') {
                                emitChange(index, { paramType: 'control' as ParamType });
                              }
                            }}
                            onChange={() => {
                              if (param.paramType !== 'control') {
                                emitChange(index, { paramType: 'control' as ParamType });
                              }
                            }}
                          />
                          <span>{paramTypeLabel('control')}</span>
                        </label>

                        <label
                          className={`checkbox-option ${param.paramType === 'protection' ? 'is-checked' : ''}`}
                        >
                          <input
                            type="checkbox"
                            checked={param.paramType === 'protection'}
                            readOnly
                            onClick={() => {
                              if (param.paramType !== 'protection') {
                                emitChange(index, { paramType: 'protection' as ParamType });
                              }
                            }}
                            onChange={() => {
                              if (param.paramType !== 'protection') {
                                emitChange(index, { paramType: 'protection' as ParamType });
                              }
                            }}
                          />
                          <span>{paramTypeLabel('protection')}</span>
                        </label>
                      </div>
                    )}
                  </td>

                  <td className="permission-cell">
                    {readonly ? (
                      <span className={`readonly-tag permission-tag ${param.permission}`}>
                        {permissionLabel(param.permission)}
                      </span>
                    ) : (
                      <label
                        className={`checkbox-option permission-toggle ${param.permission === 'visible' ? 'is-checked' : ''}`}
                      >
                        <input
                          type="checkbox"
                          checked={param.permission === 'visible'}
                          readOnly
                          onClick={() => {
                            emitChange(index, {
                              permission: (param.permission === 'visible'
                                ? 'hidden'
                                : 'visible') as ParamPermission,
                            });
                          }}
                          onChange={() => {
                            emitChange(index, {
                              permission: (param.permission === 'visible'
                                ? 'hidden'
                                : 'visible') as ParamPermission,
                            });
                          }}
                        />
                        <span>{permissionLabel(param.permission)}</span>
                      </label>
                    )}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
};
