use serde::{Deserialize, Serialize};

/// Fixed number of parameters expected in every project and every bin file.
pub const PARAM_COUNT: usize = 72;

/// Address range allowed for parameters.
pub const ADDR_MIN: u8 = 0;
pub const ADDR_MAX: u8 = 71;

/// Maximum length of a board name in Unicode characters.
pub const BOARD_NAME_MAX_CHARS: usize = 32;

/// Maximum length of a UTF-8 encoded board name in bytes. Chinese board names
/// stay encrypted in the bin payload and are decoded only by the ESP32/tools.
pub const BOARD_NAME_MAX_BYTES: usize = 96;

/// Maximum length of a parameter name in Unicode characters.
pub const NAME_MAX_CHARS: usize = 30;

/// Maximum length of a UTF-8 encoded parameter name in bytes. This is the
/// protocol-level safety limit; `NAME_MAX_CHARS` is the user-facing limit
/// and `NAME_MAX_BYTES` is the hard limit enforced on the wire.
pub const NAME_MAX_BYTES: usize = 96;

pub fn default_board_name() -> String {
    "默认板卡".to_string()
}

/// Type of a parameter (control vs protection).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ParamType {
    Control,
    Protection,
}

impl ParamType {
    pub fn to_byte(self) -> u8 {
        match self {
            ParamType::Control => 0,
            ParamType::Protection => 1,
        }
    }

    pub fn from_byte(value: u8) -> Option<Self> {
        match value {
            0 => Some(ParamType::Control),
            1 => Some(ParamType::Protection),
            _ => None,
        }
    }
}

/// Permission for a parameter (visible vs hidden).
/// 0 = hidden, 1 = visible — both internally and on the wire.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ParamPermission {
    Visible,
    Hidden,
}

impl ParamPermission {
    pub fn to_byte(self) -> u8 {
        match self {
            ParamPermission::Visible => 1,
            ParamPermission::Hidden => 0,
        }
    }

    pub fn from_byte(value: u8) -> Option<Self> {
        match value {
            1 => Some(ParamPermission::Visible),
            0 => Some(ParamPermission::Hidden),
            _ => None,
        }
    }
}

/// Single parameter record.
///
/// The frontend communicates fields in camelCase (`defaultValue`, `paramType`),
/// therefore every Rust field is renamed via `rename = "..."` so JSON stays
/// stable and round-trips work without manual conversion.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Parameter {
    pub address: u8,
    pub name: String,
    pub default_value: u16,
    /// Renamed from `param_type` so it does not collide with the reserved
    /// `type` keyword on the JS side. Frontend uses `paramType`.
    #[serde(rename = "paramType")]
    pub param_type: ParamType,
    pub permission: ParamPermission,
}

/// Project file (JSON, plaintext, used internally by the engineer only).
///
/// Note: this is NOT the bin file format. It is a human-readable JSON file the
/// engineer can save and reload from the GUI. The board name is stored here for
/// editing convenience and is encrypted when exported into the `.bin` file.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProjectFile {
    pub project_name: String,
    pub format_version: u16,
    #[serde(default = "default_board_name")]
    pub board_name: String,
    pub description: String,
    pub parameters: Vec<Parameter>,
}

impl Default for ProjectFile {
    fn default() -> Self {
        Self {
            project_name: "default_project".to_string(),
            format_version: 2,
            board_name: default_board_name(),
            description: String::new(),
            parameters: create_default_parameters(),
        }
    }
}

/// Public compact-header information reported back to the GUI after parsing a
/// bin file.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BinHeaderInfo {
    pub header_size: u16,
    pub format_version: u8,
    pub nonce_hex: String,
    pub ciphertext_len: u64,
    pub tag_len: u8,
    pub file_size: u64,
}

/// Full result returned when parsing a bin file.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ParsedBinInfo {
    pub header: BinHeaderInfo,
    pub board_name: String,
    pub parameters: Vec<Parameter>,
}

/// One validation problem reported back to the frontend.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidationError {
    pub address: Option<u8>,
    pub field: Option<String>,
    pub message: String,
}

/// Build a fresh 72-parameter template used by the GUI when a new project is
/// started or when the user clicks "Reset".
pub fn create_default_parameters() -> Vec<Parameter> {
    (0..PARAM_COUNT)
        .map(|i| Parameter {
            address: i as u8,
            name: format!("参数 {}", i),
            default_value: 0,
            param_type: if i % 2 == 0 {
                ParamType::Control
            } else {
                ParamType::Protection
            },
            permission: if i % 3 == 0 {
                ParamPermission::Visible
            } else {
                ParamPermission::Hidden
            },
        })
        .collect()
}
