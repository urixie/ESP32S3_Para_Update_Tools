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
   * on a different slice of the same underlying parameter array, without
   * confusing the global index.
   *
   * Defaults to 0 so a single-table caller (or the parser page) doesn't have
   * to pass anything explicitly.
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
  // In readonly mode the caller must still pass an `onChange`, but we ignore
  // it. We assert against accidental state writes by wrapping the callback.
  const safeOnChange = readonly
    ? (_index: number, _data: Partial<Parameter>) => undefined
    : onChange;

  // Translate a local (in-table) row index back to the global parameter
  // array index. With two tables in a row, the right one is given
  // indexOffset={36} so its row 0 maps to parameter[36], row 1 to [37], etc.
  const emitChange = (localIndex: number, data: Partial<Parameter>) => {
    safeOnChange(indexOffset + localIndex, data);
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
                    <div className={`checkbox-group type ${fieldClass}`}>
                      <label
                        className={`checkbox-option ${param.paramType === 'control' ? 'is-checked' : ''}`}
                      >
                        <input
                          type="checkbox"
                          checked={param.paramType === 'control'}
                          // `readOnly` keeps the HTML checkbox non-mutable per
                          // spec so the browser will not toggle the underlying
                          // `checked` DOM property on click. We then drive the
                          // state purely from React + onClick below — no more
                          // flicker where both rows look ticked before React
                          // re-renders, and no more trying to read the
                          // post-toggle DOM state in the click handler.
                          readOnly={!readonly}
                          disabled={readonly}
                          onClick={() => {
                            if (param.paramType !== 'control') {
                              emitChange(index, {
                                paramType: 'control' as ParamType,
                              });
                            }
                          }}
                          onChange={() => {
                            // Safety net for the rare browser that still
                            // toggles `checked` despite `readOnly`. Mirror the
                            // desired state so DOM and React stay in sync.
                            if (param.paramType !== 'control') {
                              emitChange(index, {
                                paramType: 'control' as ParamType,
                              });
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
                          readOnly={!readonly}
                          disabled={readonly}
                          onClick={() => {
                            if (param.paramType !== 'protection') {
                              emitChange(index, {
                                paramType: 'protection' as ParamType,
                              });
                            }
                          }}
                          onChange={() => {
                            if (param.paramType !== 'protection') {
                              emitChange(index, {
                                paramType: 'protection' as ParamType,
                              });
                            }
                          }}
                        />
                        <span>{paramTypeLabel('protection')}</span>
                      </label>
                    </div>
                  </td>
                  <td className="permission-cell">
                    <div className={`checkbox-group permission ${fieldClass}`}>
                      <label
                        className={`checkbox-option is-visible ${param.permission === 'visible' ? 'is-checked' : ''}`}
                      >
                        <input
                          type="checkbox"
                          checked={param.permission === 'visible'}
                          readOnly={!readonly}
                          disabled={readonly}
                          onClick={() => {
                            if (param.permission !== 'visible') {
                              emitChange(index, {
                                permission: 'visible' as ParamPermission,
                              });
                            }
                          }}
                          onChange={() => {
                            if (param.permission !== 'visible') {
                              emitChange(index, {
                                permission: 'visible' as ParamPermission,
                              });
                            }
                          }}
                        />
                        <span>{permissionLabel('visible')}</span>
                      </label>
                      <label
                        className={`checkbox-option is-hidden ${param.permission === 'hidden' ? 'is-checked' : ''}`}
                      >
                        <input
                          type="checkbox"
                          checked={param.permission === 'hidden'}
                          readOnly={!readonly}
                          disabled={readonly}
                          onClick={() => {
                            if (param.permission !== 'hidden') {
                              emitChange(index, {
                                permission: 'hidden' as ParamPermission,
                              });
                            }
                          }}
                          onChange={() => {
                            if (param.permission !== 'hidden') {
                              emitChange(index, {
                                permission: 'hidden' as ParamPermission,
                              });
                            }
                          }}
                        />
                        <span>{permissionLabel('hidden')}</span>
                      </label>
                    </div>
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
