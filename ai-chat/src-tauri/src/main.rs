#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use std::io::Write;

/// A per-user writable directory. The application may live in Program Files,
/// where its own directory is read-only for a normal process.
fn data_root() -> std::path::PathBuf {
    // The application may live in Program Files, where its own directory is
    // read-only. Prefer the reader's per-user data directory, then the
    // temporary directory, and only then the executable's folder.
    if let Ok(local) = std::env::var("LOCALAPPDATA") {
        let path = std::path::PathBuf::from(local).join("mengshee").join("ai-chat");
        if std::fs::create_dir_all(&path).is_ok() {
            return path;
        }
    }
    let temporary = std::env::temp_dir().join("mengshee-ai-chat");
    if std::fs::create_dir_all(&temporary).is_ok() {
        return temporary;
    }
    std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(|directory| directory.join("ai-chat-data")))
        .unwrap_or_else(|| std::path::PathBuf::from("ai-chat-data"))
}

/// Startup diagnostics: a double-clicked window that dies without a console
/// must still leave a trace.
fn log(message: &str) {
    let path = data_root().join("aichat.log");
    if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(file, "{message}");
    }
}

/// Where the running reader's bridge listens, and the token it expects.
#[derive(Serialize)]
struct BridgeInfo {
    port: u16,
    token: String,
}

#[tauri::command]
fn bridge_info() -> Result<BridgeInfo, String> {
    let data = std::env::var("LOCALAPPDATA").map_err(|error| error.to_string())?;
    let path = std::path::Path::new(&data)
        .join("mengshee")
        .join("ai-bridge.json");
    if !path.exists() {
        return Err("Mengshee 没有在运行。请先在 Mengshee 中打开一份文档，再用「编辑 → AI 对话窗口…」启动本窗口。".into());
    }
    let text = std::fs::read_to_string(&path)
        .map_err(|error| format!("无法读取桥接信息：{error}"))?;
    let value: serde_json::Value = serde_json::from_str(&text).map_err(|error| error.to_string())?;
    Ok(BridgeInfo {
        port: value["port"].as_u64().unwrap_or(0) as u16,
        token: value["token"].as_str().unwrap_or("").to_string(),
    })
}

/// Sends one chat-completions request. Doing it here, instead of from the
/// WebView, avoids CORS and keeps the key out of the page's network log.
#[tauri::command]
async fn chat_completion(base_url: String, api_key: String, body: serde_json::Value) -> Result<serde_json::Value, String> {
    let url = format!("{}/chat/completions", base_url.trim_end_matches('/'));
    let client = reqwest::Client::new();
    let mut request = client.post(&url).json(&body);
    if !api_key.trim().is_empty() {
        request = request.bearer_auth(api_key);
    }
    let response = request.send().await.map_err(|error| error.to_string())?;
    let status = response.status();
    let value: serde_json::Value = response.json().await.map_err(|error| error.to_string())?;
    if !status.is_success() {
        let message = value["error"]["message"]
            .as_str()
            .unwrap_or("request failed")
            .to_string();
        return Err(format!("HTTP {}: {message}", status.as_u16()));
    }
    Ok(value)
}

fn main() {
    log("start");
    std::panic::set_hook(Box::new(|info| {
        log(&format!("panic: {info}"));
    }));
    let result = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![bridge_info, chat_completion])
        .setup(|app| {
            // Create the window explicitly so a WebView2 failure is logged
            // instead of only panicking, and keep its data folder in a path we
            // know the app can write.
            log("setup");
            let data_directory = data_root().join("webview");
            let _ = std::fs::create_dir_all(&data_directory);
            log(&format!("webview data: {}", data_directory.display()));
            match tauri::WebviewWindowBuilder::new(app, "main", tauri::WebviewUrl::App("index.html".into()))
                .title("Mengshee · AI")
                .inner_size(720.0, 860.0)
                .data_directory(data_directory)
                .build()
            {
                Ok(_) => log("window created"),
                Err(error) => {
                    log(&format!("window error: {error}"));
                    return Err(Box::new(error));
                }
            }
            Ok(())
        })
        .run(tauri::generate_context!());
    match result {
        Ok(()) => log("exit: ok"),
        Err(error) => log(&format!("exit: error: {error}")),
    }
}
