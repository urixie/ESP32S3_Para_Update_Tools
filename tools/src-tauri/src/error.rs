use thiserror::Error;

/// Single error type covering the whole backend. Each variant maps to a
/// scenario the GUI should be able to display verbatim.
#[derive(Error, Debug)]
pub enum AppError {
    #[error("参数数量必须为 72，实际为 {0}")]
    InvalidParameterCount(usize),

    #[error("参数地址非法: {0}")]
    InvalidAddress(u8),

    #[error("参数地址重复: {0}")]
    DuplicateAddress(u8),

    #[error("参数地址缺失: {0}")]
    MissingAddress(u8),

    #[error("参数名称不能为空: 地址 {0}")]
    EmptyName(u8),

    #[error("参数名称 UTF-8 字节长度超过 {max} 字节: 地址 {address}, 实际 {actual}")]
    NameTooLong { address: u8, max: usize, actual: usize },

    #[error("默认值越界: 地址 {0}")]
    InvalidDefaultValue(u8),

    #[error("参数类型非法: 地址 {0}")]
    InvalidParamType(u8),

    #[error("参数权限非法: 地址 {0}")]
    InvalidPermission(u8),

    #[error("Bin 文件过短: 至少需要 {0} 字节")]
    BinTooSmall(usize),

    #[error("Bin 文件 magic 错误，期望 UEPB")]
    InvalidMagic,

    #[error("Bin 文件 Header 长度错误: 期望 48, 实际 {0}")]
    InvalidHeaderLen(u16),

    #[error("Bin 文件格式版本不支持: {0}")]
    UnsupportedFormatVersion(u16),

    #[error("Bin 文件加密算法不支持: {0}")]
    UnsupportedCryptoAlgo(u8),

    #[error("Bin 文件参数数量错误: 期望 72, 实际 {0}")]
    InvalidParamCount(u8),

    #[error("Bin 文件地址范围错误: 期望 0~71, 实际 {0}~{1}")]
    InvalidAddressRange(u8, u8),

    #[error("找不到密钥: product_id={product_id}, key_id={key_id}")]
    KeyNotFound { product_id: u32, key_id: u32 },

    #[error("Bin 文件损坏、被篡改或密钥错误 (AES-GCM tag 校验失败)")]
    DecryptFailed,

    #[error("Payload Header magic 错误，期望 UPLD")]
    InvalidPayloadMagic,

    #[error("Payload schema_version 不支持: {0}")]
    UnsupportedSchemaVersion(u16),

    #[error("Payload record_size 错误: 期望 12, 实际 {0}")]
    InvalidRecordSize(u8),

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