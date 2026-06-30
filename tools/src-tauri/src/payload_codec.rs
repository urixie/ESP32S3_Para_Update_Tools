//! Encode / decode the plaintext payload that is fed into AES-GCM.
//!
//! Layout v2 (all little-endian):
//!
//! ```text
//! +------------------------------+
//! | Payload Header (20 B)        |
//! +------------------------------+
//! | Parameter Record[72]         |   72 * 12 = 864 bytes
//! +------------------------------+
//! | Board Name                   |   UTF-8, encrypted metadata
//! +------------------------------+
//! | Parameter Name Table         |   concatenated UTF-8, no separator
//! +------------------------------+
//! ```

use crate::error::AppError;
use crate::model::{
    Parameter, ParamPermission, ParamType, ADDR_MAX, ADDR_MIN, BOARD_NAME_MAX_BYTES,
    BOARD_NAME_MAX_CHARS, PARAM_COUNT,
};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use std::io::{Cursor, Read, Write};

/// Magic bytes marking the start of the payload plaintext.
pub const PAYLOAD_MAGIC: &[u8; 4] = b"UPLD";
/// Payload Header size for schema v2.
pub const PAYLOAD_HEADER_LEN: usize = 20;
/// Each Parameter Record is exactly 12 bytes.
pub const RECORD_SIZE: u8 = 12;
/// Payload schema version. v2 adds encrypted board_name metadata and does not
/// keep compatibility with v1 bins.
pub const SCHEMA_VERSION: u16 = 2;

fn validate_board_name(board_name: &str) -> Result<&str, AppError> {
    let trimmed = board_name.trim();
    if trimmed.is_empty() {
        return Err(AppError::EmptyBoardName);
    }
    let char_count = trimmed.chars().count();
    if char_count > BOARD_NAME_MAX_CHARS {
        return Err(AppError::BoardNameCharsTooLong {
            max: BOARD_NAME_MAX_CHARS,
            actual: char_count,
        });
    }
    let actual = trimmed.as_bytes().len();
    if actual > BOARD_NAME_MAX_BYTES {
        return Err(AppError::BoardNameTooLong {
            max: BOARD_NAME_MAX_BYTES,
            actual,
        });
    }
    Ok(trimmed)
}

/// Encode the payload plaintext from a board name and list of parameters.
///
/// `parameters` MUST contain exactly `PARAM_COUNT` entries. The function will
/// reject anything else before touching the byte buffer so callers don't
/// silently write garbage to a bin file.
pub fn encode_payload(board_name: &str, parameters: &[Parameter]) -> Result<Vec<u8>, AppError> {
    if parameters.len() != PARAM_COUNT {
        return Err(AppError::InvalidParameterCount(parameters.len()));
    }

    let board_name = validate_board_name(board_name)?;
    let board_name_bytes = board_name.as_bytes();
    let board_name_len = board_name_bytes.len() as u16;

    // --- Step 1: build the Parameter Name Table and per-record tuples ---
    let mut name_table = Vec::new();
    let mut name_meta: Vec<(u16, u16)> = Vec::with_capacity(PARAM_COUNT);
    for p in parameters {
        let bytes = p.name.as_bytes();
        if bytes.len() > u16::MAX as usize {
            return Err(AppError::NameTooLong {
                address: p.address,
                max: u16::MAX as usize,
                actual: bytes.len(),
            });
        }
        let offset = name_table.len() as u16;
        let len = bytes.len() as u16;
        name_meta.push((offset, len));
        name_table.extend_from_slice(bytes);
    }

    let name_table_len = name_table.len() as u16;

    // --- Step 2: write the Payload Header v2 ---
    let mut out = Vec::with_capacity(
        PAYLOAD_HEADER_LEN + PARAM_COUNT * RECORD_SIZE as usize + board_name_bytes.len() + name_table.len(),
    );
    out.extend_from_slice(PAYLOAD_MAGIC);
    out.write_u16::<LittleEndian>(SCHEMA_VERSION).unwrap();
    out.write_u8(PARAM_COUNT as u8).unwrap();
    out.write_u8(RECORD_SIZE).unwrap();
    out.write_u16::<LittleEndian>(board_name_len).unwrap();
    out.write_u16::<LittleEndian>(name_table_len).unwrap();
    out.write_u16::<LittleEndian>(0).unwrap(); // payload_flags
    out.write_u16::<LittleEndian>(0).unwrap(); // reserved
    out.write_u32::<LittleEndian>(0).unwrap(); // payload_crc, reserved
    debug_assert_eq!(out.len(), PAYLOAD_HEADER_LEN);

    // --- Step 3: write 72 Parameter Records ---
    for (idx, p) in parameters.iter().enumerate() {
        let (name_offset, name_len) = name_meta[idx];
        out.write_u8(p.address).unwrap();
        out.write_u8(p.param_type.to_byte()).unwrap();
        out.write_u8(p.permission.to_byte()).unwrap();
        out.write_u8(0).unwrap(); // reserved0
        out.write_u16::<LittleEndian>(p.default_value).unwrap();
        out.write_u16::<LittleEndian>(name_offset).unwrap();
        out.write_u16::<LittleEndian>(name_len).unwrap();
        out.write_u16::<LittleEndian>(0).unwrap(); // reserved1
    }

    // --- Step 4: append encrypted board name and parameter names ---
    out.write_all(board_name_bytes).unwrap();
    out.write_all(&name_table).unwrap();

    Ok(out)
}

/// Decode the payload plaintext back into `(board_name, parameters)`.
///
/// Performs structural validation: Header magic, schema version, record size,
/// board-name UTF-8, address range, address uniqueness, name offsets and name
/// UTF-8 boundaries.
pub fn decode_payload(payload: &[u8]) -> Result<(String, Vec<Parameter>), AppError> {
    if payload.len() < PAYLOAD_HEADER_LEN {
        return Err(AppError::PayloadTooShort);
    }

    let mut cur = Cursor::new(payload);

    // --- Payload Header v2 ---
    let mut magic = [0u8; 4];
    cur.read_exact(&mut magic).map_err(|_| AppError::PayloadTooShort)?;
    if &magic != PAYLOAD_MAGIC {
        return Err(AppError::InvalidPayloadMagic);
    }
    let schema_version = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;
    if schema_version != SCHEMA_VERSION {
        return Err(AppError::UnsupportedSchemaVersion(schema_version));
    }
    let param_count = cur.read_u8().map_err(|_| AppError::PayloadTooShort)?;
    if param_count as usize != PARAM_COUNT {
        return Err(AppError::InvalidParameterCount(param_count as usize));
    }
    let record_size = cur.read_u8().map_err(|_| AppError::PayloadTooShort)?;
    if record_size != RECORD_SIZE {
        return Err(AppError::InvalidRecordSize(record_size));
    }
    let board_name_len = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)? as usize;
    let name_table_len = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)? as usize;
    let _payload_flags = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;
    let _reserved = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;
    let _payload_crc = cur.read_u32::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;

    if board_name_len == 0 || board_name_len > BOARD_NAME_MAX_BYTES {
        return Err(AppError::BoardNameOutOfBounds);
    }

    let records_total = (param_count as usize) * (record_size as usize);
    let board_name_offset = PAYLOAD_HEADER_LEN + records_total;
    let name_table_offset = board_name_offset
        .checked_add(board_name_len)
        .ok_or(AppError::PayloadTooShort)?;
    let expected_total = name_table_offset
        .checked_add(name_table_len)
        .ok_or(AppError::PayloadTooShort)?;
    if payload.len() < expected_total {
        return Err(AppError::PayloadTooShort);
    }

    let board_name_bytes = &payload[board_name_offset..board_name_offset + board_name_len];
    let board_name = std::str::from_utf8(board_name_bytes)
        .map_err(|_| AppError::InvalidUtf8BoardName)?
        .to_string();

    // --- 72 Parameter Records ---
    let mut parameters = Vec::with_capacity(PARAM_COUNT);
    let mut seen = [false; 72];
    for _ in 0..PARAM_COUNT {
        let address = cur.read_u8().map_err(|_| AppError::PayloadTooShort)?;
        if !(ADDR_MIN..=ADDR_MAX).contains(&address) {
            return Err(AppError::InvalidAddress(address));
        }
        if seen[address as usize] {
            return Err(AppError::DuplicateAddress(address));
        }
        seen[address as usize] = true;

        let param_type_byte = cur.read_u8().map_err(|_| AppError::PayloadTooShort)?;
        let param_type = ParamType::from_byte(param_type_byte)
            .ok_or(AppError::InvalidParamType(address))?;

        let permission_byte = cur.read_u8().map_err(|_| AppError::PayloadTooShort)?;
        let permission = ParamPermission::from_byte(permission_byte)
            .ok_or(AppError::InvalidPermission(address))?;

        let _reserved0 = cur.read_u8().map_err(|_| AppError::PayloadTooShort)?;
        let default_value = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;
        let name_offset = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)? as usize;
        let name_len = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)? as usize;
        let _reserved1 = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;

        if name_offset
            .checked_add(name_len)
            .map(|end| end > name_table_len)
            .unwrap_or(true)
        {
            return Err(AppError::NameTableOutOfBounds);
        }

        let name_bytes = &payload[name_table_offset + name_offset
            ..name_table_offset + name_offset + name_len];
        let name = std::str::from_utf8(name_bytes)
            .map_err(|_| AppError::InvalidUtf8Name(address))?
            .to_string();

        parameters.push(Parameter {
            address,
            name,
            default_value,
            param_type,
            permission,
        });
    }

    Ok((board_name, parameters))
}
