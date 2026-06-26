import React, { useState } from 'react';
import { Parameter } from '../types/parameter';

export const ParserPage: React.FC = () => {
  const [message, setMessage] = useState('请选择一个加密 bin 文件进行解析。');
  const [parameters, setParameters] = useState<Parameter[]>([]);

  return (
    <section className="panel">
      <h2 className="section-title">参数解析</h2>
      <p>{message}</p>
      <div className="action-row">
        <button disabled>选择 bin 文件</button>
      </div>
      {parameters.length > 0 && (
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
            {parameters.map((param) => (
              <tr key={param.address}>
                <td>{param.address}</td>
                <td>{param.name}</td>
                <td>{param.defaultValue}</td>
                <td>{param.type}</td>
                <td>{param.permission}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </section>
  );
};
