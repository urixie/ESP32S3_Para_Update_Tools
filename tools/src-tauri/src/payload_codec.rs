use aes_gcm::{aead::{Aead, KeyInit, OsRng}, Aes256Gcm, Nonce};
use crate::error::AppError;

pub const NONCE_SIZE: usize = 12;

pub fn encrypt_payload(key: &[u8; 32], plaintext: &[u8]) -> Result<Vec<u8>, AppError> {
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|_| AppError::Decrypt)?;
    let nonce = Nonce::from_slice(&rand::random::<[u8; NONCE_SIZE]>());
    let ciphertext = cipher.encrypt(nonce, plaintext).map_err(|_| AppError::Decrypt)?;
    let mut output = Vec::with_capacity(NONCE_SIZE + ciphertext.len());
    output.extend_from_slice(nonce);
    output.extend_from_slice(&ciphertext);
    Ok(output)
}

pub fn decrypt_payload(key: &[u8; 32], data: &[u8]) -> Result<Vec<u8>, AppError> {
    if data.len() < NONCE_SIZE {
        return Err(AppError::InvalidBin);
    }
    let cipher = Aes256Gcm::new_from_slice(key).map_err(|_| AppError::Decrypt)?;
    let nonce = Nonce::from_slice(&data[..NONCE_SIZE]);
    let ciphertext = &data[NONCE_SIZE..];
    let plaintext = cipher.decrypt(nonce, ciphertext).map_err(|_| AppError::Decrypt)?;
    Ok(plaintext)
}
