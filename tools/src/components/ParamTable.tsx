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
  /**
   * Optional small heading rendered above the table — used by the two-column
   * layout on the builder / parser pages to label "0 ~ 35" vs "36 ~ 71".
   */
  title?: string;
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
  title,
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
      {title && <div className="param-table-title">{title}</div>}
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
                    <div className={`segmented-control type ${fieldClass}`}>
                      <button
                        type="button"
                        className={param.paramType === 'control' ? 'active' : ''}
                        onClick={() => emitChange(index, { paramType: 'control' as ParamType })}
                        disabled={readonly}
                      >
                        {paramTypeLabel('control')}
                      </button>
                      <button
                        type="button"
                        className={param.paramType === 'protection' ? 'active' : ''}
                        onClick={() => emitChange(index, { paramType: 'protection' as ParamType })}
                        disabled={readonly}
                      >
                        {paramTypeLabel('protection')}
                      </button>
                    </div>
                  </td>
                  <td className="permission-cell">
                    <div className={`segmented-control permission ${fieldClass}`}>
                      <button
                        type="button"
                        className={
                          param.permission === 'visible'
                            ? 'active is-visible'
                            : ''
                        }
                        onClick={() =>
                          emitChange(index, { permission: 'visible' as ParamPermission })
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
                          emitChange(index, { permission: 'hidden' as ParamPermission })
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
    </div>
  );
};
