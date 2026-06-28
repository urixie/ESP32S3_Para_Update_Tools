//! JSON project files. Plaintext JSON used only for in-house engineering
//! workflow — this is NOT the bin file format.

use crate::error::AppError;
use crate::model::{create_default_parameters, default_board_name, ProjectFile, PARAM_COUNT};
use std::path::Path;

/// Save a `ProjectFile` to disk as pretty-printed JSON.
pub fn save_project_file(path: &Path, project: &ProjectFile) -> Result<(), AppError> {
    let text = serde_json::to_string_pretty(project)?;
    std::fs::write(path, text)?;
    Ok(())
}

/// Load a `ProjectFile` from a JSON file on disk.
///
/// Serde ignores extra fields by default. `boardName` has a default so older
/// engineer-side project JSON can still be opened, but exported `.bin` files are
/// not backward-compatible with the old payload schema.
pub fn load_project_file(path: &Path) -> Result<ProjectFile, AppError> {
    let text = std::fs::read_to_string(path)?;
    let project: ProjectFile = serde_json::from_str(&text)?;
    Ok(project)
}

/// Build a default project template for "New Project" actions.
pub fn default_project() -> ProjectFile {
    ProjectFile {
        project_name: "default_project".to_string(),
        format_version: 2,
        board_name: default_board_name(),
        description: String::new(),
        parameters: create_default_parameters(),
    }
}

/// Re-fill missing addresses 0..PARAM_COUNT-1 with placeholder entries so the
/// GUI never holds a partial list. Used after loading a file that had a
/// non-standard shape.
#[allow(dead_code)]
pub fn normalize_parameters(list: Vec<crate::model::Parameter>) -> Vec<crate::model::Parameter> {
    if list.len() == PARAM_COUNT {
        return list;
    }
    create_default_parameters()
}
