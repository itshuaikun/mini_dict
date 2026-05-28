use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::Path;

use mdict_rs::{MddFile, MdxFile};

const STATUS_OK: c_int = 0;
const STATUS_NO_ENTRY: c_int = 1;
const STATUS_ERROR: c_int = 2;
const MAX_LINK_DEPTH: usize = 8;

#[repr(C)]
pub struct MiniDictLdoceLookup {
    status: c_int,
    key: *mut c_char,
    html: *mut c_char,
    message: *mut c_char,
}

#[repr(C)]
pub struct MiniDictLdoceAsset {
    status: c_int,
    key: *mut c_char,
    data: *mut u8,
    len: usize,
    message: *mut c_char,
}

pub struct Reader {
    mdx: MdxFile,
    mdd: MddFile,
}

fn cstr_to_string(value: *const c_char) -> Result<String, String> {
    if value.is_null() {
        return Err("null string".to_string());
    }
    let cstr = unsafe { CStr::from_ptr(value) };
    cstr.to_str()
        .map(|s| s.to_string())
        .map_err(|error| error.to_string())
}

fn into_c_string(value: impl AsRef<str>) -> *mut c_char {
    let without_nuls = value.as_ref().replace('\0', "");
    CString::new(without_nuls)
        .expect("interior nul bytes were removed")
        .into_raw()
}

unsafe fn set_error(error_out: *mut *mut c_char, message: impl AsRef<str>) {
    if !error_out.is_null() {
        unsafe {
            *error_out = into_c_string(message);
        }
    }
}

fn make_result(
    status: c_int,
    key: Option<String>,
    html: Option<String>,
    message: Option<String>,
) -> *mut MiniDictLdoceLookup {
    Box::into_raw(Box::new(MiniDictLdoceLookup {
        status,
        key: key.map(into_c_string).unwrap_or(std::ptr::null_mut()),
        html: html.map(into_c_string).unwrap_or(std::ptr::null_mut()),
        message: message.map(into_c_string).unwrap_or(std::ptr::null_mut()),
    }))
}

fn link_target(text: &str) -> Option<String> {
    text.strip_prefix("@@@LINK=")
        .map(str::trim)
        .filter(|target| !target.is_empty())
        .map(ToOwned::to_owned)
}

fn lookup_following_links(reader: &Reader, query: &str) -> Result<Option<(String, String)>, String> {
    let mut current = query.to_string();
    for _ in 0..MAX_LINK_DEPTH {
        let Some(record) = reader
            .mdx
            .lookup(&current)
            .map_err(|error| error.to_string())?
        else {
            return Ok(None);
        };

        if let Some(target) = link_target(&record.text) {
            current = target;
            continue;
        }

        return Ok(Some((record.key, record.text)));
    }

    Err("too many LDOCE link redirects".to_string())
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_reader_open(
    mdx_path: *const c_char,
    mdd_path: *const c_char,
    error_out: *mut *mut c_char,
) -> *mut Reader {
    let path = match cstr_to_string(mdx_path) {
        Ok(path) => path,
        Err(error) => {
            unsafe { set_error(error_out, error) };
            return std::ptr::null_mut();
        }
    };

    let mdd_path = match cstr_to_string(mdd_path) {
        Ok(path) => path,
        Err(error) => {
            unsafe { set_error(error_out, error) };
            return std::ptr::null_mut();
        }
    };

    match (
        MdxFile::open(Path::new(&path)),
        MddFile::open(Path::new(&mdd_path)),
    ) {
        (Ok(mdx), Ok(mdd)) => Box::into_raw(Box::new(Reader { mdx, mdd })),
        (Err(error), _) | (_, Err(error)) => {
            unsafe { set_error(error_out, error.to_string()) };
            std::ptr::null_mut()
        }
    }
}

fn make_asset(
    status: c_int,
    key: Option<String>,
    data: Option<Vec<u8>>,
    message: Option<String>,
) -> *mut MiniDictLdoceAsset {
    let (data_ptr, len) = if let Some(data) = data {
        let mut boxed = data.into_boxed_slice();
        let len = boxed.len();
        let ptr = boxed.as_mut_ptr();
        std::mem::forget(boxed);
        (ptr, len)
    } else {
        (std::ptr::null_mut(), 0)
    };

    Box::into_raw(Box::new(MiniDictLdoceAsset {
        status,
        key: key.map(into_c_string).unwrap_or(std::ptr::null_mut()),
        data: data_ptr,
        len,
        message: message.map(into_c_string).unwrap_or(std::ptr::null_mut()),
    }))
}

fn resource_key_candidates(key: &str) -> Vec<String> {
    let trimmed = key
        .strip_prefix("sound://")
        .unwrap_or(key)
        .trim_start_matches('/')
        .trim_start_matches('\\')
        .replace('/', "\\");
    let mut candidates = Vec::new();
    candidates.push(format!("\\{trimmed}"));
    candidates.push(trimmed);
    candidates
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_reader_lookup_asset(
    reader: *mut Reader,
    key: *const c_char,
) -> *mut MiniDictLdoceAsset {
    if reader.is_null() {
        return make_asset(
            STATUS_ERROR,
            None,
            None,
            Some("local dictionary reader is not open".to_string()),
        );
    }

    let key = match cstr_to_string(key) {
        Ok(key) => key,
        Err(error) => {
            return make_asset(STATUS_ERROR, None, None, Some(error));
        }
    };

    let reader = unsafe { &*reader };
    for candidate in resource_key_candidates(&key) {
        match reader.mdd.lookup(&candidate) {
            Ok(Some(resource)) => {
                return make_asset(STATUS_OK, Some(resource.key), Some(resource.data), None);
            }
            Ok(None) => continue,
            Err(error) => {
                return make_asset(STATUS_ERROR, None, None, Some(error.to_string()));
            }
        }
    }

    make_asset(
        STATUS_NO_ENTRY,
        None,
        None,
        Some(format!("No local LDOCE asset found for {key}")),
    )
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_reader_lookup(
    reader: *mut Reader,
    query: *const c_char,
) -> *mut MiniDictLdoceLookup {
    if reader.is_null() {
        return make_result(
            STATUS_ERROR,
            None,
            None,
            Some("local dictionary reader is not open".to_string()),
        );
    }

    let query = match cstr_to_string(query) {
        Ok(query) => query,
        Err(error) => return make_result(STATUS_ERROR, None, None, Some(error)),
    };

    let reader = unsafe { &*reader };
    match lookup_following_links(reader, &query) {
        Ok(Some((key, html))) => make_result(STATUS_OK, Some(key), Some(html), None),
        Ok(None) => make_result(
            STATUS_NO_ENTRY,
            None,
            None,
            Some("No local LDOCE entry found.".to_string()),
        ),
        Err(error) => make_result(STATUS_ERROR, None, None, Some(error)),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_lookup_status(result: *const MiniDictLdoceLookup) -> c_int {
    if result.is_null() {
        return STATUS_ERROR;
    }
    unsafe { (*result).status }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_lookup_key(
    result: *const MiniDictLdoceLookup,
) -> *const c_char {
    if result.is_null() {
        return std::ptr::null();
    }
    unsafe { (*result).key }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_lookup_html(
    result: *const MiniDictLdoceLookup,
) -> *const c_char {
    if result.is_null() {
        return std::ptr::null();
    }
    unsafe { (*result).html }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_lookup_message(
    result: *const MiniDictLdoceLookup,
) -> *const c_char {
    if result.is_null() {
        return std::ptr::null();
    }
    unsafe { (*result).message }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_lookup_free(result: *mut MiniDictLdoceLookup) {
    if result.is_null() {
        return;
    }
    let result = unsafe { Box::from_raw(result) };
    if !result.key.is_null() {
        let _ = unsafe { CString::from_raw(result.key) };
    }
    if !result.html.is_null() {
        let _ = unsafe { CString::from_raw(result.html) };
    }
    if !result.message.is_null() {
        let _ = unsafe { CString::from_raw(result.message) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_asset_status(result: *const MiniDictLdoceAsset) -> c_int {
    if result.is_null() {
        return STATUS_ERROR;
    }
    unsafe { (*result).status }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_asset_key(
    result: *const MiniDictLdoceAsset,
) -> *const c_char {
    if result.is_null() {
        return std::ptr::null();
    }
    unsafe { (*result).key }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_asset_data(
    result: *const MiniDictLdoceAsset,
) -> *const u8 {
    if result.is_null() {
        return std::ptr::null();
    }
    unsafe { (*result).data }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_asset_len(result: *const MiniDictLdoceAsset) -> usize {
    if result.is_null() {
        return 0;
    }
    unsafe { (*result).len }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_asset_message(
    result: *const MiniDictLdoceAsset,
) -> *const c_char {
    if result.is_null() {
        return std::ptr::null();
    }
    unsafe { (*result).message }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_asset_free(result: *mut MiniDictLdoceAsset) {
    if result.is_null() {
        return;
    }
    let result = unsafe { Box::from_raw(result) };
    if !result.key.is_null() {
        let _ = unsafe { CString::from_raw(result.key) };
    }
    if !result.data.is_null() && result.len > 0 {
        let _ = unsafe { Vec::from_raw_parts(result.data, result.len, result.len) };
    }
    if !result.message.is_null() {
        let _ = unsafe { CString::from_raw(result.message) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_reader_free(reader: *mut Reader) {
    if !reader.is_null() {
        let _ = unsafe { Box::from_raw(reader) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn mini_dict_ldoce_string_free(value: *mut c_char) {
    if !value.is_null() {
        let _ = unsafe { CString::from_raw(value) };
    }
}
