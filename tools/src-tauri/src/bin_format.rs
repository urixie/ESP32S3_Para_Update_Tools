//! Simplified bin file format: compact plaintext header + AES-256-GCM payload.
//!
//! The product currently has one product, one customer and one fixed key, so
//! the bin header only keeps fields that are actually needed by ESP32 parsing.
//! Board names and parameter names are encrypted inside the payload.
//!
//! File layout:
//!
//! | off | field          | size | value                  |
//! |----:|----------------|-----:|------------------------|
//! |   0 | magic          |    4 | "UEPB"                 |
//! |   4 | format_version |    1 | 1                      |
//! |   5 | nonce          |   12 | random per export      |
//!
//! Final file:
//!
//! ```text
//! [17-byte Header][AES-GCM ciphertext][16-byte GCM tag]
//! ```
//!
//! The 17-byte Header is fed into AES-GCM as AAD, so any tampering with
//! magic/version/nonce causes decryption to fail.

use crate::crypto::{
    decrypt_payload_with_aad, encrypt_payload_with_aad, generate_nonce, PRODUCT_KEY, NONCE_LEN,
    TAG_LEN,
};
use crate::error::AppError;
use crate::model::{BinHeaderInfo, Parameter, ParsedBinInfo};
use crate::payload_codec::{decode_payload, encode_payload};
use crate::validator::validate_parameters;
use std::io::Write;

/// File magic, 4 ASCII bytes.
pub const MAGIC: &[u8; 4] = b"UEPB";

/// Current compact bin container format version. The encrypted payload schema is
/// managed separately by `payload_codec::SCHEMA_VERSION`.
pub const FORMAT_VERSION: u8 = 1;

/// Compact Header size: 4-byte magic + 1-byte version + 12-byte nonce.
pub const HEADER_SIZE: usize = 17;

/// Build the compact 17-byte Header for export. Returns the bytes ready to be
/// used as AAD and prepended to the file.
fn build_header(nonce: &[u8; NONCE_LEN]) -> Vec<u8> {
    let mut h = Vec::with_capacity(HEADER_SIZE);
    h.extend_from_slice(MAGIC);
    h.push(FORMAT_VERSION);
    h.extend_from_slice(nonce);
    debug_assert_eq!(h.len(), HEADER_SIZE);
    h
}

/// Parse the compact 17-byte Header. Validates magic and version, and extracts
/// the nonce used by AES-GCM.
fn parse_header(header: &[u8], file_size: usize) -> Result<(BinHeaderInfo, [u8; NONCE_LEN]), AppError> {
    if header.len() < HEADER_SIZE {
        return Err(AppError::BinTooSmall(HEADER_SIZE));
    }

    if &header[0..4] != MAGIC {
        return Err(AppError::InvalidMagic);
    }

    let format_version = header[4];
    if format_version != FORMAT_VERSION {
        return Err(AppError::UnsupportedFormatVersion(format_version as u16));
    }

    let mut nonce = [0u8; NONCE_LEN];
    nonce.copy_from_slice(&header[5..17]);

    let body_len = file_size
        .checked_sub(HEADER_SIZE)
        .ok_or(AppError::BinTooSmall(HEADER_SIZE + TAG_LEN))?;
    if body_len < TAG_LEN {
        return Err(AppError::BinTooSmall(HEADER_SIZE + TAG_LEN));
    }

    let ciphertext_len = body_len - TAG_LEN;

    Ok((
        BinHeaderInfo {
            header_size: HEADER_SIZE as u16,
            format_version,
            nonce_hex: hex_encode(&nonce),
            ciphertext_len: ciphertext_len as u64,
            tag_len: TAG_LEN as u8,
            file_size: file_size as u64,
        },
        nonce,
    ))
}

/// Encode board name + parameters into a complete bin file (Header + ciphertext + tag).
///
/// `parameters` is validated here — any failure short-circuits with
/// `AppError::ValidationFailed`. This makes the function safe to call from any
/// code path, not just through the Tauri command layer.
pub fn export_encrypted_bin_bytes(board_name: &str, parameters: &[Parameter]) -> Result<Vec<u8>, AppError> {
    // 1. Validate first so we never emit a malformed bin file.
    let errors = validate_parameters(parameters);
    if !errors.is_empty() {
        let summary = errors
            .into_iter()
            .map(|e| e.message)
            .collect::<Vec<_>>()
            .join("; ");
        return Err(AppError::ValidationFailed(summary));
    }

    // 2. Build plaintext payload, then a fresh random nonce and compact header.
    let plaintext = encode_payload(board_name, parameters)?;
    let nonce = generate_nonce();
    let header = build_header(&nonce);

    // 3. Encrypt payload. Header is AAD, so changing magic/version/nonce will
    //    make AES-GCM verification fail on parse.
    let ciphertext_and_tag = encrypt_payload_with_aad(&PRODUCT_KEY, &nonce, &header, &plaintext)?;

    // 4. Assemble final file: Header || ciphertext || tag.
    let mut out = Vec::with_capacity(HEADER_SIZE + ciphertext_and_tag.len());
    out.extend_from_slice(&header);
    out.extend_from_slice(&ciphertext_and_tag);
    Ok(out)
}

/// Parse a complete bin file and return board name + full parameter result for the GUI.
pub fn parse_encrypted_bin_bytes(data: &[u8]) -> Result<ParsedBinInfo, AppError> {
    if data.len() < HEADER_SIZE + TAG_LEN {
        return Err(AppError::BinTooSmall(HEADER_SIZE + TAG_LEN));
    }

    let header = &data[..HEADER_SIZE];
    let body = &data[HEADER_SIZE..];

    let (info, nonce) = parse_header(header, data.len())?;

    let plaintext = decrypt_payload_with_aad(&PRODUCT_KEY, &nonce, header, body)?;
    let (board_name, parameters) = decode_payload(&plaintext)?;

    Ok(ParsedBinInfo {
        header: info,
        board_name,
        parameters,
    })
}

/// Helper: lowercase hex string for nonce display in the GUI.
fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push_str(&format!("{:02x}", b));
    }
    s
}

/// Convenience helper used by commands.rs to write a bin to disk and return
/// the actual number of bytes written.
pub fn write_bin_file(path: &std::path::Path, bytes: &[u8]) -> Result<usize, AppError> {
    let mut file = std::fs::File::create(path)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    Ok(bytes.len())
}

/// Read a bin file from disk into memory.
pub fn read_bin_file(path: &std::path::Path) -> Result<Vec<u8>, AppError> {
    let bytes = std::fs::read(path)?;
    Ok(bytes)
}
