//! Unit tests for the whole encode / validate / encrypt / decrypt chain.

use crate::bin_format::{
    export_encrypted_bin_bytes, parse_encrypted_bin_bytes, MAGIC, FORMAT_VERSION, HEADER_SIZE,
};
use crate::model::{
    create_default_parameters, ParamPermission, ParamType, Parameter, PARAM_COUNT,
};
use crate::payload_codec::{decode_payload, encode_payload};
use crate::validator::validate_parameters;

const SAMPLE_BOARD_NAME: &str = "1号驱动板";

/// Build 72 valid parameters with Chinese names for general tests.
fn build_sample() -> Vec<Parameter> {
    (0..PARAM_COUNT)
        .map(|i| Parameter {
            address: i as u8,
            name: format!("母线参数 {}", i),
            default_value: (i as u32) * 10,
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
    let bytes = encode_payload(SAMPLE_BOARD_NAME, &params).expect("encode ok");
    let (board_name, parsed) = decode_payload(&bytes).expect("decode ok");
    assert_eq!(board_name, SAMPLE_BOARD_NAME);
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
    let bytes = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");
    assert!(bytes.starts_with(MAGIC));
    assert_eq!(bytes[4], FORMAT_VERSION);
    let parsed = parse_encrypted_bin_bytes(&bytes).expect("parse ok");
    assert_eq!(parsed.board_name, SAMPLE_BOARD_NAME);
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
    let plaintext = crate::payload_codec::encode_payload(SAMPLE_BOARD_NAME, &params).expect("encode ok");
    let bin = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");
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
    params[5].address = 3;
    let errors = validate_parameters(&params);
    assert!(!errors.is_empty(), "duplicate address must fail validation");
}

#[test]
fn missing_address_rejected() {
    let mut params = build_sample();
    params.remove(10);
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
    params[1].default_value = u32::MAX;
    let errors = validate_parameters(&params);
    assert!(errors.is_empty(), "0 and u32::MAX must be valid: {:?}", errors);
}

#[test]
fn chinese_name_and_board_name_not_present_in_bin_bytes() {
    let params = build_sample();
    let bytes = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");

    for needle in ["母线参数".as_bytes(), SAMPLE_BOARD_NAME.as_bytes()] {
        let header = &bytes[..HEADER_SIZE];
        assert!(
            find_subsequence(header, needle).is_none(),
            "Chinese text must not appear in the compact header"
        );

        let body = &bytes[HEADER_SIZE..];
        assert!(
            find_subsequence(body, needle).is_none(),
            "Chinese text must not appear in the encrypted body"
        );
    }
}

#[test]
fn tampered_byte_fails_parse() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");
    let target = bytes.len() - 1;
    bytes[target] ^= 0xFF;
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "tampered byte must cause parse failure");
}

#[test]
fn tampered_header_fails_parse() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");
    bytes[5] ^= 0x55;
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "tampered header must cause parse failure");
}

#[test]
fn invalid_magic_rejected() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");
    bytes[0] = b'X';
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "wrong magic must fail");
}

#[test]
fn unsupported_format_version_rejected() {
    let params = build_sample();
    let mut bytes = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params).expect("export ok");
    bytes[4] = FORMAT_VERSION.wrapping_add(1);
    let result = parse_encrypted_bin_bytes(&bytes);
    assert!(result.is_err(), "wrong format version must fail");
}

#[test]
fn export_directly_rejects_duplicate_address() {
    let mut params = build_sample();
    params[5].address = 3;
    let result = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params);
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
    let result = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params);
    assert!(result.is_err(), "empty name must be rejected");
}

#[test]
fn export_directly_rejects_empty_board_name() {
    let params = build_sample();
    let result = export_encrypted_bin_bytes("   ", &params);
    assert!(result.is_err(), "empty board name must be rejected");
}

#[test]
fn export_directly_rejects_long_board_name() {
    let params = build_sample();
    let board_name = "板".repeat(crate::model::BOARD_NAME_MAX_CHARS + 1);
    let result = export_encrypted_bin_bytes(&board_name, &params);
    assert!(
        result.is_err(),
        "board names longer than 32 chars must be rejected"
    );
}

#[test]
fn export_directly_rejects_missing_address() {
    let mut params = build_sample();
    params.remove(20);
    let result = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params);
    assert!(result.is_err(), "wrong param count must be rejected");
}

#[test]
fn name_20_chinese_chars_ok() {
    let mut params = build_sample();
    params[0].name = "参".repeat(20);
    assert_eq!(params[0].name.chars().count(), 20);
    assert!(params[0].name.as_bytes().len() <= 96);
    let errors = crate::validator::validate_parameters(&params);
    assert!(
        errors.is_empty(),
        "20 Chinese chars should be valid, got {:?}",
        errors
    );
}

#[test]
fn name_21_chinese_chars_rejected() {
    let mut params = build_sample();
    params[0].name = "参".repeat(21);
    assert_eq!(params[0].name.chars().count(), 21);
    assert!(params[0].name.as_bytes().len() <= 96);
    let errors = crate::validator::validate_parameters(&params);
    assert!(
        !errors.is_empty(),
        "21 Chinese chars must be rejected by validation"
    );
    assert!(
        errors
            .iter()
            .any(|e| e.field.as_deref() == Some("name") && e.message.contains("20")),
        "error must mention the 20-char limit, got {:?}",
        errors
    );
}

#[test]
fn export_rejects_21_chinese_chars() {
    let mut params = build_sample();
    params[0].name = "参".repeat(21);
    let result = export_encrypted_bin_bytes(SAMPLE_BOARD_NAME, &params);
    assert!(result.is_err(), "export must reject names longer than 20 chars");
}

#[test]
fn default_project_is_valid() {
    let project = crate::project_file::default_project();
    assert!(!project.board_name.trim().is_empty());
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
