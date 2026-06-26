use thiserror::Error;

#[derive(Error, Debug)]
pub enum AppError {
    #[error("序列化失败: {0}")]
    Serialize(String),
    #[error("解密失败")]
    Decrypt,
    #[error("无效的 bin 文件")]
    InvalidBin,
}
