use crate::error::AppError;
use crate::model::{Parameter, ParamPermission, ParamType};
use byteorder::{LittleEndian, WriteBytesExt};

pub const MAGIC: &[u8; 4] = b"PBTL";

pub fn encode_parameters(parameters: &[Parameter]) -> Result<Vec<u8>, AppError> {
    if parameters.len() != 72 {
        return Err(AppError::InvalidBin);
    }

    let mut buffer = Vec::new();
    buffer.extend_from_slice(MAGIC);

    for param in parameters {
        buffer.write_u8(param.address).map_err(|_| AppError::InvalidBin)?;
        let name_bytes = param.name.as_bytes();
        let mut name_chunk = [0u8; 32];
        let len = name_bytes.len().min(32);
        name_chunk[..len].copy_from_slice(&name_bytes[..len]);
        buffer.extend_from_slice(&name_chunk);
        buffer.write_u16::<LittleEndian>(param.default_value).map_err(|_| AppError::InvalidBin)?;
        buffer.write_u8(match param.param_type {
            ParamType::Control => 0,
            ParamType::Protection => 1,
        })?;
        buffer.write_u8(match param.permission {
            ParamPermission::Visible => 0,
            ParamPermission::Hidden => 1,
        })?;
        buffer.extend_from_slice(&[0u8; 2]);
    }

    Ok(buffer)
}
