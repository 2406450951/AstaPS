// entry.cpp - injected overlay host.
//
// Loaded as <game>\Astrolabe.dll. Two jobs:
//   1. draw the ImGui menu over the game (D3D11 swap-chain Present hook)
//   2. load hk4e-patch.dll next to it, which does the actual dispatch/RSA patching
//
// Input is polled with GetCursorPos/GetAsyncKeyState instead of WM_ messages: Genshin keeps the
// mouse captured and hidden in gameplay, so a WndProc-only overlay would never see the cursor.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <tlhelp32.h>
#include <psapi.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>

#include "MinHook.h"
#include "bridge.h"
#include "menu.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_origPresent = nullptr;
ResizeBuffersFn g_origResize = nullptr;

HWND g_hwnd = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_imguiReady = false;
bool g_bridgeStarted = false;

HMODULE g_self = nullptr;

const int g_hotkeys[] = {VK_INSERT, VK_F1, VK_F2, VK_F3, VK_HOME, VK_END};
bool g_hotkeyDown[6] = {false};

// Serialises the log file. The render thread (Present hook) and the dialog watchdog both log, and
// unsynchronised CreateFile/WriteFile pairs from two threads were trashing the CRT heap - the crash
// dump showed msvcrt malloc/free frames with our own DLL right below them.
CRITICAL_SECTION g_logLock;
INIT_ONCE g_logLockOnce = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK initLogLock(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_logLock);
    return TRUE;
}

void logLine(const char* fmt, ...) {
    char path[MAX_PATH];
    GetModuleFileNameA(g_self, path, MAX_PATH);
    char* dot = strrchr(path, '.');
    if (dot) *dot = 0;
    lstrcatA(path, ".log");

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1200];
    snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, msg);

    InitOnceExecuteOnce(&g_logLockOnce, initLogLock, nullptr, nullptr);
    EnterCriticalSection(&g_logLock);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, line, (DWORD)lstrlenA(line), &written, nullptr);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_logLock);
}

void loadPatchSibling() {
    char dir[MAX_PATH];
    GetModuleFileNameA(g_self, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (!slash) return;
    *slash = 0;

    char dll[MAX_PATH];
    snprintf(dll, sizeof(dll), "%s\\hk4e-patch.dll", dir);
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) {
        logLine("hk4e-patch.dll not found next to the overlay, skipping patch");
        return;
    }
    HMODULE patch = LoadLibraryA(dll);
    logLine("hk4e-patch.dll -> %p", (void*)patch);
}

void createRenderTarget(IDXGISwapChain* swap) {
    ID3D11Texture2D* backbuffer = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer)) && backbuffer) {
        g_device->CreateRenderTargetView(backbuffer, nullptr, &g_rtv);
        backbuffer->Release();
    }
}

void initImGui(IDXGISwapChain* swap) {
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), (void**)&g_device)) || !g_device) {
        logLine("GetDevice(ID3D11Device) failed - the game is probably not on D3D11");
        return;
    }
    g_device->GetImmediateContext(&g_context);

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Genshin hides and re-locks the OS cursor every frame during gameplay. Let ImGui draw its own
    // software cursor instead of trying to change the system one.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;

    ImFontConfig fc;
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    const char* fonts[] = {"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msyh.ttf",
                           "C:\\Windows\\Fonts\\simhei.ttf"};
    ImFont* font = nullptr;
    for (const char* path : fonts) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        font = io.Fonts->AddFontFromFileTTF(path, 16.5f, &fc,
                                            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font) {
            logLine("font loaded: %s", path);
            break;
        }
    }
    if (!font) {
        io.Fonts->AddFontDefault();
        logLine("no CJK font found, falling back to the default font");
    }

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    g_imguiReady = true;
    logLine("imgui ready (hwnd=%p)", (void*)g_hwnd);
}

// ---------------------------------------------------------------- raw mouse input
//
// Genshin locks the OS cursor to the middle of the window during gameplay (Unity's
// Cursor.lockState = Locked), so GetCursorPos barely moves and the menu is unusable. A window
// registered with RIDEV_INPUTSINK still receives WM_INPUT while another window is in the
// foreground, so we track the physical mouse ourselves and drive ImGui from that.
bool g_cursorSeeded = false;
std::atomic<int> g_rawDx{0};
std::atomic<int> g_rawDy{0};
HWND g_rawWnd = nullptr;

LRESULT CALLBACK rawProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        UINT size = sizeof(RAWINPUT);
        RAWINPUT raw{};
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            raw.header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& m = raw.data.mouse;
            if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                g_rawDx.fetch_add(m.lLastX);
                g_rawDy.fetch_add(m.lLastY);
            }
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// The window has to live on a thread with its own message loop, otherwise WM_INPUT is queued and
// never dispatched (which is exactly what happened the first time).
DWORD WINAPI rawThread(LPVOID) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = rawProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GS71RawInput";
    RegisterClassExW(&wc);
    g_rawWnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                               wc.hInstance, nullptr);
    if (!g_rawWnd) {
        logLine("raw input window creation failed (err=%lu)", GetLastError());
        return 0;
    }

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;          // generic desktop
    rid.usUsage = 0x02;              // mouse
    rid.dwFlags = RIDEV_INPUTSINK;   // keep receiving while the game owns the foreground
    rid.hwndTarget = g_rawWnd;
    if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
        logLine("raw mouse input registered");
    else
        logLine("raw mouse input registration failed (err=%lu)", GetLastError());

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// A second RIDEV_INPUTSINK registration does not receive anything while the game keeps the mouse
// captured, so mirror the deltas the game itself reads: hook GetRawInputData and copy each mouse
// packet into our accumulator. Works regardless of who owns the mouse.
using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
GetRawInputDataFn g_origRawInputData = nullptr;
std::atomic<int> g_rawPackets{0};

UINT WINAPI hkGetRawInputData(HRAWINPUT hRaw, UINT cmd, LPVOID data, PUINT size, UINT hdrSize) {
    const UINT ret = g_origRawInputData(hRaw, cmd, data, size, hdrSize);
    if (ret != (UINT)-1 && data && cmd == RID_INPUT && hdrSize == sizeof(RAWINPUTHEADER)) {
        RAWINPUT* raw = (RAWINPUT*)data;   // writable: the menu zeroes the deltas below
        if (raw->header.dwType == RIM_TYPEMOUSE &&
            (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            g_rawDx.fetch_add(raw->data.mouse.lLastX);
            g_rawDy.fetch_add(raw->data.mouse.lLastY);
            g_rawPackets.fetch_add(1);
            // While the menu is up the game must not see the movement, otherwise the camera spins
            // behind the window. Zero the deltas in the caller's buffer: we keep our copy for the
            // software cursor, the game gets nothing.
            if (menu::isOpen()) {
                raw->data.mouse.lLastX = 0;
                raw->data.mouse.lLastY = 0;
            }
        }
    }
    return ret;
}

void installRawInputHook() {
    if (g_origRawInputData) return;
    MH_Initialize();   // hookSwapChain() may not have run yet
    if (MH_CreateHookApi(L"user32.dll", "GetRawInputData", (LPVOID)hkGetRawInputData,
                         (LPVOID*)&g_origRawInputData) == MH_OK &&
        MH_EnableHook(MH_ALL_HOOKS) == MH_OK) {
        logLine("GetRawInputData hooked");
    } else {
        logLine("GetRawInputData hook failed");
    }
}

void createRawInputWindow() {
    static bool started = false;
    if (started) return;
    started = true;
    CreateThread(nullptr, 0, rawThread, nullptr, 0, nullptr);
}

void feedInput() {
    ImGuiIO& io = ImGui::GetIO();
    if (!menu::isOpen()) {
        io.MouseDrawCursor = false;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        g_cursorSeeded = false;
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        return;
    }

    // The game calls ShowCursor(FALSE) and ClipCursor every frame while in gameplay, so a one-shot
    // ShowCursor(TRUE) when the menu opens is undone immediately. Redo it every frame, and let ImGui
    // draw its own cursor so the pointer stays visible even if the game wins the fight.
    ClipCursor(nullptr);
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    for (int i = 0; i < 8; ++i) {
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) break;
        if (ShowCursor(TRUE) >= 0) break;
    }
    io.MouseDrawCursor = true;

    // Cursor position is maintained by us: seed from the OS position when the menu opens, then
    // integrate the raw deltas. This keeps working even if the game pins the OS cursor.
    static float mx = -1.f, my = -1.f;
    if (!g_cursorSeeded) {
        POINT p;
        GetCursorPos(&p);
        if (g_hwnd) ScreenToClient(g_hwnd, &p);
        mx = (float)p.x;
        my = (float)p.y;
        g_rawDx.exchange(0);
        g_rawDy.exchange(0);
        g_cursorSeeded = true;
    }
    const bool ours = g_hwnd && GetForegroundWindow() == g_hwnd;
    int ddx = g_rawDx.exchange(0);
    int ddy = g_rawDy.exchange(0);
    // A single frame can carry a huge backlog (alt-tab, device wake). Cap it so the cursor cannot
    // teleport across the screen or run off to absurd coordinates.
    const int kMaxStep = 300;
    if (ddx > kMaxStep) ddx = kMaxStep;
    if (ddx < -kMaxStep) ddx = -kMaxStep;
    if (ddy > kMaxStep) ddy = kMaxStep;
    if (ddy < -kMaxStep) ddy = -kMaxStep;
    // Hybrid: if the OS cursor actually moves (game not locking it) trust it; if it is pinned
    // (gameplay locks it to the window centre) integrate the raw deltas instead.
    static POINT lastOs{INT_MIN, INT_MIN};
    POINT os;
    GetCursorPos(&os);
    if (g_hwnd) ScreenToClient(g_hwnd, &os);
    if (os.x != lastOs.x || os.y != lastOs.y) {
        mx = (float)os.x;
        my = (float)os.y;
        lastOs = os;
    } else if (ours) {
        mx += (float)ddx;
        my += (float)ddy;
    }
    static DWORD lastLog = 0;
    const DWORD nowTick = GetTickCount();
    if ((ddx || ddy) && nowTick - lastLog > 2000) {
        lastLog = nowTick;
        logLine("cursor: raw delta=%d,%d pos=%.0f,%.0f", ddx, ddy, mx, my);
    }
    // Clamp against the real client rect; DisplaySize can be 0 early on, which used to drive the
    // cursor to -1 and leave it there.
    float limitX = io.DisplaySize.x;
    float limitY = io.DisplaySize.y;
    if (limitX < 2.f || limitY < 2.f) {
        RECT rc{};
        if (g_hwnd && GetClientRect(g_hwnd, &rc)) {
            limitX = (float)(rc.right - rc.left);
            limitY = (float)(rc.bottom - rc.top);
        }
    }
    if (limitX < 2.f) limitX = 1920.f;
    if (limitY < 2.f) limitY = 1080.f;
    if (mx < 0.f) mx = 0.f;
    if (my < 0.f) my = 0.f;
    if (mx > limitX - 1.f) mx = limitX - 1.f;
    if (my > limitY - 1.f) my = limitY - 1.f;
    io.AddMousePosEvent(mx, my);
    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);

    static short wheelPrev = 0;
    short wheel = (short)GetAsyncKeyState(VK_MBUTTON);
    (void)wheel;
    wheelPrev = 0;
}

// ---------------------------------------------------------------- stack probe (F2)
//
// The client's "The client is damaged (2,0,4)" verdict is raised from native code and no client
// file contains the message as plain text, so it cannot be located statically. Pressing F2 dumps
// the return-address chain of every thread, resolved to module+offset, so the deciding call chain
// can be identified and then patched like key_sign_check already is.
struct ModuleRange {
    uintptr_t base;
    size_t size;
    char name[64];
};

struct ThreadFrames {
    DWORD tid;
    uintptr_t rip;
    int count;
    uintptr_t offset[24];
    char owner[24][48];
};

ThreadFrames g_frames[192];

void dumpThreadStacks() {
    HMODULE mods[256];
    DWORD needed = 0;
    ModuleRange ranges[256];
    int rangeCount = 0;

    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        int count = (int)(needed / sizeof(HMODULE));
        if (count > 256) count = 256;
        for (int i = 0; i < count; ++i) {
            MODULEINFO mi{};
            char path[MAX_PATH] = {0};
            if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi))) continue;
            GetModuleFileNameA(mods[i], path, MAX_PATH);
            const char* slash = strrchr(path, '\\');
            snprintf(ranges[rangeCount].name, sizeof(ranges[rangeCount].name), "%s",
                     slash ? slash + 1 : path);
            ranges[rangeCount].base = (uintptr_t)mi.lpBaseOfDll;
            ranges[rangeCount].size = mi.SizeOfImage;
            rangeCount++;
        }
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    DWORD self = GetCurrentProcessId();
    DWORD selfThread = GetCurrentThreadId();
    int slot = 0;

    // Nothing that can allocate or take a lock may run while a thread is suspended, otherwise the
    // probe deadlocks against the heap lock held by that thread. Collect raw values only, then
    // format everything after every thread is running again.
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != self) continue;
            if (te.th32ThreadID == selfThread) continue;
            if (slot >= 192) break;

            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;

            ThreadFrames& tf = g_frames[slot];
            tf.tid = te.th32ThreadID;
            tf.rip = 0;
            tf.count = 0;

            SuspendThread(th);
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(th, &ctx)) {
                tf.rip = (uintptr_t)ctx.Rip;
                uintptr_t sp = (uintptr_t)ctx.Rsp;
                for (int i = 0; i < 900 && tf.count < 24; ++i) {
                    uintptr_t value = 0;
                    SIZE_T read = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), (void*)(sp + i * 8), &value,
                                           sizeof(value), &read) ||
                        read != sizeof(value)) {
                        break;
                    }
                    for (int m = 0; m < rangeCount; ++m) {
                        if (value >= ranges[m].base && value < ranges[m].base + ranges[m].size) {
                            tf.offset[tf.count] = value - ranges[m].base;
                            snprintf(tf.owner[tf.count], sizeof(tf.owner[tf.count]), "%s", ranges[m].name);
                            tf.count++;
                            break;
                        }
                    }
                }
            }
            ResumeThread(th);
            CloseHandle(th);
            slot++;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    logLine("=== stack dump: %d modules, %d threads ===", rangeCount, slot);
    for (int i = 0; i < slot; ++i) {
        const ThreadFrames& tf = g_frames[i];
        char line[1200];
        int len = snprintf(line, sizeof(line), "T%lu rip=%p:", tf.tid, (void*)tf.rip);
        for (int f = 0; f < tf.count && len < (int)sizeof(line) - 80; ++f) {
            int used = snprintf(line + len, sizeof(line) - len, " %s+0x%llX", tf.owner[f],
                                (unsigned long long)tf.offset[f]);
            if (used > 0) len += used;
        }
        // Re-resolve the offset against the module base for readability.
        logLine("%s", line);
    }
    logLine("=== stack dump end ===");
}

// INSERT is the documented hotkey; F1 is accepted as a second one because half the community
// expects it. Both are edge-triggered so holding the key does not flicker the menu.
void pollHotkeys() {
    static int cursorRefs = 0;
    const int count = (int)(sizeof(g_hotkeys) / sizeof(g_hotkeys[0]));
    for (int i = 0; i < count; ++i) {
        bool down = (GetAsyncKeyState(g_hotkeys[i]) & 0x8000) != 0;
        if (down && !g_hotkeyDown[i]) {
            if (g_hotkeys[i] == VK_INSERT || g_hotkeys[i] == VK_F1) {
                menu::toggle();
                logLine("hotkey: menu %s", menu::isOpen() ? "opened" : "closed");
                if (menu::isOpen()) {
                    ClipCursor(nullptr);
                    if (cursorRefs++ == 0) {
                        ShowCursor(TRUE);
                        logLine("menu opened: cursor released, game mouse input muted");
                    }
                } else {
                    if (cursorRefs > 0 && --cursorRefs == 0) {
                        ShowCursor(FALSE);
                        logLine("menu closed: input handed back to the game");
                    }
                }
            } else if (g_hotkeys[i] == VK_F2) {
                logLine("hotkey: F2 stack dump requested");
                dumpThreadStacks();
            }
        }
        g_hotkeyDown[i] = down;
    }
}

HRESULT WINAPI hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    static bool failed = false;

    if (!g_imguiReady && !failed) {
        initImGui(swap);
        if (!g_imguiReady) failed = true;
    }

    if (g_imguiReady) {
        if (!g_rtv) createRenderTarget(swap);

        pollHotkeys();
        feedInput();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        menu::draw();
        menu::drawHud();

        ImGui::Render();
        if (g_rtv && g_context) {
            g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }

    const HRESULT hr = g_origPresent(swap, sync, flags);

    // Locking the session, turning the monitor off or a driver reset makes the swapchain return
    // DEVICE_LOST / DEVICE_REMOVED. Rendering into the dead device on the next frame is what turns
    // that into a crash, so drop every device object and let the next Present rebuild them.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
        static int lostCount = 0;
        if (lostCount < 20) {
            logLine("Present returned 0x%08lX - device lost, rebuilding (count=%d)",
                    (unsigned long)hr, ++lostCount);
        } else {
            lostCount++;
        }
        if (g_rtv) {
            g_rtv->Release();
            g_rtv = nullptr;
        }
        if (g_context) {
            g_context->ClearState();
        }
        return hr;
    }

    return hr;
}

HRESULT WINAPI hkResizeBuffers(IDXGISwapChain* swap, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                               UINT flags) {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    return g_origResize(swap, count, w, h, fmt, flags);
}

// ---------------------------------------------------------------- message box probe
//
// The client reports its own failures through a Win32 message box ("The client is damaged ...").
// Hooking it does two things: the dialog stops blocking the login form, and the log records the
// caller's module+offset, which is where the real check lives.
using MessageBoxWFn = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
using MessageBoxAFn = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);

MessageBoxWFn g_origMessageBoxW = nullptr;
MessageBoxAFn g_origMessageBoxA = nullptr;

void logCaller(const char* what, const wchar_t* text, void* caller) {
    HMODULE mod = nullptr;
    char path[MAX_PATH] = {0};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)caller, &mod)) {
        GetModuleFileNameA(mod, path, MAX_PATH);
    }
    const char* name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    char narrow[512] = {0};
    if (text) WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    logLine("%s caller=%s+0x%llX text=%s", what, name,
            (unsigned long long)((uintptr_t)caller - (uintptr_t)mod), narrow);
}

int WINAPI hkMessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    logCaller("MessageBoxW", text, __builtin_return_address(0));
    return IDOK;
}

int WINAPI hkMessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    wchar_t wide[512] = {0};
    if (text) MultiByteToWideChar(CP_ACP, 0, text, -1, wide, 511);
    logCaller("MessageBoxA", wide, __builtin_return_address(0));
    return IDOK;
}

void hookMessageBox() {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (!user32) return;
    void* w = (void*)GetProcAddress(user32, "MessageBoxW");
    void* a = (void*)GetProcAddress(user32, "MessageBoxA");
    if (w && MH_CreateHook(w, (void*)&hkMessageBoxW, (void**)&g_origMessageBoxW) == MH_OK) {
        MH_EnableHook(w);
        logLine("MessageBoxW hooked");
    }
    if (a && MH_CreateHook(a, (void*)&hkMessageBoxA, (void**)&g_origMessageBoxA) == MH_OK) {
        MH_EnableHook(a);
        logLine("MessageBoxA hooked");
    }
}

void hookSwapChain() {
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "GS71_DUMMY";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, "GS71_DUMMY", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr,
                                nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swap = nullptr;
    D3D_FEATURE_LEVEL level;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &swap,
        &dev, &level, &ctx);
    if (FAILED(hr) || !swap) {
        logLine("dummy swapchain creation failed: 0x%08lX", (unsigned long)hr);
        if (hwnd) DestroyWindow(hwnd);
        return;
    }

    void** vtable = *(void***)swap;
    void* present = vtable[8];
    void* resize = vtable[13];

    MH_Initialize();
    MH_CreateHook(present, (void*)&hkPresent, (void**)&g_origPresent);
    MH_EnableHook(present);

    if (MH_CreateHook(resize, (void*)&hkResizeBuffers, (void**)&g_origResize) == MH_OK) {
        MH_EnableHook(resize);
    }

    logLine("present hook installed at %p", present);

    swap->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassA("GS71_DUMMY", GetModuleHandleA(nullptr));
}

DWORD WINAPI stackProbeThread(LPVOID) {
    // Diagnostics only: runs when "stackprobe.on" exists next to the game executable.
    // Injected keys never reach GetAsyncKeyState in this game (RawInput grab), so the probe is on a
    // timer instead of a hotkey: once shortly after the login page shows up, once after the
    // client's own "damaged" verdict fires.
    char dir[MAX_PATH];
    GetModuleFileNameA(g_self, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (!slash) return 0;
    *slash = 0;
    char flag[MAX_PATH];
    snprintf(flag, sizeof(flag), "%s\\stackprobe.on", dir);
    if (GetFileAttributesA(flag) == INVALID_FILE_ATTRIBUTES) return 0;

    Sleep(22000);
    logLine("auto stack dump #1 (login page up)");
    dumpThreadStacks();
    Sleep(9000);
    logLine("auto stack dump #2 (after the damage verdict)");
    dumpThreadStacks();
    return 0;
}

// ---------------------------------------------------------------- channel probe
//
// The client logs "SearchPathW() failed for play_pc_sdk.dll ... No compatible channel detected"
// and then refuses to log in, claiming the client is damaged. It searches an explicit directory
// that is not the game folder, so hooking SearchPathW both reveals that directory and lets us hand
// the client a channel DLL of our own.
using SearchPathWFn = DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPWSTR, LPWSTR*);
SearchPathWFn g_origSearchPathW = nullptr;

std::wstring g_stubPath;

DWORD WINAPI hkSearchPathW(LPCWSTR lpPath, LPCWSTR lpFileName, LPCWSTR lpExtension,
                           DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart) {
    if (lpPath && lpFileName) {
        logLine("SearchPathW path=%ls file=%ls", lpPath, lpFileName);
    }

    if (lpFileName && wcsstr(lpFileName, L"play_pc_sdk") && !g_stubPath.empty()) {
        const size_t need = g_stubPath.size();
        if (lpBuffer && nBufferLength > need) {
            memcpy(lpBuffer, g_stubPath.c_str(), (need + 1) * sizeof(wchar_t));
            if (lpFilePart) {
                // Point at the file name inside the path instead of assuming a fixed length - the
                // old "need - 12" underflowed for short paths and was off by three characters.
                const size_t slash = g_stubPath.find_last_of(L"\\/");
                const size_t nameOff = (slash == std::wstring::npos) ? 0 : slash + 1;
                *lpFilePart = (nameOff < need) ? lpBuffer + nameOff : nullptr;
            }
            logLine("SearchPathW -> forced stub %ls", g_stubPath.c_str());
            return (DWORD)need;
        }
    }

    return g_origSearchPathW(lpPath, lpFileName, lpExtension, nBufferLength, lpBuffer, lpFilePart);
}

void hookSearchPath() {
    char dir[MAX_PATH];
    GetModuleFileNameA(g_self, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (!slash) return;
    *slash = 0;
    char stub[MAX_PATH];
    snprintf(stub, sizeof(stub), "%s\\play_pc_sdk.dll", dir);
    if (GetFileAttributesA(stub) == INVALID_FILE_ATTRIBUTES) {
        logLine("no play_pc_sdk.dll stub next to the game, channel probe disabled");
        return;
    }
    wchar_t wide[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, stub, -1, wide, MAX_PATH);
    g_stubPath = wide;

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void* sp = k32 ? (void*)GetProcAddress(k32, "SearchPathW") : nullptr;
    if (sp && MH_CreateHook(sp, (void*)&hkSearchPathW, (void**)&g_origSearchPathW) == MH_OK) {
        MH_EnableHook(sp);
        logLine("SearchPathW hooked, stub = %s", stub);
    }
}

// ---------------------------------------------------------------- hard-error suppressor
//
// The SDK reports "The client is damaged ... Error Code:(2,0,4)" through NtRaiseHardError: the
// dialog window is owned by csrss, MessageBoxW is never called, and it re-appears every few
// seconds. Swallowing the call (log it, return STATUS_SUCCESS, answer "Ok") keeps the dialog from
// stealing the keyboard from the login form.
using NtRaiseHardErrorFn = LONG(NTAPI*)(LONG, ULONG, ULONG, ULONG_PTR*, ULONG, ULONG*);
NtRaiseHardErrorFn g_origRaiseHardError = nullptr;

LONG NTAPI hkRaiseHardError(LONG status, ULONG number, ULONG mask, ULONG_PTR* params,
                            ULONG options, ULONG* response) {
    char extra[256] = {0};
    if (params && number > 0) {
        // Parameters[0] is usually a UNICODE_STRING or a NUL-terminated wide string.
        const wchar_t* maybe = (const wchar_t*)params[0];
        if (maybe && ((uintptr_t)maybe >> 16) != 0) {
            WideCharToMultiByte(CP_UTF8, 0, maybe, -1, extra, sizeof(extra) - 1, nullptr, nullptr);
        }
    }
    logLine("NtRaiseHardError suppressed: status=0x%08lX params=%lu first=%s", (unsigned long)status,
            (unsigned long)number, extra);
    if (response) *response = 1;  // ResponseOk
    return 0;                      // STATUS_SUCCESS
}

void hookHardError() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;
    void* fn = (void*)GetProcAddress(ntdll, "NtRaiseHardError");
    if (!fn) {
        logLine("NtRaiseHardError not found");
        return;
    }
    if (MH_CreateHook(fn, (void*)&hkRaiseHardError, (void**)&g_origRaiseHardError) == MH_OK) {
        MH_EnableHook(fn);
        logLine("NtRaiseHardError hooked (hard-error dialogs suppressed)");
    } else {
        logLine("failed to hook NtRaiseHardError");
    }
}

// ---------------------------------------------------------------- hard-error dialog watchdog
//
// Not every raise goes through the ntdll export (some use a direct syscall stub), so a second line
// of defence: the dialog is always a csrss-owned window of class "#" at a fixed size. Close it the
// moment it shows up, which keeps it from holding the keyboard for more than ~150 ms.
BOOL CALLBACK dialogWatchdogProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);

    RECT r;
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w < 200 || w > 600 || h < 100 || h > 350) return TRUE;

    // The hard-error dialog is a csrss window of class "#" (possibly "#32770"). csrss cannot be
    // opened even as admin, so the class name is the reliable handle here.
    static HWND lastClosed = nullptr;
    if (cls[0] != L'#') return TRUE;

    char clsA[64] = {0};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, clsA, sizeof(clsA) - 1, nullptr, nullptr);
    if (hwnd != lastClosed) {
        lastClosed = hwnd;
        logLine("closing hard-error dialog hwnd=%p class='%s' %dx%d", (void*)hwnd, clsA, w, h);
    }

    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

DWORD WINAPI dialogWatchdog(LPVOID) {
    logLine("hard-error dialog watchdog running");
    bool paused = false;
    for (;;) {
        // The hard error only fires while the login flow is failing. Once a player is in the world
        // the dialog is gone for good, so stop enumerating windows - that removes the watchdog (and
        // its cross-process window calls) from the game loop entirely.
        const bool online = bridge::get().online;
        if (online) {
            if (!paused) {
                logLine("player online - dialog watchdog paused");
                paused = true;
            }
            Sleep(2000);
            continue;
        }
        if (paused) {
            logLine("player offline - dialog watchdog resumed");
            paused = false;
        }
        EnumWindows(dialogWatchdogProc, 0);
        Sleep(1000);
    }
    return 0;
}

// ---------------------------------------------------------------- FPS unlock
//
// The client keeps its frame-rate limit in a global int32 inside the module's "il2cpp" section.
// The address is located the same way genshin-fps-unlock does it: find "mov ecx,60 ; call", follow
// the call/jmp thunk chain, then read the RIP-relative displacement of the store that follows.
// Writing that int every ~60 ms overrides the 30/60 limit the settings menu offers.
int g_fpsTarget = 144;
static bool g_fpsApply = false;   // flipped on once the true cap field is confirmed
static int32_t* g_pFrameRate = nullptr;
static int32_t* g_fpsCandidates[8] = {nullptr};
static int g_fpsCandidateCount = 0;

static uint8_t* findIl2cppSection(size_t& size) {
    uint8_t* base = (uint8_t*)GetModuleHandleA(nullptr);
    if (!base) return nullptr;
    auto dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        char name[9] = {0};
        memcpy(name, sec->Name, 8);
        if (!strcmp(name, "il2cpp")) {
            size = sec->Misc.VirtualSize;
            return base + sec->VirtualAddress;
        }
    }
    return nullptr;
}

static bool setupFpsUnlock() {
    uint8_t* base = (uint8_t*)GetModuleHandleA(nullptr);
    if (!base) return false;
    auto dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    int logged = 0;
    uint8_t* found = nullptr;

    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int s = 0; s < nt->FileHeader.NumberOfSections && !found; s++, sec++) {
        char sname[9] = {0};
        memcpy(sname, sec->Name, 8);
        uint8_t* p = base + sec->VirtualAddress;
        const size_t n = sec->Misc.VirtualSize;

        for (size_t i = 0; i + 16 < n; i++) {
            uint8_t* rip = p + i;
            int32_t* target = nullptr;

            if (rip[0] == 0xC7 && rip[1] == 0x05) {                 // mov dword [rip+d], imm32
                int32_t imm = *(int32_t*)(rip + 6);
                if (imm != 30 && imm != 60 && imm != 120 && imm != 144) continue;
                target = (int32_t*)(rip + 10 + *(int32_t*)(rip + 2));
            } else if (rip[0] == 0x89 && (rip[1] & 0xC7) == 0x05) {  // mov [rip+d], r32
                target = (int32_t*)(rip + 6 + *(int32_t*)(rip + 2));
            } else {
                continue;
            }

            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(target, &mbi, sizeof(mbi))) continue;
            const bool writable = (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY ||
                                   mbi.Protect == PAGE_EXECUTE_READWRITE ||
                                   mbi.Protect == PAGE_EXECUTE_WRITECOPY);
            if (!writable) continue;

            const int v = *target;
            if (v == 30 || v == 60 || v == 120 || v == 144 || v == 165) {
                if (logged < 30) {
                    logLine("fps dbg: sec=%s store=%p target=%p value=%d enc=%02X%02X", sname,
                            (void*)rip, (void*)target, v, rip[0], rip[1]);
                    logged++;
                }
                if (g_fpsCandidateCount < 8) g_fpsCandidates[g_fpsCandidateCount++] = target;
            }
        }
    }

    if (g_fpsCandidateCount > 0) {
        g_pFrameRate = g_fpsCandidates[0];
        logLine("fps unlock: %d candidate field(s), keeping all in sync", g_fpsCandidateCount);
        return true;
    }
    logLine("fps unlock: nothing found (logged %d)", logged);
    return false;
}
DWORD WINAPI fpsUnlockThread(LPVOID) {
    if (!setupFpsUnlock()) return 0;
    logLine("fps unlock: holding target %d", g_fpsTarget);
    for (;;) {
        // Watch-only: log when a candidate changes value. Writing blindly killed the client, so the
        // real cap field has to be identified first (change the in-game FPS 30 <-> 60 and see which
        // one moves). Set g_fpsApply=true only once the field is confirmed.
        static int lastVals[8] = {-999, -999, -999, -999, -999, -999, -999, -999};
        for (int i = 0; i < g_fpsCandidateCount; i++) {
            if (!g_fpsCandidates[i]) continue;
            int cur = *g_fpsCandidates[i];
            if (cur != lastVals[i]) {
                logLine("fps watch [%d] %p: %d -> %d", i, (void*)g_fpsCandidates[i], lastVals[i], cur);
                lastVals[i] = cur;
            }
        }
        static bool applyOnce = false;
        if (!applyOnce && g_fpsApply) {
            applyOnce = true;
            for (int i = 0; i < g_fpsCandidateCount; i++)
                if (g_fpsCandidates[i]) *g_fpsCandidates[i] = g_fpsTarget;
            logLine("fps unlock: applied %d to all candidates", g_fpsTarget);
        }
        Sleep(62);
    }
    return 0;
}

// ---------------------------------------------------------------- crash dump
//
// The client's own crash reporter only prints a symbol-less stack. When anything faults while our
// code is loaded we write a minidump next to Astrolabe.log plus the exception code and the faulting
// instruction, which is what actually pinpoints the bug.
using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
using MiniDumpCallbackFn = BOOL(WINAPI*)(void*, void*, void*);

LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    logLine("CRASH exception=0x%08lX at %p", (unsigned long)code, addr);

    char path[MAX_PATH];
    GetModuleFileNameA(g_self, path, MAX_PATH);
    char* dot = strrchr(path, '.');
    if (dot) *dot = 0;
    lstrcatA(path, ".crash.dmp");

    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    if (dbghelp) {
        auto write = (MiniDumpWriteDumpFn)GetProcAddress(dbghelp, "MiniDumpWriteDump");
        if (write) {
            HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                // MiniDumpNormal (0) with a full memory range is enough to unwind our frames.
                if (write(GetCurrentProcess(), GetCurrentProcessId(), file, 0, nullptr, nullptr,
                          nullptr)) {
                    logLine("minidump written: %s", path);
                } else {
                    logLine("minidump failed (%lu)", GetLastError());
                }
                CloseHandle(file);
            }
        }
        FreeLibrary(dbghelp);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------- vectored exception log
//
// The SDK installs its own UnhandledExceptionFilter, which replaces ours and is why the last crash
// produced no dump. A vectored handler runs *before* SEH and cannot be displaced, so it logs the
// exception code, the faulting address and a short stack walk (module+offset for each frame).
static void describeAddr(void* addr, char* out, size_t cap) {
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) &&
        mod) {
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* slash = strrchr(path, '\\');
        const char* name = slash ? slash + 1 : path;
        snprintf(out, cap, "%s+0x%llX", name, (unsigned long long)((uintptr_t)addr - (uintptr_t)mod));
    } else {
        snprintf(out, cap, "%p", addr);
    }
}

LONG CALLBACK vehLog(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    // ignore the noisy first-chance ones the runtime raises on purpose
    if (code == 0xE06D7363 /* C++ EH */ || code == 0x406D1388 /* thread name */ ||
        code == 0x80000003 /* breakpoint */ || code == 0x40010006 /* OutputDebugString */ ||
        code == 0x4001000A /* OutputDebugStringW */) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char where[160];
    describeAddr(ep->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    logLine("VEH exception=0x%08lX at %s (thread %lu)", (unsigned long)code, where,
            (unsigned long)GetCurrentThreadId());

    // ---- 方案 A：跳过 SDK 那个 VM 派发表的非法读取 ----
    // 崩溃指令：0x17112B  movzx eax, byte ptr [rdi+rbx]   （表基址/索引非法）
    // 处理：把 rax 置 0（派发索引归零，落到合法入口），RIP += 3 跳过这条指令，继续执行。
    if (code == 0xC0000005 && ep->ContextRecord) {
        HMODULE mhyp = GetModuleHandleA("mhypbase.dll");
        if (mhyp) {
            const uintptr_t fault = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
            const uintptr_t base = (uintptr_t)mhyp;
            if (fault == base + 0x17112B) {
                ep->ContextRecord->Rax = 0;
                ep->ContextRecord->Rip = fault + 4;   // movzx eax,[rdi+rbx] is 0f b6 04 1f = 4 bytes
                static int skipped = 0;
                if (skipped < 20) {
                    logLine("VEH skip mhypbase+0x17112B (dispatch table read), rax=0 rip+=4 (#%d)",
                            ++skipped);
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }

    void* frames[10] = {0};
    const USHORT n = RtlCaptureStackBackTrace(1, 10, frames, nullptr);
    for (USHORT i = 0; i < n; i++) {
        char d[160];
        describeAddr(frames[i], d, sizeof(d));
        logLine("   [%u] %s", (unsigned)i, d);
    }
    return EXCEPTION_CONTINUE_SEARCH;  // let the SDK/CRT handle it as before
}

DWORD WINAPI boot(LPVOID) {
    SetUnhandledExceptionFilter(crashFilter);
    AddVectoredExceptionHandler(1, vehLog);
    logLine("overlay booting (crash filter armed)");
    createRawInputWindow();
    installRawInputHook();

    // Windows 10 + DPI: keep the ImGui coordinates in real pixels.
    SetProcessDPIAware();

    loadPatchSibling();

    bridge::start();
    hookSwapChain();
    hookMessageBox();
    hookSearchPath();
    // hookHardError();  // swallowing the raise makes the SDK spin; the watchdog closes the window instead
    CreateThread(nullptr, 0, dialogWatchdog, nullptr, 0, nullptr);
    {
        // The FPS probe scans every section of the (444 MB) client and touches candidate globals;
        // it is opt-in while the true frame-rate field is still being identified.
        char dir[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, dir, MAX_PATH);
        char* slash = strrchr(dir, '\\');
        if (slash) {
            *slash = 0;
            char flag[MAX_PATH];
            snprintf(flag, sizeof(flag), "%s\\fpsprobe.on", dir);
            if (GetFileAttributesA(flag) != INVALID_FILE_ATTRIBUTES) {
                // DISABLED: the in-process scan trips the client anti-tamper. Frame-rate unlocking
                // is handled by the dedicated external tool instead (see 新电脑部署.md §11).
                logLine("fps probe requested but disabled in this build");
            }
        }
    }
    CreateThread(nullptr, 0, stackProbeThread, nullptr, 0, nullptr);

    logLine("overlay ready");
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        CreateThread(nullptr, 0, boot, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        bridge::stop();
    }
    return TRUE;
}
