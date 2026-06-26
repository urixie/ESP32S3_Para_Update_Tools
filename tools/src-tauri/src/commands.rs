//! Tauri commands invoked from the React frontend.

use crate::bin_format::{
    export_encrypted_bin_bytes, parse_encrypted_bin_bytes, read_bin_file, write_bin_file,
};
use crate::error::AppError;
use crate::model::{Parameter, ParsedBinInfo, ProjectFile, ValidationError};
use crate::project_file::{default_project, load_project_file, save_project_file};
use crate::validator::validate_parameters;

/// Convenience conversion so commands can return `Result<T, String>` to the
/// frontend without leaking the AppError enum.
fn to_string_err<T>(result: Result<T, AppError>) -> Result<T, String> {
    result.map_err(|e| e.to_string())
}

/// Ping command kept for sanity checks / future liveness probes.
#[tauri::command]
pub fn ping() -> String {
    "pong".to_string()
}

/// Build a fresh project template (72 default parameters, product_id=1, key_id=1).
#[tauri::command]
pub fn new_project() -> ProjectFile {
    default_project()
}

/// Run every validation check on the parameter list. Returns an empty Vec on
/// success or one entry per failure.
#[tauri::command]
pub fn validate_parameters_cmd(params: Vec<Parameter>) -> Result<(), Vec<ValidationError>> {
    let errors = validate_parameters(&params);
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors)
    }
}

/// Save a `ProjectFile` (plaintext JSON) to the path the user picked.
#[tauri::command]
pub fn save_project_file_cmd(path: String, project: ProjectFile) -> Result<(), String> {
    to_string_err(save_project_file(std::path::Path::new(&path), &project))
}

/// Load a `ProjectFile` (plaintext JSON) from the path the user picked.
#[tauri::command]
pub fn load_project_file_cmd(path: String) -> Result<ProjectFile, String> {
    to_string_err(load_project_file(std::path::Path::new(&path)))
}

/// Encrypt parameters and write the resulting bin file to `path`.
#[tauri::command]
pub fn export_encrypted_bin_cmd(
    path: String,
    params: Vec<Parameter>,
    product_id: u32,
    key_id: u32,
) -> Result<(), String> {
    // Reject the export early if validation fails — we don't want to ship a
    // broken bin file even by accident.
    let errors = validate_parameters(&params);
    if !errors.is_empty() {
        let summary = errors
            .iter()
            .map(|e| e.message.clone())
            .collect::<Vec<_>>()
            .join("; ");
        return Err(format!("参数校验失败: {}", summary));
    }

    let bytes = to_string_err(export_encrypted_bin_bytes(&params, product_id, key_id))?;
    to_string_err(write_bin_file(std::path::Path::new(&path), &bytes).map(|_| ()))
}

/// Read and decrypt a bin file from `path`, returning all parameters + header.
#[tauri::command]
pub fn parse_encrypted_bin_cmd(path: String) -> Result<ParsedBinInfo, String> {
    let bytes = to_string_err(read_bin_file(std::path::Path::new(&path)))?;
    to_string_err(parse_encrypted_bin_bytes(&bytes))
}