export type ParamType = 'control' | 'protection';
export type ParamPermission = 'visible' | 'hidden';

export interface Parameter {
  address: number;
  name: string;
  defaultValue: number;
  paramType: ParamType;
  permission: ParamPermission;
}

export interface ProjectFile {
  projectName: string;
  formatVersion: number;
  productId: number;
  keyId: number;
  description: string;
  parameters: Parameter[];
}

export interface ValidationError {
  address?: number;
  field?: string;
  message: string;
}

export interface BinHeaderInfo {
  headerLen: number;
  formatVersion: number;
  cryptoAlgo: number;
  paramCount: number;
  addrMin: number;
  addrMax: number;
  productId: number;
  keyId: number;
  flags: number;
  nonceHex: string;
  payloadLen: number;
  tagLen: number;
  fileSize: number;
}

export interface ParsedBinInfo {
  header: BinHeaderInfo;
  parameters: Parameter[];
}

export const PARAM_COUNT = 72;
export const ADDR_MIN = 0;
export const ADDR_MAX = 71;
export const NAME_MAX_BYTES = 96;

export const createDefaultParameters = (): Parameter[] => {
  return Array.from({ length: PARAM_COUNT }, (_, index) => ({
    address: index,
    name: `参数 ${index}`,
    defaultValue: 0,
    paramType: index % 2 === 0 ? 'control' : 'protection',
    permission: index % 3 === 0 ? 'visible' : 'hidden',
  }));
};

export const createDefaultProject = (): ProjectFile => ({
  projectName: 'default_project',
  formatVersion: 1,
  productId: 1,
  keyId: 1,
  description: '',
  parameters: createDefaultParameters(),
});

export const paramTypeLabel = (t: ParamType) => (t === 'control' ? '控制' : '保护');
export const permissionLabel = (p: ParamPermission) => (p === 'visible' ? '可见' : '隐藏');