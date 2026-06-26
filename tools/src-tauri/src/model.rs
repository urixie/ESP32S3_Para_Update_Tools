use serde::{Deserialize, Serialize};

pub const PARAM_COUNT: usize = 72;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub enum ParamType {
    Control,
    Protection,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub enum ParamPermission {
    Visible,
    Hidden,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Parameter {
    pub address: u8,
    pub name: String,
    pub default_value: u16,
    pub param_type: ParamType,
    pub permission: ParamPermission,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ProjectFile {
    pub parameters: Vec<Parameter>,
}
