#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod error;
mod model;
mod project_file;
mod bin_format;
mod payload_codec;
mod crypto;

use tauri::Manager;

fn main() {
  tauri::Builder::default()
    .invoke_handler(tauri::generate_handler![commands::ping])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}
