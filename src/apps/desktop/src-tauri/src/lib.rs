/// The desktop shell: the web UI plus native dialogs and file access (including watching the open
/// file, so edits made in another editor refresh the diagram). All app logic lives in the web UI.
#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .run(tauri::generate_context!())
        .expect("error while running DiaDraw");
}
