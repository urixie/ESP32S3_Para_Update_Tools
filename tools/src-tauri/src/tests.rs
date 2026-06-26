#[cfg(test)]
mod tests {
    use crate::bin_format::{encode_parameters, MAGIC};
    use crate::crypto::derive_key_from_secret;
    use crate::payload_codec::{decrypt_payload, encrypt_payload};
    use crate::model::{ParamPermission, ParamType, Parameter};

    #[test]
    fn test_bin_format_roundtrip() {
        let parameters = (0..72)
            .map(|address| Parameter {
                address: address as u8,
                name: format!("参数 {}", address),
                default_value: address as u16,
                param_type: if address % 2 == 0 { ParamType::Control } else { ParamType::Protection },
                permission: if address % 3 == 0 { ParamPermission::Visible } else { ParamPermission::Hidden },
            })
            .collect::<Vec<_>>();

        let encoded = encode_parameters(&parameters).expect("encode should succeed");
        assert!(encoded.starts_with(MAGIC));
        assert_eq!(encoded.len(), 4 + 72 * 38);

        let key = derive_key_from_secret("test-secret");
        let encrypted = encrypt_payload(&key, &encoded).expect("encrypt should succeed");
        let decrypted = decrypt_payload(&key, &encrypted).expect("decrypt should succeed");
        assert_eq!(&decrypted, &encoded);
    }
}
