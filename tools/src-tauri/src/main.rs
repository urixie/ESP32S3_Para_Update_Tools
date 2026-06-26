#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod bin_format;
mod commands;
mod crypto;
mod error;
mod model;
mod payload_codec;
mod project_file;
mod validator;

#[cfg(test)]
mod tests;

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            commands::ping,
            commands::new_project,
            commands::validate_parameters_cmd,
            commands::save_project_file_cmd,
            commands::load_project_file_cmd,
            commands::export_encrypted_bin_cmd,
            commands::parse_encrypted_bin_cmd,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}