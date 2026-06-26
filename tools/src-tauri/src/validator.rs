//! Project-level validation. Errors are accumulated into a `Vec` so the GUI
//! can show every problem at once instead of forcing the user to fix them one
//! at a time.

use crate::model::{Parameter, ValidationError, ADDR_MAX, ADDR_MIN, NAME_MAX_BYTES, PARAM_COUNT};

/// Run every check we care about. Returns an empty Vec if the parameters are
/// valid, otherwise one entry per failure.
pub fn validate_parameters(parameters: &[Parameter]) -> Vec<ValidationError> {
    let mut errors = Vec::new();

    // 1. Count
    if parameters.len() != PARAM_COUNT {
        errors.push(ValidationError {
            address: None,
            field: Some("parameters".into()),
            message: format!("参数数量必须为 {}，实际为 {}", PARAM_COUNT, parameters.len()),
        });
        // No point continuing structural checks if the count is wrong.
        return errors;
    }

    // 2. Address coverage and uniqueness
    let mut seen = [false; PARAM_COUNT];
    for p in parameters {
        let addr = p.address as usize;
        if !(addr >= ADDR_MIN as usize && addr <= ADDR_MAX as usize) {
            errors.push(ValidationError {
                address: Some(p.address),
                field: Some("address".into()),
                message: format!("参数地址非法: {}，允许范围 {}~{}", p.address, ADDR_MIN, ADDR_MAX),
            });
            continue;
        }
        if seen[addr] {
            errors.push(ValidationError {
                address: Some(p.address),
                field: Some("address".into()),
                message: format!("参数地址重复: {}", p.address),
            });
            continue;
        }
        seen[addr] = true;
    }
    for addr in 0..PARAM_COUNT {
        if !seen[addr] {
            errors.push(ValidationError {
                address: Some(addr as u8),
                field: Some("address".into()),
                message: format!("参数地址缺失: {}", addr),
            });
        }
    }

    // 3. Per-field checks
    for p in parameters {
        if p.name.trim().is_empty() {
            errors.push(ValidationError {
                address: Some(p.address),
                field: Some("name".into()),
                message: format!("参数名称不能为空: 地址 {}", p.address),
            });
        } else {
            let byte_len = p.name.as_bytes().len();
            if byte_len > NAME_MAX_BYTES {
                errors.push(ValidationError {
                    address: Some(p.address),
                    field: Some("name".into()),
                    message: format!(
                        "参数名称 UTF-8 字节长度超过 {} 字节: 地址 {}, 实际 {}",
                        NAME_MAX_BYTES, p.address, byte_len
                    ),
                });
            }
        }
    }

    errors
}