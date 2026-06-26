export type ParamType = 'control' | 'protection';
export type ParamPermission = 'visible' | 'hidden';

export interface Parameter {
  address: number;
  name: string;
  defaultValue: number;
  type: ParamType;
  permission: ParamPermission;
}

export const PARAM_COUNT = 72;

export const createDefaultParameters = (): Parameter[] => {
  return Array.from({ length: PARAM_COUNT }, (_, index) => ({
    address: index,
    name: `参数 ${index}`,
    defaultValue: 0,
    type: index % 2 === 0 ? 'control' : 'protection',
    permission: index % 3 === 0 ? 'visible' : 'hidden',
  }));
};
