use thiserror::Error;

/// Single error type covering the whole backend. Each variant maps to a
/// scenario the GUI should be able to display verbatim.
#[derive(Error, Debug)]
pub enum AppError {
    #[error("参数校验失败: {0}")]
    ValidationFailed(String),

    #[error("板卡名称不能为空")]
    EmptyBoardName,

    #[error("板卡名称字符数超过 {max} 个，实际 {actual}")]
    BoardNameCharsTooLong { max: usize, actual: usize },

    #[error("板卡名称 UTF-8 字节长度超过 {max} 字节，实际 {actual}")]
    BoardNameTooLong { max: usize, actual: usize },

    #[error("参数数量必须为 72，实际为 {0}")]
    InvalidParameterCount(usize),

    #[error("参数地址非法: {0}")]
    InvalidAddress(u8),

    #[error("参数地址重复: {0}")]
    DuplicateAddress(u8),

    #[error("参数地址缺失: {0}")]
    #[allow(dead_code)]
    MissingAddress(u8),

    #[error("参数名称不能为空: 地址 {0}")]
    #[allow(dead_code)]
    EmptyName(u8),

    #[error("参数名称 UTF-8 字节长度超过 {max} 字节: 地址 {address}, 实际 {actual}")]
    NameTooLong { address: u8, max: usize, actual: usize },

    #[error("默认值越界: 地址 {0}")]
    #[allow(dead_code)]
    InvalidDefaultValue(u8),

    #[error("参数类型非法: 地址 {0}")]
    InvalidParamType(u8),

    #[error("参数权限非法: 地址 {0}")]
    InvalidPermission(u8),

    #[error("Bin 文件过短: 至少需要 {0} 字节")]
    BinTooSmall(usize),

    #[error("Bin 文件 magic 错误，期望 UEPB")]
    InvalidMagic,

    #[error("Bin 文件格式版本不支持: {0}")]
    UnsupportedFormatVersion(u16),

    #[error("Bin 文件损坏、被篡改或密钥错误 (AES-GCM tag 校验失败)")]
    DecryptFailed,

    #[error("Payload Header magic 错误，期望 UPLD")]
    InvalidPayloadMagic,

    #[error("Payload schema_version 不支持: {0}")]
    UnsupportedSchemaVersion(u16),

    #[error("Payload record_size 错误: 期望 12, 实际 {0}")]
    InvalidRecordSize(u8),

    #[error("Payload 板卡名称越界")]
    BoardNameOutOfBounds,

    #[error("Payload 板卡名称 UTF-8 解析失败")]
    InvalidUtf8BoardName,

    #[error("Payload Name Table 越界")]
    NameTableOutOfBounds,

    #[error("Payload Name Table UTF-8 解析失败: 地址 {0}")]
    InvalidUtf8Name(u8),

    #[error("Payload 长度不足")]
    PayloadTooShort,

    #[error("I/O 错误: {0}")]
    Io(String),

    #[error("JSON 序列化错误: {0}")]
    Json(String),

    #[error("未知错误: {0}")]
    #[allow(dead_code)]
    Unknown(String),
}

impl From<std::io::Error> for AppError {
    fn from(value: std::io::Error) -> Self {
        AppError::Io(value.to_string())
    }
}

impl From<serde_json::Error> for AppError {
    fn from(value: serde_json::Error) -> Self {
        AppError::Json(value.to_string())
    }
}
