use std::ffi::CStr;
use std::sync::atomic::{AtomicBool, Ordering};

use super::{MhyContext, MhyModule, ModuleType};
use anyhow::Result;
use ilhook::x64::Registers;
use windows::{
    core::s,
    Win32::System::LibraryLoader::{GetModuleHandleA, GetProcAddress},
};

pub static LOG_DNS: AtomicBool = AtomicBool::new(false);
/// When set, every hostname except loopback resolves to 0.0.0.0. A private server needs no
/// internet, and any request the patch failed to redirect only ends in a multi-second timeout that
/// the SDK reports as a generic failure.
pub static BLOCK_ALL: AtomicBool = AtomicBool::new(false);

/// Hosts that must never resolve. The official dispatch endpoints are hard-coded here; extra
/// names can be appended from `hk4e-patch.cfg` (`block = a.com,b.com`) so a different client
/// build does not need a rebuild of the DLL.
pub static EXTRA_BLOCKED: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());

const BLOCKED: &[&str] = &[
    "dispatchosglobal.yuanshen.com",
    "dispatchcnglobal.yuanshen.com",
    "dispatchosglobal.hoyoverse.com",
    "dispatchcnglobal.hoyoverse.com",
];

pub struct CcpBlocker;

impl MhyModule for MhyContext<CcpBlocker> {
    unsafe fn init(&mut self) -> Result<()> {
        let winsock2 = GetModuleHandleA(s!("Ws2_32.dll")).unwrap();
        let getaddrinfo = GetProcAddress(winsock2, s!("getaddrinfo")).unwrap();

        self.interceptor
            .attach(getaddrinfo as usize, on_getaddrinfo)
    }

    unsafe fn de_init(&mut self) -> Result<()> {
        Ok(())
    }

    fn get_module_type(&self) -> ModuleType {
        ModuleType::CcpBlocker
    }
}

fn should_block(host: &str) -> bool {
    let host = host.trim_end_matches('.').to_ascii_lowercase();
    if BLOCKED.iter().any(|b| host == *b) {
        return true;
    }
    if let Ok(extra) = EXTRA_BLOCKED.lock() {
        if extra.iter().any(|b| !b.is_empty() && host == *b) {
            return true;
        }
    }
    false
}

unsafe extern "win64" fn on_getaddrinfo(reg: *mut Registers, _: usize) {
    let host_ptr = (*reg).rcx as *const i8;
    if host_ptr.is_null() {
        return;
    }
    let host = match CStr::from_ptr(host_ptr).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return,
    };

    if LOG_DNS.load(Ordering::Relaxed) {
        println!("dns: {}", host);
    }

    let block_all = BLOCK_ALL.load(Ordering::Relaxed)
        && !(host.eq_ignore_ascii_case("localhost")
            || host == "127.0.0.1"
            || host == "::1"
            || host.parse::<std::net::Ipv4Addr>().is_ok());

    // The replacement is written over the caller's buffer, so it must be at least as long.
    if (should_block(&host) || block_all) && host.len() >= 9 {
        println!("dns blocked: {}", host);
        std::ptr::copy_nonoverlapping(c"0.0.0.0".as_ptr(), (*reg).rcx as *mut i8, 9);
    }
}
