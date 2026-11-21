fn main() {
    // The release executable embeds these files; a frontend-only hotfix must
    // invalidate Cargo's cached build even when no Rust source has changed.
    println!("cargo:rerun-if-changed=../src");
    tauri_build::build()
}
