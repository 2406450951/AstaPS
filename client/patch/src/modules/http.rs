use std::ffi::CString;

use super::{MhyContext, MhyModule, ModuleType};
use crate::marshal;
use anyhow::Result;
use ilhook::x64::Registers;
use crate::util;
use crate::target_base;

const WEB_REQUEST_UTILS_MAKE_INITIAL_URL: &str = "55 41 56 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 45 ?? ?? ?? ?? ?? 48 89 D6 48 89 CF 48 8B 0D ?? ?? ?? ??";
const BROWSER_LOAD_URL: &str = "41 B0 01 E9 08 00 00 00 0F 1F 84 00 00 00 00 00 56 57";
const BROWSER_LOAD_URL_OFFSET: usize = 0x10;

pub struct Http;

impl MhyModule for MhyContext<Http> {
    unsafe fn init(&mut self) -> Result<()> {

        let web_request_utils_make_initial_url = util::pattern_scan_il2cpp(self.assembly_name, WEB_REQUEST_UTILS_MAKE_INITIAL_URL);
        if let Some(addr) = web_request_utils_make_initial_url {
            println!("web_request_utils_make_initial_url: {:x}", addr as usize);
            self.interceptor.attach(
                addr as usize,
                on_make_initial_url,
            )?;
        }
        else
        {
            println!("Failed to find web_request_utils_make_initial_url");
        }

        let browser_load_url = util::pattern_scan_il2cpp(self.assembly_name, BROWSER_LOAD_URL);
        if let Some(addr) = browser_load_url {
            let addr_offset = addr as usize + BROWSER_LOAD_URL_OFFSET;
            println!("browser_load_url: {:x}", addr_offset);
            self.interceptor.attach(
                addr_offset,
                on_browser_load_url,
            )?;
        }
        else
        {
            println!("Failed to find browser_load_url");
        }
        
        Ok(())
    }

    unsafe fn de_init(&mut self) -> Result<()> {
        Ok(())
    }

    fn get_module_type(&self) -> super::ModuleType {
        ModuleType::Http
    }
}

/// The CN dispatch answer carries sdkenv=0 (mainland compliance mode), which this client refuses
/// with "The client is damaged, please reinstall the client. Error Code:(2,0,4)". Asking the server
/// for the OSREL variant makes it answer with sdkenv=2, the mode every private server runs in.
/// The stock server answers a few of the hk4e_global SDK endpoints with a stub the client refuses
/// ("The client is damaged ... Error Code:(2,0,4)"), and Javalin will not let a plugin replace an
/// existing route. So the patch points those paths at the plugin's own /gs71/sdk/* handlers.
fn rewrite_sdk_paths(url: &str) -> String {
    let mut url = url.to_string();
    const MAP: [(&str, &str); 7] = [
        ("/hk4e_global/mdk/shield/api/loadConfig", "/gs71/sdk/loadConfig"),
        ("/hk4e_global/combo/granter/api/getConfig", "/gs71/sdk/getConfig"),
        ("/hk4e_global/combo/granter/api/compareProtocolVersion", "/gs71/sdk/compareProtocolVersion"),
        ("/hk4e_global/account/ma-passport/api/getConfig", "/gs71/sdk/maPassportConfig"),
        ("/hk4e_cn/mdk/shield/api/loadConfig", "/gs71/sdk/loadConfig"),
        ("/hk4e_cn/combo/granter/api/compareProtocolVersion", "/gs71/sdk/compareProtocolVersion"),
        ("/device-fp/api/getExtList", "/gs71/sdk/getExtList"),
    ];
    for (from, to) in MAP {
        if let Some(pos) = url.find(from) {
            let mut out = String::with_capacity(url.len() + 8);
            out.push_str(&url[..pos]);
            out.push_str(to);
            out.push_str(&url[pos + from.len()..]);
            url = out;
        }
    }
    url
}

#[allow(dead_code)]
fn force_os_branch(url: &str) -> String {
    url.replace("version=CNRELWin", "version=OSRELWin")
        .replace("version=CNRELiOS", "version=OSRELiOS")
        .replace("version=CNRELAnd", "version=OSRELAnd")
}

/// Hosts whose content is pure static web assets (the login page's own HTML/JS/CSS, announcement
/// pages, i18n tables). Pointing these at the local server hands the embedded browser a JSON blob
/// instead of a page, which is one of the ways the login UI can end up reporting itself as broken.
/// They are CDN-hosted with no account data, so they are left pointing at the real internet.
const PASSTHROUGH_HOSTS: [&str; 4] = [
    "webstatic.mihoyo.com",
    "webstatic-sea.hoyoverse.com",
    // The device-fingerprint service signs its own requests; a faked reply makes the SDK treat
    // the machine as unregistered. Let the real service answer, it carries no account data.
    "public-data-api.mihoyo.com",
    // CDN-hosted SDK bootstrap configs (login_flow_notification / kcp_enable / httpdns). They are
    // served without a signature and the SDK parses them strictly; our stub answered with a
    // string where an object is expected.
    "sdk-static.mihoyo.com",
];

fn is_passthrough(url: &str) -> bool {
    PASSTHROUGH_HOSTS.iter().any(|h| url.contains(h))
}

unsafe extern "win64" fn on_make_initial_url(reg: *mut Registers, _: usize) {
    let str_length = *((*reg).rcx.wrapping_add(16) as *const u32);
    let str_ptr = (*reg).rcx.wrapping_add(20) as *const u8;

    let slice = std::slice::from_raw_parts(str_ptr, (str_length * 2) as usize);
    let url = String::from_utf16le(slice).unwrap();

    let mut new_url = target_base();

    url.split('/').skip(3).for_each(|s| {
        new_url.push_str("/");
        new_url.push_str(s);
    });

    if !url.contains("/query_cur_region") && !url.starts_with("file:") {
        if is_passthrough(&url) {
            println!("Passthrough (static): {url}");
            return;
        }
        let new_url = rewrite_sdk_paths(&new_url);
        println!("Redirect: {url} -> {new_url}");
        (*reg).rcx =
            marshal::ptr_to_string_ansi(CString::new(new_url.as_str()).unwrap().as_c_str()) as u64;
    }
}

unsafe extern "win64" fn on_browser_load_url(reg: *mut Registers, _: usize) {
    let str_length = *((*reg).rdx.wrapping_add(16) as *const u32);
    let str_ptr = (*reg).rdx.wrapping_add(20) as *const u8;

    let slice = std::slice::from_raw_parts(str_ptr, (str_length * 2) as usize);
    let url = String::from_utf16le(slice).unwrap();

    let mut new_url = target_base();
    url.split('/').skip(3).for_each(|s| {
        new_url.push_str("/");
        new_url.push_str(s);
    });

    if url.starts_with("file:") || url.starts_with("data:") { return; }
    println!("Browser::LoadURL: {url} -> {new_url}");

    (*reg).rdx =
        marshal::ptr_to_string_ansi(CString::new(new_url.as_str()).unwrap().as_c_str()) as u64;
}
