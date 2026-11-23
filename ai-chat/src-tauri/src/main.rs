#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::collections::HashMap;
use std::io::{BufRead, Write};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::Sender;
use std::sync::{Mutex, OnceLock};
use tauri::Manager;

/// Everything the window needs is handed over by Mengshee when it starts it:
/// this is the reader's own window, not a program that looks for a partner.
#[derive(Clone)]
struct Launch {
    document: String,
    document_id: String,
    document_title: String,
    parent_hwnd: String,
}

static LAUNCH: OnceLock<Launch> = OnceLock::new();
static APP_HANDLE: OnceLock<tauri::AppHandle> = OnceLock::new();

type Pending = Mutex<HashMap<u64, Sender<Result<serde_json::Value, String>>>>;

static PENDING: OnceLock<Pending> = OnceLock::new();
static NEXT_ID: AtomicU64 = AtomicU64::new(1);

fn launch_info() -> Launch {
    let arguments: Vec<String> = std::env::args().collect();
    let mut document = String::new();
    let mut document_id = String::new();
    let mut document_title = String::new();
    let mut parent_hwnd = String::new();
    let mut index = 1;
    while index < arguments.len() {
        match arguments[index].as_str() {
            "--document" => {
                document = arguments.get(index + 1).cloned().unwrap_or_default();
                index += 2;
            }
            "--document-id" => {
                document_id = arguments.get(index + 1).cloned().unwrap_or_default();
                index += 2;
            }
            "--document-title" => {
                document_title = arguments.get(index + 1).cloned().unwrap_or_default();
                index += 2;
            }
            "--parent-hwnd" => {
                parent_hwnd = arguments.get(index + 1).cloned().unwrap_or_default();
                index += 2;
            }
            _ => index += 1,
        }
    }
    Launch {
        document,
        document_id,
        document_title,
        parent_hwnd,
    }
}

/// A per-user writable directory. The application may live in Program Files,
/// where its own directory is read-only for a normal process.
fn data_root() -> std::path::PathBuf {
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

fn log(message: &str) {
    let path = data_root().join("aichat.log");
    if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(file, "{message}");
    }
}

fn document_title(document: &str) -> String {
    let name = std::path::Path::new(document)
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
        .unwrap_or_default();
    if name.is_empty() {
        "Mengshee · AI".to_string()
    } else {
        format!("Mengshee · AI — {name}")
    }
}

fn window_title(launch: &Launch) -> String {
    format_window_title(&launch.document_title, &launch.document_id, &launch.document)
}

fn format_window_title(title: &str, document_id: &str, document: &str) -> String {
    let configured = title.trim();
    let base = if !configured.is_empty() {
        configured.to_string()
    } else {
        let fallback = document_title(document);
        fallback
            .strip_prefix("Mengshee · AI — ")
            .unwrap_or("Document")
            .to_string()
    };
    let id = document_id
        .chars()
        .filter(|character| character.is_ascii_alphanumeric())
        .take(8)
        .collect::<String>();
    if id.is_empty() {
        format!("Mengshee · AI — {base}")
    } else {
        format!("Mengshee · AI — {base} [{id}]")
    }
}

fn webview_data_directory(launch: &Launch) -> std::path::PathBuf {
    let id = launch
        .document_id
        .chars()
        .filter(|character| character.is_ascii_alphanumeric() || *character == '-' || *character == '_')
        .collect::<String>();
    data_root()
        .join("webview")
        .join(if id.is_empty() { "default".to_string() } else { id })
}

/// Reads the reader's replies from standard input and hands each one to the
/// request that is waiting for it.
fn start_reader() {
    std::thread::spawn(|| {
        let stdin = std::io::stdin();
        for line in stdin.lock().lines() {
            let Ok(line) = line else { break };
            let Ok(value) = serde_json::from_str::<serde_json::Value>(&line) else {
                continue;
            };
            if value["method"].as_str() == Some("documentIdentity") {
                if let Some(app) = APP_HANDLE.get() {
                    if let Some(window) = app.get_webview_window("main") {
                        let title = value["params"]["title"].as_str().unwrap_or_default();
                        let id = value["params"]["id"].as_str().unwrap_or_default();
                        let document = LAUNCH.get().map(|launch| launch.document.as_str()).unwrap_or_default();
                        let _ = window.set_title(&format_window_title(title, id, document));
                    }
                }
                continue;
            }
            let Some(id) = value["id"].as_u64() else { continue };
            let Some(pending) = PENDING.get() else { continue };
            let sender = pending.lock().ok().and_then(|mut map| map.remove(&id));
            if let Some(sender) = sender {
                if value["ok"].as_bool() == Some(true) {
                    let _ = sender.send(Ok(value["result"].clone()));
                } else {
                    let error = value["error"]
                        .as_str()
                        .unwrap_or("Mengshee 返回了错误")
                        .to_string();
                    let _ = sender.send(Err(error));
                }
            }
        }
    });
}

fn send_request(method: String, params: serde_json::Value) -> Result<serde_json::Value, String> {
    let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
    let (sender, receiver) = std::sync::mpsc::channel();
    PENDING
        .get()
        .ok_or_else(|| "这个窗口由 Mengshee 启动。".to_string())?
        .lock()
        .map_err(|_| "连接状态已损坏".to_string())?
        .insert(id, sender);

    let request = serde_json::json!({ "id": id, "method": method, "params": params });
    let line = serde_json::to_string(&request).map_err(|error| error.to_string())?;
    {
        let mut output = std::io::stdout().lock();
        output
            .write_all(format!("{line}\n").as_bytes())
            .map_err(|error| error.to_string())?;
        output.flush().map_err(|error| error.to_string())?;
    }

    match receiver.recv_timeout(std::time::Duration::from_secs(300)) {
        Ok(result) => result,
        Err(_) => {
            if let Some(pending) = PENDING.get() {
                if let Ok(mut map) = pending.lock() {
                    map.remove(&id);
                }
            }
            Err("Mengshee 没有响应。".into())
        }
    }
}

#[tauri::command]
async fn bridge_request(method: String, params: serde_json::Value) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || send_request(method, params))
        .await
        .map_err(|error| error.to_string())?
}

fn main() {
    let launch = launch_info();
    log(&format!("start: document={}", launch.document));
    std::panic::set_hook(Box::new(|info| {
        log(&format!("panic: {info}"));
    }));
    let _ = PENDING.set(Mutex::new(HashMap::new()));
    start_reader();
    let _ = LAUNCH.set(launch.clone());

    let result = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![bridge_request])
        .setup(move |app| {
            let _ = APP_HANDLE.set(app.handle().clone());
            let data_directory = webview_data_directory(&launch);
            let _ = std::fs::create_dir_all(&data_directory);
            let mut builder = tauri::WebviewWindowBuilder::new(app, "main", tauri::WebviewUrl::App("index.html".into()))
                .title(window_title(&launch))
                .inner_size(720.0, 860.0)
                .data_directory(data_directory);
            #[cfg(windows)]
            {
                if let Ok(raw) = launch.parent_hwnd.parse::<isize>() {
                    if raw != 0 {
                        builder = builder.owner_raw(windows::Win32::Foundation::HWND(raw as *mut core::ffi::c_void));
                    }
                }
            }
            match builder.build() {
                Ok(window) => {
                    let _ = window.set_focus();
                    log("window created");
                }
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
