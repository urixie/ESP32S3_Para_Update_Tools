//! AES-256-GCM helpers used by the bin format module.
//!
//! The exported ciphertext is the AES-GCM "combined" output: plain ciphertext
//! immediately followed by the 16-byte authentication tag. Decryption must
//! therefore pass that combined buffer back into the AEAD.
//!
//! This project currently targets one product and one customer, so the first
//! version uses one fixed product key shared by the PC tool and the ESP32
//! firmware. The key is never written into the bin file.

use crate::error::AppError;
use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use rand::RngCore;
use zeroize::Zeroize;

/// AES-256 key length in bytes.
pub const KEY_LEN: usize = 32;
/// AES-GCM nonce length in bytes.
pub const NONCE_LEN: usize = 12;
/// AES-GCM tag length in bytes.
pub const TAG_LEN: usize = 16;

/// Single fixed product key used by this tool and by the matching ESP32 parser.
///
/// Replace this demo value with a real randomly generated 32-byte key before
/// production. The same bytes must be embedded in the ESP32 firmware.
pub const PRODUCT_KEY: [u8; KEY_LEN] = [
    0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB, 0xED, 0x0F,
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
];

/// Generate a fresh random 12-byte AES-GCM nonce.
pub fn generate_nonce() -> [u8; NONCE_LEN] {
    let mut nonce = [0u8; NONCE_LEN];
    rand::thread_rng().fill_bytes(&mut nonce);
    nonce
}

/// Encrypt `plaintext` with the given key and nonce. The compact file header is
/// supplied as AAD so that any tampering with magic/version/nonce also fails the
/// authentication check.
///
/// Returns: ciphertext || tag, where tag is the last 16 bytes.
pub fn encrypt_payload_with_aad(
    key: &[u8; KEY_LEN],
    nonce: &[u8; NONCE_LEN],
    aad: &[u8],
    plaintext: &[u8],
) -> Result<Vec<u8>, AppError> {
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|_| AppError::DecryptFailed)?;
    let nonce = Nonce::from_slice(nonce);
    let payload = aes_gcm::aead::Payload {
        msg: plaintext,
        aad,
    };
    let ciphertext_and_tag = cipher
        .encrypt(nonce, payload)
        .map_err(|_| AppError::DecryptFailed)?;
    Ok(ciphertext_and_tag)
}

/// Decrypt `ciphertext_and_tag` and verify the AAD tag.
pub fn decrypt_payload_with_aad(
    key: &[u8; KEY_LEN],
    nonce: &[u8; NONCE_LEN],
    aad: &[u8],
    ciphertext_and_tag: &[u8],
) -> Result<Vec<u8>, AppError> {
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|_| AppError::DecryptFailed)?;
    let nonce = Nonce::from_slice(nonce);
    let payload = aes_gcm::aead::Payload {
        msg: ciphertext_and_tag,
        aad,
    };
    let plaintext = cipher
        .decrypt(nonce, payload)
        .map_err(|_| AppError::DecryptFailed)?;
    Ok(plaintext)
}

/// Zero out a key in memory. Used on hot paths to limit the lifetime of key
/// material in RAM.
#[allow(dead_code)]
pub fn clear_key(key: &mut [u8; KEY_LEN]) {
    key.zeroize();
}
