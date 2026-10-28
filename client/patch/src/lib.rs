use std::{sync::RwLock};

use lazy_static::lazy_static;
use windows::Win32::System::SystemServices::DLL_PROCESS_ATTACH;
use windows::Win32::Foundation::{HANDLE, HINSTANCE};
use windows::Win32::System::LibraryLoader::GetModuleFileNameA;
use windows::Win32::System::Console::{SetStdHandle, STD_ERROR_HANDLE, STD_OUTPUT_HANDLE};
use std::ffi::CStr;
use std::io::Write;
use std::os::windows::io::AsRawHandle;
use std::path::Path;

mod interceptor;
mod marshal;
mod modules;
mod util;

use crate::modules::{MhyContext, ModuleManager,
                     CcpBlocker, Http, Misc, Security, WinHttp};

/// Redirect stdout/stderr to a log file next to the game executable.
/// A debug console is deliberately NOT allocated: this DLL is loaded by the game itself,
/// so a console would sit on top of the game at every launch, and a failed `AllocConsole`
/// used to abort the whole patch thread before any module ran.
unsafe fn setup_log() {
    let mut exe = [0u8; 260];
    let n = GetModuleFileNameA(None, &mut exe);
    let exe_path = if n > 0 {
        String::from_utf8_lossy(&exe[..n as usize]).to_string()
    } else {
        String::from(".")
    };
    let dir = Path::new(&exe_path).parent().map(|p| p.to_path_buf()).unwrap_or_default();
    let log_path = dir.join("hk4e-patch.log");

    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&log_path) {
        let _ = writeln!(f, "\n===== patch start {:?} =====", std::time::SystemTime::now());
        let _ = f.flush();
        let handle = HANDLE(f.as_raw_handle() as _);
        let _ = SetStdHandle(STD_OUTPUT_HANDLE, handle);
        let _ = SetStdHandle(STD_ERROR_HANDLE, handle);
        // Keep the handle alive for the lifetime of the process.
        std::mem::forget(f);
    }
}

/// Base URL every redirected request is rewritten to. Configurable from `hk4e-patch.cfg`
/// (`target = http://127.0.0.1:8089`) so a logging proxy can sit in front of the server.
pub static TARGET_BASE: std::sync::Mutex<String> = std::sync::Mutex::new(String::new());

pub fn target_base() -> String {
    if let Ok(g) = TARGET_BASE.lock() {
        if !g.is_empty() {
            return g.clone();
        }
    }
    "http://127.0.0.1:8088".to_string()
}
/// Reads `hk4e-patch.cfg` next to the game exe.
/// Supported keys (one per line, `#` comments allowed):
///   modules = ccp,security,http,misc,winhttp     (default: all)
///   dnslog  = 1                                  log every getaddrinfo()ed host
///   block   = host1,host2                        extra hosts forced to 0.0.0.0
fn cfg_value(key: &str, exe_dir: &Path) -> Option<String> {
    let cfg_path = exe_dir.join("hk4e-patch.cfg");
    let text = std::fs::read_to_string(&cfg_path).ok()?;
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some(rest) = line.strip_prefix(key) {
            let rest = rest.trim_start();
            let rest = rest.strip_prefix('=').or_else(|| rest.strip_prefix(':')).unwrap_or(rest);
            return Some(rest.trim().to_string());
        }
    }
    None
}

fn module_enabled(name: &str, exe_dir: &Path) -> bool {
    match cfg_value("modules", exe_dir) {
        Some(list) => list.split(',').any(|m| m.trim().eq_ignore_ascii_case(name)),
        None => true,
    }
}

unsafe fn thread_func() {
    setup_log();

    let mut module_manager = MODULE_MANAGER.write().unwrap();

    let mut buffer = [0u8; 260];
    GetModuleFileNameA(None, &mut buffer);
    let exe_path = CStr::from_ptr(buffer.as_ptr() as *const i8).to_str().unwrap();
    let exe_dir = Path::new(exe_path).parent().map(|p| p.to_path_buf()).unwrap_or_default();

    if let Some(v) = cfg_value("target", &exe_dir) {
        if let Ok(mut g) = TARGET_BASE.lock() {
            *g = v.trim().to_string();
            println!("[cfg] redirect target = {}", *g);
        }
    }

    // Block query_security_file ASAP
    if module_enabled("ccp", &exe_dir) {
        if let Some(v) = cfg_value("dnslog", &exe_dir) {
            modules::ccp_blocker_api::LOG_DNS.store(v.trim() == "1" || v.trim().eq_ignore_ascii_case("true"),
                                                std::sync::atomic::Ordering::Relaxed);
        }
        if let Some(v) = cfg_value("blockall", &exe_dir) {
            let on = v.trim() == "1" || v.trim().eq_ignore_ascii_case("true");
            modules::ccp_blocker_api::BLOCK_ALL.store(on, std::sync::atomic::Ordering::Relaxed);
            println!("[cfg] blockall = {}", on);
        }
        if let Some(v) = cfg_value("block", &exe_dir) {
            if let Ok(mut extra) = modules::ccp_blocker_api::EXTRA_BLOCKED.lock() {
                for h in v.split(',') {
                    let h = h.trim();
                    if !h.is_empty() {
                        extra.push(h.to_ascii_lowercase());
                    }
                }
                println!("[cfg] extra blocked hosts: {:?}", *extra);
            }
        }
        module_manager.enable(MhyContext::<CcpBlocker>::new(""));
    }

    util::disable_memprotect_guard();

    println!("Genshin Impact encryption patch\nMade by xeondev, modified by Kitsxki_\n(Modded for all version >= 6.5)");

    let exe_name = Path::new(exe_path).file_name().unwrap().to_str().unwrap();
    println!("Current executable name: {}", exe_name);

    if exe_name != "GenshinImpact.exe" && exe_name != "YuanShen.exe" {
        println!("Executable is not Genshin. Skipping initialization.");
        return;
    }

    println!("Initializing modules...");

    if module_enabled("security", &exe_dir) {
        module_manager.enable(MhyContext::<Security>::new(&exe_name));
    }
    marshal::find();
    if module_enabled("http", &exe_dir) {
        module_manager.enable(MhyContext::<Http>::new(&exe_name));
    }
    if module_enabled("misc", &exe_dir) {
        module_manager.enable(MhyContext::<Misc>::new(&exe_name));
    }
    if module_enabled("winhttp", &exe_dir) {
        module_manager.enable(MhyContext::<WinHttp>::new(&exe_name));
    }

    println!("Successfully initialized!");
}

lazy_static! {
    static ref MODULE_MANAGER: RwLock<ModuleManager> = RwLock::new(ModuleManager::default());
}

#[no_mangle]
#[allow(non_snake_case)]
unsafe extern "system" fn DllMain(_: HINSTANCE, call_reason: u32, _: *mut ()) -> bool {
    if call_reason == DLL_PROCESS_ATTACH {
        #[cfg(debug_assertions)]
        {
            thread_func();
        }
        #[cfg(not(debug_assertions))]
        {
            std::thread::spawn(|| thread_func());
        }
    }

    true
}
