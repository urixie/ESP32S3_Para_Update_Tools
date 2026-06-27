//! Unit tests for the whole encode / validate / encrypt / decrypt chain.

use crate::bin_format::{
    export_encrypted_bin_bytes, parse_encrypted_bin_bytes, MAGIC, FORMAT_VERSION, HEADER_SIZE,
};
use crate::model::{
    create_default_parameters, ParamPermission, ParamType, Parameter, PARAM_COUNT,
};
use crate::payload_codec::{decode_payload, encode_payload};
use crate::validator::validate_parameters;

/// Build 72 valid parameters with Chinese names for general tests.
fn build_sample() -> Vec<Parameter> {
    (0..PARAM_COUNT)
        .map(|i| Parameter {
            address: i as u8,
            name: format!("母线参数 {}", i),
            default_value: (i as u16) * 10,
            param_type: if i % 2 == 0 {
                ParamType::Control
            } else {
                ParamType::Protection
            },
            permission: if i % 3 == 0 {
                ParamPermission::Visible
            } else {
                ParamPermission::Hidden
            },
        })
        .collect()
}

#[test]
fn payload_roundtrip() {
    let params = build_sample();
    let bytes = encode_payload(&params).expect("encode ok");
    let parsed = decode_payload(&bytes).expect("decode ok");
    assert_eq!(parsed.len(), PARAM_COUNT);
    for (a, b) in params.iter().zip(parsed.iter()) {
        assert_eq!(a.address, b.address);
        assert_eq!(a.name, b.name);
        assert_eq!(a.default_value, b.default_value);
        assert_eq!(a.param_type, b.param_type);
        assert_eq!(a.permission, b.permission);
    }
}

#[test]
fn full_export_parse_roundtrip() {
    let params = build_sample();
    let bytes = export_encrypted_bin_bytes(&params).expect("export ok");
    assert!(bytes.starts_with(MAGIC));
    assert_eq!(bytes[4], FORMAT_VERSION);
    let parsed = parse_encrypted_bin_bytes(&bytes).expect("parse ok");
    assert_eq!(parsed.parameters.len(), PARAM_COUNT);
    for (a, b) in params.iter().zip(parsed.parameters.iter()) {
        assert_eq!(a.address, b.address);
        assert_eq!(a.name, b.name);
        assert_eq!(a.default_value, b.default_value);
        assert_eq!(a.param_type, b.param_type);
        assert_eq!(a.permission, b.permission);
    }
}

#[test]
fn simplified_header_layout_is_17_bytes() {
    let params = build_sample();
    let plaintext = crate::payload_codec::encode_payload(&params).expect("encode ok");
    let bin = export_encrypted_bin_bytes(&params).expect("export ok");
    let parsed = parse_encrypted_bin_bytes(&bin).expect("parse ok");

    assert_eq!(HEADER_SIZE, 17);
    assert_eq!(&bin[0..4], MAGIC);
    assert_eq!(bin[4], FORMAT_VERSION);
    assert_eq!(parsed.header.header_size as usize, HEADER_SIZE);
    assert_eq!(parsed.header.format_version, FORMAT_VERSION);
    assert_eq!(parsed.header.tag_len as usize, crate::crypto::TAG_LEN);
    assert_eq!(parsed.header.ciphertext_len as usize, plaintext.len());
    assert_eq!(parsed.header.file_size as usize, bin.len());
}

#[test]
fn address_completeness_valid() {
    let params = build_sample();
    let errors = validate_parameters(&params);
    assert!(errors.is_empty(), "expected no errors, got {:?}", errors);
}

#[test]
fn duplicate_address_rejected() {
    let mut params = build_sample();
    params[5].address = 3; // now addresses 3 (from index 5) and 3 (original index 3) collide
    let errors = validate_parameters(&params);
    assert!(!errors.is_empty(), "duplicate address must fail validation");
}

#[test]
fn missing_address_rejected() {
    let mut params = build_sample();
    params.remove(10); // drop one, count drops to 71
    let errors = validate_parameters(&params);
    assert!(!errors.is_empty(), "missing address must fail validation");
}

#[test]
fn empty_name_rejected() {
    let mut params = build_sample();
    params[7].name = "".to_string();
    let errors = validate_parameters(&params);
    assert!(
        errors.iter().any(|e| e.field.as_deref() == Some("name")),
        "empty name must fail validation"
    );
}

#[test]
fn default_value_boundaries_ok() {
    let mut params = build_sample();
    params[0].default_value = 0;
    params[1].default_value = 65535;
    let errors = validate_parameters(&params);
    assert!(errors.is_empty(), "0 and 65535 must be valid: {:?}", errors);
}

#[test]
fn chinese_name_not_present_in_bin_bytes() {
    let params = build_sample();
    let bytes = export_encrypted_bin_bytes(&params).expect("export ok");

    let needle = "母线参数".as_bytes();

    let header = &bytes[..HEADER_SIZE];
    assert!(
        find_subsequence(header, needle).is_none(),
        "Chinese name must not appear in the compact header"
    );

    let body = &bytes[HEADER_SIZE..];
    assert!(
        find_subsequence(body, needle).is_none(),
        "Chinese name must not appear in the encrypted body"
    );
}

#[test]
fn tampered_byte_fails_parse() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(&params).expect("export ok");
    // Flip one byte in the encrypted body/tag.
    let target = bytes.len() - 1;
    bytes[target] ^= 0xFF;
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "tampered byte must cause parse failure");
}

#[test]
fn tampered_header_fails_parse() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(&params).expect("export ok");
    // Flip one byte in the compact Header (AAD).
    bytes[5] ^= 0x55;
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "tampered header must cause parse failure");
}

#[test]
fn invalid_magic_rejected() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(&params).expect("export ok");
    bytes[0] = b'X';
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "wrong magic must fail");
}

#[test]
fn unsupported_format_version_rejected() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(&params).expect("export ok");
    bytes[4] = FORMAT_VERSION.wrapping_add(1);
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "wrong format version must fail");
}

#[test]
fn export_directly_rejects_duplicate_address() {
    // export_encrypted_bin_bytes() must validate its own input — calling it
    // directly (not via the Tauri command) with a duplicate address must
    // return an Err instead of producing a broken bin file.
    let mut params = build_sample();
    params[5].address = 3; // collide with params[3].address
    let result = export_encrypted_bin_bytes(&params);
    assert!(result.is_err(), "duplicate address must be rejected");
    match result.unwrap_err() {
        crate::error::AppError::ValidationFailed(_) => {}
        other => panic!("expected ValidationFailed, got {:?}", other),
    }
}

#[test]
fn export_directly_rejects_empty_name() {
    let mut params = build_sample();
    params[12].name = "".to_string();
    let result = export_encrypted_bin_bytes(&params);
    assert!(result.is_err(), "empty name must be rejected");
}

#[test]
fn export_directly_rejects_missing_address() {
    let mut params = build_sample();
    params.remove(20);
    let result = export_encrypted_bin_bytes(&params);
    assert!(result.is_err(), "wrong param count must be rejected");
}

#[test]
fn name_30_chinese_chars_ok() {
    // Exactly 30 Chinese characters — should pass both the 30-char rule
    // and the 96-byte rule (30 chars * 3 bytes = 90 bytes).
    let mut params = build_sample();
    params[0].name = "参".repeat(30);
    assert_eq!(params[0].name.chars().count(), 30);
    assert!(params[0].name.as_bytes().len() <= 96);
    let errors = crate::validator::validate_parameters(&params);
    assert!(
        errors.is_empty(),
        "30 Chinese chars should be valid, got {:?}",
        errors
    );
}

#[test]
fn name_31_chinese_chars_rejected() {
    // 31 Chinese chars -> 93 bytes (still under 96) but over the 30-char
    // limit, so validation must fail.
    let mut params = build_sample();
    params[0].name = "参".repeat(31);
    assert_eq!(params[0].name.chars().count(), 31);
    assert!(params[0].name.as_bytes().len() <= 96);
    let errors = crate::validator::validate_parameters(&params);
    assert!(
        !errors.is_empty(),
        "31 Chinese chars must be rejected by validation"
    );
    assert!(
        errors
            .iter()
            .any(|e| e.field.as_deref() == Some("name") && e.message.contains("30")),
        "error must mention the 30-char limit, got {:?}",
        errors
    );
}

#[test]
fn export_rejects_31_chinese_chars() {
    // The export function must not produce a bin when validation fails.
    let mut params = build_sample();
    params[0].name = "参".repeat(31);
    let result = export_encrypted_bin_bytes(&params);
    assert!(result.is_err(), "export must reject names longer than 30 chars");
}

#[test]
fn default_project_is_valid() {
    let project = crate::project_file::default_project();
    let errors = validate_parameters(&project.parameters);
    assert!(errors.is_empty(), "default project must validate: {:?}", errors);
}

#[test]
fn create_default_parameters_count() {
    let params = create_default_parameters();
    assert_eq!(params.len(), PARAM_COUNT);
}

/// Find `needle` in `haystack`, returning the index of the first occurrence.
fn find_subsequence(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || haystack.len() < needle.len() {
        return None;
    }
    for i in 0..=(haystack.len() - needle.len()) {
        if &haystack[i..i + needle.len()] == needle {
            return Some(i);
        }
    }
    None
}
