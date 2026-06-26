import React, { useState } from 'react';
import { createDefaultParameters, Parameter } from '../types/parameter';
import { ParamTable } from '../components/ParamTable';

export const BuilderPage: React.FC = () => {
  const [parameters, setParameters] = useState<Parameter[]>(createDefaultParameters());

  const handleParamChange = (index: number, data: Partial<Parameter>) => {
    setParameters((prev) => {
      const next = [...prev];
      next[index] = { ...next[index], ...data };
      return next;
    });
  };

  return (
    <section className="panel">
      <h2 className="section-title">参数构建</h2>
      <p>编辑参数后，可保存工程文件或生成加密 bin 文件。</p>
      <ParamTable parameters={parameters} onChange={handleParamChange} />
      <div className="action-row">
        <button disabled>保存工程文件</button>
        <button disabled>生成加密 bin</button>
      </div>
    </section>
  );
};
