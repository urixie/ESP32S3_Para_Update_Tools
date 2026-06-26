//! Bin file format: 48-byte plaintext Header + AES-256-GCM ciphertext+tag.
//!
//! Header layout (all little-endian, fixed 48 bytes):
//!
//! | off | field           | size | value                       |
//! |----:|-----------------|-----:|-----------------------------|
//! |   0 | magic           |    4 | "UEPB"                       |
//! |   4 | header_len      |    2 | 48                           |
//! |   6 | format_version  |    2 | 1                            |
//! |   8 | crypto_algo     |    1 | 1 = AES-256-GCM              |
//! |   9 | param_count     |    1 | 72                           |
//! |  10 | addr_min        |    1 | 0                            |
//! |  11 | addr_max        |    1 | 71                           |
//! |  12 | product_id      |    4 | caller supplied              |
//! |  16 | key_id          |    4 | caller supplied              |
//! |  20 | flags           |    4 | 0                            |
//! |  24 | nonce           |   12 | random per export            |
//! |  36 | payload_len     |    4 | ciphertext length (no tag)   |
//! |  40 | tag_len         |    1 | 16                           |
//! |  41 | reserved        |    7 | zeros                        |
//!
//! The Header is fed into AES-GCM as AAD, so any tampering with the header
//! itself causes decryption to fail.

use crate::crypto::{
    decrypt_payload_with_aad, encrypt_payload_with_aad, get_key_by_id, generate_nonce,
    KEY_LEN, NONCE_LEN, TAG_LEN,
};
use crate::error::AppError;
use crate::model::{BinHeaderInfo, Parameter, ParsedBinInfo, PARAM_COUNT};
use crate::payload_codec::{decode_payload, encode_payload};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use std::io::{Cursor, Read, Write};

/// File magic, 4 ASCII bytes.
pub const MAGIC: &[u8; 4] = b"UEPB";
/// Header is fixed at 48 bytes for the first format version.
pub const HEADER_LEN: u16 = 48;
/// Current format version.
pub const FORMAT_VERSION: u16 = 1;
/// Crypto algorithm ID: 1 = AES-256-GCM.
pub const CRYPTO_ALGO_AES_256_GCM: u8 = 1;
/// Total Header size constant for slicing.
pub const HEADER_SIZE: usize = 48;
/// Address range hard-coded into the header. Mirrored from `model.rs`.
pub const ADDR_MIN: u8 = 0;
pub const ADDR_MAX: u8 = 71;

/// Build the 48-byte Header for export. Returns the bytes ready to be used
/// as AAD and prepended to the file.
fn build_header(
    product_id: u32,
    key_id: u32,
    nonce: &[u8; NONCE_LEN],
    payload_cipher_len: u32,
) -> Vec<u8> {
    let mut h = Vec::with_capacity(HEADER_SIZE);
    h.extend_from_slice(MAGIC);
    h.write_u16::<LittleEndian>(HEADER_LEN).unwrap();
    h.write_u16::<LittleEndian>(FORMAT_VERSION).unwrap();
    h.write_u8(CRYPTO_ALGO_AES_256_GCM).unwrap();
    h.write_u8(PARAM_COUNT as u8).unwrap();
    h.write_u8(ADDR_MIN).unwrap();
    h.write_u8(ADDR_MAX).unwrap();
    h.write_u32::<LittleEndian>(product_id).unwrap();
    h.write_u32::<LittleEndian>(key_id).unwrap();
    h.write_u32::<LittleEndian>(0).unwrap(); // flags
    h.extend_from_slice(nonce);
    h.write_u32::<LittleEndian>(payload_cipher_len).unwrap();
    h.write_u8(TAG_LEN as u8).unwrap();
    h.extend_from_slice(&[0u8; 7]); // reserved
    debug_assert_eq!(h.len(), HEADER_SIZE);
    h
}

/// Parse the 48-byte Header into the public info struct. Validates every
/// fixed field; callers can rely on a successful return meaning the bin file
/// is at least structurally sound.
fn parse_header(header: &[u8]) -> Result<BinHeaderInfo, AppError> {
    if header.len() < HEADER_SIZE {
        return Err(AppError::BinTooSmall(HEADER_SIZE));
    }
    let mut cur = Cursor::new(&header[..HEADER_SIZE]);

    let mut magic = [0u8; 4];
    cur.read_exact(&mut magic).map_err(|_| AppError::InvalidMagic)?;
    if &magic != MAGIC {
        return Err(AppError::InvalidMagic);
    }

    let header_len = cur.read_u16::<LittleEndian>().map_err(|_| AppError::InvalidMagic)?;
    if header_len as usize != HEADER_SIZE {
        return Err(AppError::InvalidHeaderLen(header_len));
    }
    let format_version = cur.read_u16::<LittleEndian>().map_err(|_| AppError::InvalidMagic)?;
    if format_version != FORMAT_VERSION {
        return Err(AppError::UnsupportedFormatVersion(format_version));
    }
    let crypto_algo = cur.read_u8().map_err(|_| AppError::InvalidMagic)?;
    if crypto_algo != CRYPTO_ALGO_AES_256_GCM {
        return Err(AppError::UnsupportedCryptoAlgo(crypto_algo));
    }
    let param_count = cur.read_u8().map_err(|_| AppError::InvalidMagic)?;
    if param_count as usize != PARAM_COUNT {
        return Err(AppError::InvalidParamCount(param_count));
    }
    let addr_min = cur.read_u8().map_err(|_| AppError::InvalidMagic)?;
    let addr_max = cur.read_u8().map_err(|_| AppError::InvalidMagic)?;
    if addr_min != ADDR_MIN || addr_max != ADDR_MAX {
        return Err(AppError::InvalidAddressRange(addr_min, addr_max));
    }

    let product_id = cur.read_u32::<LittleEndian>().map_err(|_| AppError::InvalidMagic)?;
    let key_id = cur.read_u32::<LittleEndian>().map_err(|_| AppError::InvalidMagic)?;
    let flags = cur.read_u32::<LittleEndian>().map_err(|_| AppError::InvalidMagic)?;

    let mut nonce = [0u8; NONCE_LEN];
    cur.read_exact(&mut nonce).map_err(|_| AppError::InvalidMagic)?;

    let payload_len = cur.read_u32::<LittleEndian>().map_err(|_| AppError::InvalidMagic)?;
    let tag_len = cur.read_u8().map_err(|_| AppError::InvalidMagic)?;
    if tag_len as usize != TAG_LEN {
        return Err(AppError::UnsupportedCryptoAlgo(tag_len));
    }
    // Skip 7 reserved bytes; nothing to validate (must be zero by spec).

    Ok(BinHeaderInfo {
        header_len,
        format_version,
        crypto_algo,
        param_count,
        addr_min,
        addr_max,
        product_id,
        key_id,
        flags,
        nonce_hex: hex_encode(&nonce),
        payload_len,
        tag_len,
        file_size: 0,
    })
}

/// Encode parameters into a complete bin file (Header + ciphertext + tag).
pub fn export_encrypted_bin_bytes(
    parameters: &[Parameter],
    product_id: u32,
    key_id: u32,
) -> Result<Vec<u8>, AppError> {
    let key = get_key_by_id(product_id, key_id).ok_or(AppError::KeyNotFound {
        product_id,
        key_id,
    })?;

    let plaintext = encode_payload(parameters)?;
    let nonce = generate_nonce();

    // Two-pass approach: we don't know the ciphertext length until we have
    // computed it, but AES-GCM needs the AAD (= Header) up front. Solution:
    // encrypt with a placeholder AAD first, then re-encrypt with the real
    // Header once payload_len is known.
    let placeholder_header = build_header(product_id, key_id, &nonce, 0);
    let placeholder_ct =
        encrypt_payload_with_aad(&key, &nonce, &placeholder_header, &plaintext)?;
    let cipher_len = (placeholder_ct.len() - TAG_LEN) as u32;

    let header = build_header(product_id, key_id, &nonce, cipher_len);
    let ciphertext_and_tag = encrypt_payload_with_aad(&key, &nonce, &header, &plaintext)?;

    let mut out = Vec::with_capacity(HEADER_SIZE + ciphertext_and_tag.len());
    out.extend_from_slice(&header);
    out.extend_from_slice(&ciphertext_and_tag);
    Ok(out)
}

/// Parse a complete bin file and return the full result for the GUI.
pub fn parse_encrypted_bin_bytes(data: &[u8]) -> Result<ParsedBinInfo, AppError> {
    if data.len() < HEADER_SIZE + TAG_LEN {
        return Err(AppError::BinTooSmall(HEADER_SIZE + TAG_LEN));
    }

    let header = &data[..HEADER_SIZE];
    let body = &data[HEADER_SIZE..];

    let mut info = parse_header(header)?;
    info.file_size = data.len() as u64;

    // ciphertext_len is body minus the trailing tag
    if body.len() < TAG_LEN {
        return Err(AppError::BinTooSmall(HEADER_SIZE + TAG_LEN));
    }
    let payload_len = body.len() - TAG_LEN;
    if payload_len as u32 != info.payload_len {
        // If the on-disk body length disagrees with the declared payload_len,
        // treat the file as tampered.
        return Err(AppError::DecryptFailed);
    }

    let key = get_key_by_id(info.product_id, info.key_id).ok_or(AppError::KeyNotFound {
        product_id: info.product_id,
        key_id: info.key_id,
    })?;

    let mut nonce = [0u8; NONCE_LEN];
    let nonce_bytes = hex_decode(&info.nonce_hex).map_err(|_| AppError::InvalidMagic)?;
    if nonce_bytes.len() != NONCE_LEN {
        return Err(AppError::InvalidMagic);
    }
    nonce.copy_from_slice(&nonce_bytes);

    let plaintext = decrypt_payload_with_aad(&key, &nonce, header, body)?;
    let parameters = decode_payload(&plaintext)?;

    Ok(ParsedBinInfo {
        header: info,
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

/// Helper: reverse of `hex_encode`. Accepts either case.
fn hex_decode(s: &str) -> Result<Vec<u8>, AppError> {
    if s.len() % 2 != 0 {
        return Err(AppError::InvalidMagic);
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    let bytes = s.as_bytes();
    for i in (0..bytes.len()).step_by(2) {
        let hi = hex_nibble(bytes[i])?;
        let lo = hex_nibble(bytes[i + 1])?;
        out.push((hi << 4) | lo);
    }
    Ok(out)
}

fn hex_nibble(b: u8) -> Result<u8, AppError> {
    match b {
        b'0'..=b'9' => Ok(b - b'0'),
        b'a'..=b'f' => Ok(b - b'a' + 10),
        b'A'..=b'F' => Ok(b - b'A' + 10),
        _ => Err(AppError::InvalidMagic),
    }
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

// Suppress unused-import warnings on a stable toolchain.
#[allow(dead_code)]
fn _ensure_key_len_matches(_k: &[u8; KEY_LEN]) {}