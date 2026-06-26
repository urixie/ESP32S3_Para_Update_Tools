//! Encode / decode the plaintext payload that is fed into AES-GCM.
//!
//! Layout (all little-endian):
//!
//! ```text
//! +--------------------------+
//! | Payload Header (16 B)    |
//! +--------------------------+
//! | Parameter Record[72]     |   72 * 12 = 864 bytes
//! +--------------------------+
//! | Name Table               |   concatenated UTF-8, no separator
//! +--------------------------+
//! ```

use crate::error::AppError;
use crate::model::{Parameter, ParamPermission, ParamType, ADDR_MAX, ADDR_MIN, PARAM_COUNT};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use std::io::{Cursor, Read, Write};

/// Magic bytes marking the start of the payload plaintext.
pub const PAYLOAD_MAGIC: &[u8; 4] = b"UPLD";
/// Payload Header size, fixed for the first schema version.
pub const PAYLOAD_HEADER_LEN: usize = 16;
/// Each Parameter Record is exactly 12 bytes.
pub const RECORD_SIZE: u8 = 12;
/// Payload schema version this module understands.
pub const SCHEMA_VERSION: u16 = 1;

/// Encode the payload plaintext from a list of parameters.
///
/// `parameters` MUST contain exactly `PARAM_COUNT` entries. The function will
/// reject anything else before touching the byte buffer so callers don't
/// silently write garbage to a bin file.
pub fn encode_payload(parameters: &[Parameter]) -> Result<Vec<u8>, AppError> {
    if parameters.len() != PARAM_COUNT {
        return Err(AppError::InvalidParameterCount(parameters.len()));
    }

    // --- Step 1: build the Name Table and per-record (offset, len) tuples ---
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

    // --- Step 2: write the Payload Header ---
    let mut out = Vec::with_capacity(PAYLOAD_HEADER_LEN + PARAM_COUNT * RECORD_SIZE as usize + name_table.len());
    out.extend_from_slice(PAYLOAD_MAGIC);
    out.write_u16::<LittleEndian>(SCHEMA_VERSION).unwrap();
    out.write_u8(PARAM_COUNT as u8).unwrap();
    out.write_u8(RECORD_SIZE).unwrap();
    out.write_u16::<LittleEndian>(name_table_len).unwrap();
    out.write_u16::<LittleEndian>(0).unwrap(); // payload_flags
    out.write_u32::<LittleEndian>(0).unwrap(); // payload_crc
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

    // --- Step 4: append the Name Table ---
    out.write_all(&name_table).unwrap();

    Ok(out)
}

/// Decode the payload plaintext back into a list of parameters.
///
/// Performs structural validation along the way: Header magic, schema version,
/// record size, address range, address uniqueness, name offsets and name UTF-8
/// boundaries.
pub fn decode_payload(payload: &[u8]) -> Result<Vec<Parameter>, AppError> {
    if payload.len() < PAYLOAD_HEADER_LEN {
        return Err(AppError::PayloadTooShort);
    }

    let mut cur = Cursor::new(payload);

    // --- Payload Header ---
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
    let name_table_len = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)? as usize;
    let _payload_flags = cur.read_u16::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;
    let _payload_crc = cur.read_u32::<LittleEndian>().map_err(|_| AppError::PayloadTooShort)?;

    let records_total = (param_count as usize) * (record_size as usize);
    let expected_total = PAYLOAD_HEADER_LEN + records_total + name_table_len;
    if payload.len() < expected_total {
        return Err(AppError::PayloadTooShort);
    }

    let name_table_offset = PAYLOAD_HEADER_LEN + records_total;

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

    Ok(parameters)
}