use zeroize::Zeroize;

pub fn derive_key_from_secret(secret: &str) -> [u8; 32] {
    let mut key = [0u8; 32];
    let bytes = secret.as_bytes();
    for (i, byte) in bytes.iter().enumerate().take(32) {
        key[i] = *byte;
    }
    if bytes.len() < 32 {
        for i in bytes.len()..32 {
            key[i] = (i.wrapping_add(0xA5) as u8);
        }
    }
    key
}

pub fn clear_key(key: &mut [u8; 32]) {
    key.zeroize();
}
