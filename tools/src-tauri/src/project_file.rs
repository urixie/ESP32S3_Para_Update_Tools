use crate::error::AppError;
use crate::model::ProjectFile;
use serde_json;
use std::fs;
use std::path::Path;

pub fn save_project_file(path: &Path, project: &ProjectFile) -> Result<(), AppError> {
    let text = serde_json::to_string_pretty(project).map_err(|err| AppError::Serialize(err.to_string()))?;
    fs::write(path, text).map_err(|err| AppError::Serialize(err.to_string()))
}

pub fn load_project_file(path: &Path) -> Result<ProjectFile, AppError> {
    let text = fs::read_to_string(path).map_err(|err| AppError::Serialize(err.to_string()))?;
    let project = serde_json::from_str(&text).map_err(|err| AppError::Serialize(err.to_string()))?;
    Ok(project)
}
