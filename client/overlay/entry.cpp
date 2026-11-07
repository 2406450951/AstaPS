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
#include <vector>
#include <mutex>

#include "MinHook.h"
#include "bridge.h"
#include "menu.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);


void autoProbeTypesStart();
DWORD WINAPI worldAvatarProbeThread(LPVOID);
DWORD WINAPI scanNowWatcher(LPVOID);
DWORD WINAPI bpProbeFromFlag(LPVOID);
DWORD WINAPI bpFromFileThread(LPVOID);
DWORD WINAPI pairProbeThread(LPVOID);
DWORD WINAPI strictClassThread(LPVOID);
DWORD WINAPI layoutProbeThread(LPVOID);
void Gs71DumpMetadata();
struct SizeSlot { unsigned int size; unsigned long long count; };
static SizeSlot g_sizes[12] = {};
static SRWLOCK g_sizeLock = SRWLOCK_INIT;
using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
PresentFn g_origPresent = nullptr;
ResizeBuffersFn g_origResize = nullptr;
HWND g_hwnd = nullptr;

// ---- 输入门控 --------------------------------------------------------------
// ��有本进程的窗口在前台时才认按键，不做全局快捷键
bool gs71InputFocused() {
    const HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (g_hwnd && fg == g_hwnd) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
#define GetAsyncKeyState(vk) (gs71InputFocused() ? ::GetAsyncKeyState(vk) : 0)
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_imguiReady = false;
bool g_bridgeStarted = false;
HMODULE g_self = nullptr;
const int g_hotkeys[] = {VK_INSERT, VK_F1, VK_F2, VK_F3, VK_HOME, VK_END};
bool g_hotkeyDown[6] = {false};
void bpLogHitRegs(CONTEXT* c);
DWORD WINAPI diffScanThread(LPVOID);
DWORD WINAPI dumpAddrsFromFile(LPVOID);
DWORD WINAPI posWriteThread(LPVOID);
DWORD WINAPI xrefProbeThread(LPVOID);
DWORD WINAPI dumpMappedMetadata(LPVOID);
void installMetadataHooks();
DWORD WINAPI huntMhyBuffer(LPVOID);
DWORD WINAPI findCollisionHelper(LPVOID);
DWORD WINAPI collIgnoreThread(LPVOID);
DWORD WINAPI collDisableAllThread(LPVOID);
void installPhysicsNoHitHooks(bool enable);
DWORD WINAPI scanMultiClasses(LPVOID);
DWORD WINAPI planAThread(LPVOID);
DWORD WINAPI findPlayerActor(LPVOID);
DWORD WINAPI findPlayerMoveInst(LPVOID);
void installCounterStubs(bool enable);
void dumpCounters();
void installCounterStubsV2(bool enable);
void dumpCountersV2();
void patchSoloMoveCheck(bool enable);
void patchSolo2Check(bool enable);
void patchWrap1345(bool enable);
void patchWrap823(bool enable);
void patchSolo3Check(bool enable);
void installCtorCapture(bool enable);
void dumpCtorCaptures();
void installTransformProbe(bool enable);
void dumpTransformProbe();
void installPlayerTransformHook(bool enable);
void reportPlayerTransform();
DWORD WINAPI findPlayerTransform(LPVOID);
DWORD WINAPI toggleRigidbodyCollisions(LPVOID);
void installFsmHooks(bool enable);
void reportFsm();
DWORD WINAPI rbdFromFileThread(LPVOID);
DWORD WINAPI fsmFieldsThread(LPVOID);
DWORD WINAPI fsmMaskThread(LPVOID);
DWORD WINAPI noclipAtomicThread(LPVOID);
DWORD WINAPI fsmMaskScanThread(LPVOID);
DWORD WINAPI findTransformOffsetThread(LPVOID);
DWORD WINAPI holdKeyThread(LPVOID);
void installMmoronProbe(bool enable);
void reportMmoron();
void installStackProbe(bool enable);
void reportStackProbe();
DWORD WINAPI findEntityByPosThread(LPVOID);
DWORD WINAPI apiProbeThread(LPVOID);
void installFsmCtorCapture(bool enable);
void reportFsmTransforms();
void installGetPositionHook(bool enable);
void reportGetPosition();
DWORD WINAPI zeroVelocityThread(LPVOID);
void installVchmCtorCapture();
void findPlayerVchm();
DWORD WINAPI classWalkThread(LPVOID);

std::atomic<bool> g_bpArmed{false};

using FsmTickFn = void (*)(void* self, float dt, void* mi);
static FsmTickFn g_origFsmA = nullptr;
static FsmTickFn g_origFsmB = nullptr;
static volatile long g_fsmCallsA = 0;
static volatile long g_fsmCallsB = 0;
static volatile bool g_fsmSkip = false;
static bool g_fsmHooked = false;
static volatile bool g_vel0On = false;

// 主线程任务：IL2CPP  API 必须在游戏主线程（渲染线程）调用，否则会
static volatile bool g_mainMovePending = false;

static volatile bool g_mainGotoPending = false;
static volatile int g_mainMoveVariant = 0;   // 0=MoveTo(1,0) 1=MoveTo(0,0) 2=GotoPoint

// 每帧飞行的状
static volatile bool g_flyLoopOn = false;
static volatile int g_flyIsWalk = 0;

// ---- 第一批功能（主线 Unity API 调用；FPS 已有独立实现，这里不重复 ---
static volatile int g_fogOp = 0;            // 1=关雾 2=开
static volatile float g_timeScaleOp = 0.f;  // 0 = 不改
static volatile float g_fovOp = 0.f;        // 0 = 不改

// ---- 第二批：字符串构 / 隐藏UI / 世界投影 ESP / 原生飞行 ----
static volatile int g_uiProbeReq = 0;
static volatile int g_hideUiOp = 0;         // 1=隐藏 2=恢复
static void* g_uiRoot = nullptr;            // 找到并操作过 UI 根节
static void* g_strKlass = nullptr;          // System.String 的类指针
static volatile int g_cutsceneSkip = 0;   // 1 = 自动跳过过场（CriwareMediaPlayer::Skip）
static volatile int g_flyMode = 0;          // 0= 1=原生飞行状 2=速度覆写
static volatile int g_flyNoClip = 0;
static volatile int g_flyFlags = 0x7;       // bit0 forceAirStateFly bit1 up/down bit2 重力 bit3 忽略重力
static volatile float g_flySpeed = 6.0f;
static volatile float g_flyDescend = 2.5f;   // 慢降速度 m/s    // 速度覆写模式下的水平速度 m/s
static volatile int g_projFailLogged = 0;



static float g_mainMovePos[3] = {0, 0, 0};
static float g_mainMoveLen = 3.0f;
void mainThreadTick();


using VchmCtorFn = void (*)(void* self, void* mi);
static VchmCtorFn g_origVchmCtor = nullptr;
static void* g_vchmInst[512] = {};
static volatile long g_vchmInstCount = 0;

namespace {
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


// ---------------------------------------------------------------- 输入隔离
//
// 菜单打开时，鼠标点击必须只给 ImGui，不能落到游戏里（否则点按钮就等于攻击一下）
// 我们自己的输入是轮询 GetAsyncKeyState/GetCursorPos，所以直接把窗口消息吞掉即可
WNDPROC g_origWndProc = nullptr;

LRESULT CALLBACK hkWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (menu::isOpen()) {
        switch (msg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                return 0;
            default:
                break;
        }
    }
    return CallWindowProcW(g_origWndProc, h, msg, wp, lp);
}


// ---------------------------------------------------------------- 相机探测（只读）
//
// 灵魂出窍要先把相机在内存里定位出来。这里只做读操作
//   第一步：在可读写堆里找跟角色坐标一致（±0.8）的
// float 三元组，记下地址
//   第二步：玩家只转视角不移动时再读一遍，值变了的那些就是相机/镜头相关
// 全程 ReadProcessMemory 读自己进程，不直接解引用，遇到不可读页直接跳过
struct CamCand {
    uint8_t* addr;
    float v[3];
    float dist;   // 采集时到角色的距
};

const int kCamMax = 4096;
CamCand g_camCands[kCamMax];
int g_camCount = 0;
std::atomic<int> g_camProbeState{0};   // 0=空闲 1=扫描 2=已采 
DWORD WINAPI camProbeThread(LPVOID param) {
    float px = 0, py = 0, pz = 0;
    {
        Snapshot s = bridge::get();
        px = s.px;
        py = s.py;
        pz = s.pz;
    }
    if (px == 0.f && py == 0.f && pz == 0.f) {
        logLine("cam probe: no player position yet (进世界再 ");
        g_camProbeState.store(0);
        return 0;
    }
    logLine("cam probe: scanning for avatar pos %.2f %.2f %.2f", px, py, pz);

    g_camCount = 0;
    static uint8_t buf[1 << 20];
    HANDLE self = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    size_t totalRead = 0;

    MEMORY_BASIC_INFORMATION mbi{};
    while (p < limit && g_camCount < kCamMax) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const DWORD prot = mbi.Protect;
        const bool readable = (mbi.State == MEM_COMMIT) && !(prot & PAGE_GUARD) &&
                              !(prot & PAGE_NOACCESS) &&
                              (prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE ||
                               prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_WRITECOPY ||
                               prot == PAGE_READONLY);
        uint8_t* regBase = (uint8_t*)mbi.BaseAddress;
        size_t regSize = mbi.RegionSize;
        if (readable && regSize > 0) {
            for (size_t off = 0; off + 12 <= regSize && g_camCount < kCamMax;
                 off += sizeof(buf)) {
                size_t chunk = regSize - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(self, regBase + off, buf, chunk, &got) || got < 12)
                    continue;
                totalRead += got;
                for (size_t i = 0; i + 12 <= got; i += 4) {
                    const float* f = (const float*)(buf + i);
                    if (f[0] != f[0] || f[1] != f[1] || f[2] != f[2]) continue;
                    // 相机不在角色身上，在角色后上方几米。所以按"附近 15  收集
// 再用静止噪声过滤把每帧复用的临时缓冲剔掉
if (fabsf(f[0] - px) < 15.f && fabsf(f[1] - py) < 15.f &&
                        fabsf(f[2] - pz) < 15.f) {
                        g_camCands[g_camCount].addr = regBase + off + i;
                        g_camCands[g_camCount].v[0] = f[0];
                        g_camCands[g_camCount].v[1] = f[1];
                        g_camCands[g_camCount].v[2] = f[2];
                        {
                            const float ddx = f[0] - px, ddy = f[1] - py, ddz = f[2] - pz;
                            g_camCands[g_camCount].dist =
                                    sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                        }
                        g_camCount++;
                        if (g_camCount >= kCamMax) break;
                    }
                }
            }
        }
        uint8_t* next = regBase + regSize;
        if (next <= p) break;
        p = next;
    }
    logLine("cam probe: scanned %.1f MB, %d raw candidate(s)",
            (double)totalRead / (1024.0 * 1024.0), g_camCount);

    // 静止噪声过滤：玩家没动，两次采样之间还变的就是临时缓冲，直接踢掉
Sleep(1500);
    int kept = 0;
    const int before = g_camCount;
    for (int i = 0; i < before; ++i) {
        float v[3] = {0, 0, 0};
        SIZE_T got = 0;
        if (!ReadProcessMemory(self, g_camCands[i].addr, v, sizeof(v), &got) || got < 12)
            continue;
        if (fabsf(v[0] - g_camCands[i].v[0]) > 0.05f ||
            fabsf(v[1] - g_camCands[i].v[1]) > 0.05f ||
            fabsf(v[2] - g_camCands[i].v[2]) > 0.05f)
            continue;
        g_camCands[kept++] = g_camCands[i];
    }
    g_camCount = kept;
    logLine("cam probe: %d/%d survived the still-frame filter", kept, before);
    for (int i = 0; i < g_camCount && i < 12; ++i)
        logLine("   cand[%d] %p = %.2f %.2f %.2f", i, g_camCands[i].addr, g_camCands[i].v[0],
                g_camCands[i].v[1], g_camCands[i].v[2]);
    g_camProbeState.store(2);
    return 0;
}

void camProbeStart() {
    if (g_camProbeState.load() == 1) return;
    g_camProbeState.store(1);
    CreateThread(nullptr, 0, camProbeThread, nullptr, 0, nullptr);
}


// 把候选地址周围
// float 全打出来，用来看相机控制器的结构布局
// （找 ~6.2 的距离值、yaw/pitch 角度、以及指向角色的指针 
// ---------------------------------------------------------------- 指针扫描（方 B
//
// 思路：相机控制器为了跟随角色，必然持有指向角色对象的指针
//   4. 锁定角色对象：在已采集候选里 值等于角色坐标、且附近有血量数 的那块，记下地址
//   5. 指针扫描：全堆找等于这个地址（或其内部偏移） 8 字节  持有者列
//   6. 转储持有者：把每个持有者前 128 字节打出来，人工找含 ~6.2 距离和两个角度的结构
uint8_t* g_avatarAnchor = nullptr;
static HMODULE g_selfModule = nullptr;
int g_avatarAnchorOffset = 0;

struct PtrHolder {
    uint8_t* slot;    // 存指针的那个 8 字节位置
    uint8_t* target;  // 指向的地址
};
const int kHolderMax = 1024;
PtrHolder g_holders[kHolderMax];
int g_holderCount = 0;

void camProbeAnchor() {
    if (g_camCount == 0) {
        logLine("anchor: run step 1 first");
        return;
    }
    //  相机位置"当锚点：距离角色 3~12 米、不是方向向量的那些候选
// 然后找谁持有 — 持有者要么是相机控制器，要么是渲染管线（能顺藤摸瓜）
g_avatarAnchor = nullptr;
    for (int i = 0; i < g_camCount; ++i) {
        const float d = g_camCands[i].dist;
        if (d < 3.f || d > 12.f) continue;
        const float* v = g_camCands[i].v;
        if (fabsf(v[0]) < 1.5f && fabsf(v[1]) < 1.5f && fabsf(v[2]) < 1.5f) continue;
        g_avatarAnchor = g_camCands[i].addr;
        logLine("anchor: camera position field at %p  value %.2f %.2f %.2f  dist %.2f",
                g_avatarAnchor, v[0], v[1], v[2], d);
        break;
    }
    if (!g_avatarAnchor) {
        logLine("anchor: no camera-like candidate; run step 1 first");
        return;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(g_avatarAnchor, &mbi, sizeof(mbi)))
        logLine("anchor: region base %p size %llu KB", mbi.BaseAddress,
                (unsigned long long)(mbi.RegionSize / 1024));
}

DWORD WINAPI camPointerScanThread(LPVOID) {
    if (!g_avatarAnchor) {
        logLine("ptrscan: no anchor yet");
        return 0;
    }
    // 接受"指向角色位置字段本身或它前后 0x200 字节 的指
const uint64_t lo = (uint64_t)g_avatarAnchor - 0x80;
    const uint64_t hi = (uint64_t)g_avatarAnchor + 0x80;

    g_holderCount = 0;
    static uint8_t buf[1 << 20];
    HANDLE self = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    size_t readTotal = 0;

    MEMORY_BASIC_INFORMATION mbi{};
    while (p < limit && g_holderCount < kHolderMax) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const DWORD prot = mbi.Protect;
        const bool readable = (mbi.State == MEM_COMMIT) && !(prot & PAGE_GUARD) &&
                              !(prot & PAGE_NOACCESS) &&
                              (prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE ||
                               prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_WRITECOPY ||
                               prot == PAGE_READONLY);
        uint8_t* regBase = (uint8_t*)mbi.BaseAddress;
        size_t regSize = mbi.RegionSize;
        if (readable && regSize > 0) {
            for (size_t off = 0; off < regSize && g_holderCount < kHolderMax; off += sizeof(buf)) {
                size_t chunk = regSize - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(self, regBase + off, buf, chunk, &got) || got < 8) continue;
                readTotal += got;
                for (size_t i = 0; i + 8 <= got; i += 8) {
                    const uint64_t val = *(const uint64_t*)(buf + i);
                    if (val >= lo && val <= hi) {
                        // 跳过自己模块里的命中：g_camCands[] 本身就存着锚点地址，会自我命中
                        HMODULE hitMod = nullptr;
                        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                               (LPCSTR)(regBase + off + i), &hitMod) &&
                            hitMod == g_selfModule)
                            continue;
                        g_holders[g_holderCount].slot = regBase + off + i;
                        g_holders[g_holderCount].target = (uint8_t*)val;
                        g_holderCount++;
                        if (g_holderCount >= kHolderMax) break;
                    }
                }
            }
        }
        uint8_t* next = regBase + regSize;
        if (next <= p) break;
        p = next;
    }
    logLine("ptrscan: read %.1f MB, %d holder(s) point at avatar %p", (double)readTotal / 1048576.0,
            g_holderCount, g_avatarAnchor);
    for (int i = 0; i < g_holderCount && i < 12; ++i)
        logLine("   holder[%d] %p -> %p", i, g_holders[i].slot, g_holders[i].target);
    return 0;
}

void camProbePointerScan() { CreateThread(nullptr, 0, camPointerScanThread, nullptr, 0, nullptr); }

void camProbeDumpHolders() {
    if (g_holderCount == 0) {
        logLine("holder dump: run step 5 first");
        return;
    }
    HANDLE self = GetCurrentProcess();
    logLine("holder dump: main module base %p", (void*)GetModuleHandleA(nullptr));

    int shown = 0;
    for (int i = 0; i < g_holderCount && shown < 3; ++i) {
        uint8_t* slot = g_holders[i].slot;

        // 找到这个地址属于哪个模块（栈/堆上的会返回失败
HMODULE owner = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)slot, &owner) ||
            !owner) {
            continue;   // 不在任何模块里，跳过（栈拷贝
}
        if (owner == g_selfModule) continue;   // 自己的数组，忽略
        char modPath[MAX_PATH] = {0};
        GetModuleFileNameA(owner, modPath, MAX_PATH);
        const char* modName = strrchr(modPath, '\\');
        modName = modName ? modName + 1 : modPath;

        shown++;
        logLine("== static holder in %s (base %p)", modName, (void*)owner);
        logLine("   slot %p = RVA 0x%llX -> target %p = RVA 0x%llX", slot,
                (unsigned long long)(slot - (uint8_t*)owner), g_holders[i].target,
                (unsigned long long)(g_holders[i].target - (uint8_t*)owner));

        static uint8_t raw[2048];
        uint8_t* startp = slot - 512;
        SIZE_T got = 0;
        if (!ReadProcessMemory(self, startp, raw, sizeof(raw), &got) || got < sizeof(raw)) {
            logLine("   (read failed)");
            continue;
        }
        for (int row = 0; row < 64; ++row) {
            char line[256];
            int n = 0;
            for (int c = 0; c < 8; ++c) {
                const float fv = *(const float*)(raw + row * 32 + c * 4);
                n += snprintf(line + n, sizeof(line) - n, "%11.3f", fv);
                if (n > 200) break;
            }
            logLine("   %+05X:%s", row * 32 - 512, line);
        }
    }
    logLine("holder dump: done (%d module-resident structure(s))", shown);
}


// ---------------------------------------------------------------- 硬件断点探针（只读）
//
// 相机位置字段没有任何长期引用（指针扫 0 持有者），所以反过来 谁写 
// 在该地址上设**写断 *（DR0/DR7），写发生时 CPU  0x80000004，VEH 记下 RIP
// 然后立刻把所有线程上的断点撤掉。全程不修改任何游戏数据
static std::atomic<int> g_bpHits{0};
static uintptr_t g_bpAddr = 0;

// 对所有线程设 清除 DR0+DR7。返回成功设置的线程数
static int bpApplyAllThreads(uintptr_t addr) {
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int done = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                           THREAD_SET_CONTEXT,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            if (SuspendThread(th) == (DWORD)-1) {
                CloseHandle(th);
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &ctx)) {
                if (addr) {
                    ctx.Dr0 = addr;
                    // L0=1(局部使  | RW0=01(  | LEN0=11(4字节)
                    ctx.Dr7 = (ctx.Dr7 & ~0xF0003ULL) | 0x0001 | (0x1ULL << 16) | (0x3ULL << 18);
                } else {
                    ctx.Dr0 = 0;
                    ctx.Dr7 = ctx.Dr7 & ~0xF0003ULL;
                }
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) done++;
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return done;
}

// 记录命中 RIP 和返回地址链（模块 + RVA
static void bpLogHit(CONTEXT* ctx) {
    const int n = g_bpHits.fetch_add(1);
    if (n >= 6) return;

    HMODULE m = nullptr;
    char name[64] = "?";
    unsigned long long rva = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)ctx->Rip, &m) &&
        m) {
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* bn = strrchr(path, '\\');
        snprintf(name, sizeof(name), "%s", bn ? bn + 1 : path);
        rva = (unsigned long long)(ctx->Rip - (uintptr_t)m);
    }
    logLine("bp hit #%d: writing instruction %s+0x%llX (rip=%p)", n + 1, name, rva,
            (void*)ctx->Rip);

    // 栈回溯： Rsp 往 8  qword 逐个映射 模块+偏移"
    uint64_t frames[8] = {0};
    SIZE_T got = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (void*)ctx->Rsp, frames, sizeof(frames), &got) &&
        got == sizeof(frames)) {
        for (int i = 0; i < 8; ++i) {
            if (!frames[i]) continue;
            HMODULE fm = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)frames[i], &fm) &&
                fm) {
                char fp[MAX_PATH] = {0};
                GetModuleFileNameA(fm, fp, MAX_PATH);
                const char* fb = strrchr(fp, '\\');
                logLine("   stack[%d] %s+0x%llX", i, fb ? fb + 1 : fp,
                        (unsigned long long)(frames[i] - (uintptr_t)fm));
            }
        }
    }
}

DWORD WINAPI bpWorker(LPVOID) {
    if (!g_avatarAnchor) {
        logLine("bp: no camera anchor yet (先做 1   ");
        g_bpArmed.store(false);
        return 0;
    }
    g_bpAddr = (uintptr_t)g_avatarAnchor;
    logLine("bp: arming write breakpoint on %p for 6 seconds", (void*)g_bpAddr);
    const int armed = bpApplyAllThreads(g_bpAddr);
    logLine("bp: armed on %d thread(s)", armed);

    for (int i = 0; i < 60 && g_bpHits.load() < 6; ++i) Sleep(100);

    const int cleared = bpApplyAllThreads(0);
    g_bpArmed.store(false);
    logLine("bp: breakpoint cleared on %d thread(s), %d hit(s) captured", cleared,
            g_bpHits.load());
    return 0;
}

void camBpProbe() {
    if (g_bpArmed.exchange(true)) return;
    g_bpHits.store(0);
    CreateThread(nullptr, 0, bpWorker, nullptr, 0, nullptr);
}



// ---------------------------------------------------------------- 自由相机（v1
//
// 由反汇编得到的布局：相机结构体 = 锚点地址 - 0xC4，位置在 +0xC4/+0xC8/+0xCC
// 游戏每帧会写这三个字段，所以这里用高频线程抢写 ms 一次，帧间隔约 7ms），
// 相机就会停在我们指定的位置，人物照常行动
static std::atomic<bool> camFreeOn{false};
static std::atomic<int> camFreeWrites{0};
static float camFreeX = 0.f, camFreeY = 0.f, camFreeZ = 0.f;
static uint8_t* camFreePos = nullptr;   // 指向 +0xC4 的地址

DWORD WINAPI camFreeThread(LPVOID) {
    bool wasOn = false;
    for (;;) {
        const bool on = camFreeOn.load();
        if (on && !wasOn) {
            // 进入自由相机：把当前位置作为起点
            if (g_avatarAnchor) {
                camFreePos = g_avatarAnchor;   // 锚点本身就是 +0xC4
                SIZE_T got = 0;
                float v[3] = {0, 0, 0};
                if (ReadProcessMemory(GetCurrentProcess(), camFreePos, v, sizeof(v), &got) &&
                    got == sizeof(v)) {
                    camFreeX = v[0];
                    camFreeY = v[1];
                    camFreeZ = v[2];
                }
                logLine("freecam: ON at %.2f %.2f %.2f (struct %p)", camFreeX, camFreeY, camFreeZ,
                        (void*)(camFreePos - 0xC4));
            } else {
                logLine("freecam: no anchor, run step 1/4 first");
                camFreeOn.store(false);
            }
        }
        if (on && camFreePos) {
            // 小键 8/2/4/6 前后左右，PageUp/PageDown 上下，Shift 加
const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const float step = fast ? 1.2f : 0.35f;
            if (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) camFreeY += step;
            if (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) camFreeY -= step;
            if (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) camFreeX -= step;
            if (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) camFreeX += step;
            if (GetAsyncKeyState(VK_PRIOR) & 0x8000) camFreeZ += step;
            if (GetAsyncKeyState(VK_NEXT) & 0x8000) camFreeZ -= step;

            float v[3] = {camFreeX, camFreeY, camFreeZ};
            SIZE_T written = 0;
            if (WriteProcessMemory(GetCurrentProcess(), camFreePos, v, sizeof(v), &written))
                camFreeWrites.fetch_add(1);
        }
        wasOn = on;
        Sleep(1);
    }
    return 0;
}

void camFreeToggle(bool on) {
    if (on && !g_avatarAnchor) {
        logLine("freecam: need step 1 + step 4 first (no anchor)");
        return;
    }
    camFreeOn.store(on);
    if (on) camFreeWrites.store(0);
    logLine("freecam: %s", on ? "enabled" : "disabled");
}

bool camFreeEnabled() { return camFreeOn.load(); }
int camFreeWriteCount() { return camFreeWrites.load(); }
float camFreeGet(int i) { return i == 0 ? camFreeX : (i == 1 ? camFreeY : camFreeZ); }
void camFreeNudge(int axis, float delta) {
    if (axis == 0) camFreeX += delta;
    else if (axis == 1) camFreeY += delta;
    else camFreeZ += delta;
}

DWORD WINAPI camFreeLogThread(LPVOID);


// ---------------------------------------------------------------- 自动对话（剧情加速）
//
// 原神的对话推进靠"空格 / 左键"。剧情慢本质是没有自动播放，所以这里按住一个开关就定时敲空格
//  SendInput + 扫描码（ PostMessage 更能过游戏自带的输入层）
static std::atomic<bool> g_autoTalkOn{false};
static std::atomic<int> g_autoTalkCount{0};
static std::atomic<int> g_autoTalkInterval{220};   // ms

static void tapSpace() {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan = 0x39;                 // space 的扫描码
    in[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

DWORD WINAPI autoTalkThread(LPVOID) {
    for (;;) {
        if (g_autoTalkOn.load() && g_hwnd && GetForegroundWindow() == g_hwnd) {
            tapSpace();
            g_autoTalkCount.fetch_add(1);
            Sleep(g_autoTalkInterval.load());
        } else {
            Sleep(60);
        }
    }
    return 0;
}

void autoTalkToggle(bool on) {
    g_autoTalkOn.store(on);
    logLine("autotalk: %s", on ? "ON ( 220ms 敲一次空 " : "OFF");
}
bool autoTalkEnabled() { return g_autoTalkOn.load(); }
int autoTalkCount() { return g_autoTalkCount.load(); }
void autoTalkStart() { CreateThread(nullptr, 0, autoTalkThread, nullptr, 0, nullptr); }


void camFreeStartThread() {
    CreateThread(nullptr, 0, camFreeThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, camFreeLogThread, nullptr, 0, nullptr);
}



// ---------------------------------------------------------------- 自由相机 v2：直接接管写指令
//
// 反汇编结果（GenshinImpact.exe .1.0）：
//   0x1977424  f2 41 0f 11 87 c4 00 00 00   movsd %xmm0,0xc4(%r15)   ; x,y
//   0x197742d  f3 41 0f 11 97 cc 00 00 00   movss %xmm2,0xcc(%r15)   ; z
//   0x1977436                                （后续指令，跳回这里
// 进入 0x1977424 时：r15 = 相机结构体，xmm0 = 打包 (x,y)，xmm2 = z
// 我们把这两条 9 字节指令换成自己的写入，然后直接跳到 0x1977436
extern "C" {
volatile unsigned char g_camFreeFlag = 0;
volatile float g_camFreeX = 0.f, g_camFreeY = 0.f, g_camFreeZ = 0.f;
volatile unsigned long long g_camHookHits = 0;
volatile unsigned long long g_camTargetR15 = 0;
volatile unsigned long long g_camSkipCount = 0;
volatile unsigned long long g_lastR15 = 0;
volatile unsigned long long g_firstR15 = 0;
uintptr_t g_camStoreResume = 0;
}

extern "C" __attribute__((naked)) void camStoreHook() {
    __asm__ volatile(
        "incq g_camHookHits(%rip)\n"
        "movq %r15, g_lastR15(%rip)\n"
        "cmpq $0, g_firstR15(%rip)\n"
        "jne 9f\n"
        "movq %r15, g_firstR15(%rip)\n"
        "9:\n"
        "cmpb $0, g_camFreeFlag(%rip)\n"
        "je 1f\n"
        // 只有目标结构体才接管，其它对象照         "movq g_camTargetR15(%rip), %rax\n"
        "cmpq %rax, %r15\n"
        "jne 3f\n"
        // 自由相机开：写我们的坐         "movss g_camFreeX(%rip), %xmm0\n"
        "movss %xmm0, 0xC4(%r15)\n"
        "movss g_camFreeY(%rip), %xmm0\n"
        "movss %xmm0, 0xC8(%r15)\n"
        "movss g_camFreeZ(%rip), %xmm0\n"
        "movss %xmm0, 0xCC(%r15)\n"
        "jmp 2f\n"
        "1:\n"
        // 关闭：执行被我们替换掉的原始两条指令
        "movsd %xmm0, 0xC4(%r15)\n"
        "movss %xmm2, 0xCC(%r15)\n"
        "2:\n"
        "movq g_camStoreResume(%rip), %rax\n"
        "jmp *%rax\n"
        "3:\n"
        "incq g_camSkipCount(%rip)\n"
        "movsd %xmm0, 0xC4(%r15)\n"
        "movss %xmm2, 0xCC(%r15)\n"
        "movq g_camStoreResume(%rip), %rax\n"
        "jmp *%rax\n");
}

static bool g_camHookInstalled = false;

bool camInstallStoreHook() {
    if (g_camHookInstalled) return true;
    uint8_t* base = (uint8_t*)GetModuleHandleA(nullptr);
    if (!base) return false;
    void* target = base + 0x1977424;
    g_camStoreResume = (uintptr_t)(base + 0x1977436);
    MH_STATUS st = MH_CreateHook(target, (void*)camStoreHook, nullptr);
    if (st != MH_OK) {
        logLine("freecam: MH_CreateHook failed (%d)", (int)st);
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        logLine("freecam: MH_EnableHook failed (%d)", (int)st);
        return false;
    }
    g_camHookInstalled = true;
    logLine("freecam: store hook installed at %p (resume %p)", target, (void*)g_camStoreResume);
    return true;
}

//  hook 路径开关自由相
void camFreeHookToggle(bool on) {
    if (on) {
        if (!camInstallStoreHook()) {
            logLine("freecam: hook not installed, staying off");
            return;
        }
        g_camFreeFlag = 1;
    } else {
        g_camFreeFlag = 0;
    }
    logLine("freecam(hook): %s", on ? "ON" : "OFF");
}

bool camFreeHookEnabled() { return g_camFreeFlag != 0; }
void camFreeHookSet(float x, float y, float z) {
    g_camFreeX = x;
    g_camFreeY = y;
    g_camFreeZ = z;
}
DWORD WINAPI cbSizeLogThread(LPVOID) {
    for (;;) {
        Sleep(5000);
        char line[512];
        int n = 0;
        AcquireSRWLockShared(&g_sizeLock);
        for (int i = 0; i < 12; ++i) {
            if (!g_sizes[i].size) break;
            n += snprintf(line + n, sizeof(line) - n, " %u:%llu", g_sizes[i].size,
                          g_sizes[i].count);
            if (n > 380) break;
        }
        ReleaseSRWLockShared(&g_sizeLock);
        if (n > 0) logLine("cbsizes:%s", line);
    }
    return 0;
}

DWORD WINAPI camFreeLogThread(LPVOID) {
    unsigned long long lastHits = 0, lastSkip = 0;
    for (;;) {
        Sleep(5000);
        const unsigned long long h = g_camHookHits;
        const unsigned long long k = g_camSkipCount;
        if (h != lastHits || k != lastSkip) {
            logLine("freecam stat: hits=%llu skipped=%llu flag=%d target=%p firstR15=%p lastR15=%p",
                    h, k, (int)g_camFreeFlag, (void*)g_camTargetR15, (void*)g_firstR15,
                    (void*)g_lastR15);
            lastHits = h;
            lastSkip = k;
        }
    }
    return 0;
}

void camFreeHookSetTarget() {
    if (!g_avatarAnchor) return;
    g_camTargetR15 = (unsigned long long)(g_avatarAnchor - 0xC4);
    logLine("freecam(hook): target struct %p (anchor %p - 0xC4)",
            (void*)g_camTargetR15, (void*)g_avatarAnchor);
}

void camFreeHookSeed() {
    // 用当前相机位置作为起
if (!g_avatarAnchor) return;
    SIZE_T got = 0;
    float v[3] = {0, 0, 0};
    if (ReadProcessMemory(GetCurrentProcess(), g_avatarAnchor, v, sizeof(v), &got) &&
        got == sizeof(v)) {
        g_camFreeX = v[0];
        g_camFreeY = v[1];
        g_camFreeZ = v[2];
        logLine("freecam(hook): seeded at %.2f %.2f %.2f", v[0], v[1], v[2]);
    }
}
float camFreeHookGet(int i) {
    return i == 0 ? g_camFreeX : (i == 1 ? g_camFreeY : g_camFreeZ);
}
void camFreeHookNudge(int axis, float d) {
    if (axis == 0) g_camFreeX += d;
    else if (axis == 1) g_camFreeY += d;
    else g_camFreeZ += d;
}


void camProbeDump() {
    if (g_camCount == 0) {
        logLine("cam dump: nothing collected");
        return;
    }
    float px = 0, py = 0, pz = 0;
    {
        Snapshot s = bridge::get();
        px = s.px;
        py = s.py;
        pz = s.pz;
    }
    HANDLE self = GetCurrentProcess();
    int shown = 0;
    for (int i = 0; i < g_camCount && shown < 3; ++i) {
        // 只看"像相 的候选：离角 3~12 米，且不是方向向
const float d = g_camCands[i].dist;
        if (d < 3.f || d > 12.f) continue;
        const float* v = g_camCands[i].v;
        if (fabsf(v[0]) < 1.5f && fabsf(v[1]) < 1.5f && fabsf(v[2]) < 1.5f) continue;

        uint8_t* base = g_camCands[i].addr;
        bool dup = false;
        for (int k = 0; k < i; ++k) {
            intptr_t dd = (intptr_t)base - (intptr_t)g_camCands[k].addr;
            if (dd > -512 && dd < 512) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        shown++;

        logLine("cam dump #%d @ %p  value %.2f %.2f %.2f  dist %.2f", shown, base, v[0], v[1],
                v[2], d);

        static float buf[128];
        SIZE_T got = 0;
        uint8_t* startp = base - 256;
        if (!ReadProcessMemory(self, startp, buf, sizeof(buf), &got) || got < sizeof(buf)) {
            logLine("  (read failed)");
            continue;
        }
        for (int row = 0; row < 16; ++row) {
            char line[256];
            int n = 0;
            for (int c = 0; c < 8; ++c) {
                const float fv = buf[row * 8 + c];
                n += snprintf(line + n, sizeof(line) - n, "%11.3f", fv);
                if (n > 200) break;
            }
            const int off = row * 32 - 256;
            logLine("  %+04X:%s", off, line);
        }
        // 指针视图：把这块 8 字节整数打一遍，找指向角 场景的指
static uint64_t q[64];
        if (ReadProcessMemory(self, startp, q, sizeof(q), &got) && got == sizeof(q)) {
            for (int row = 0; row < 8; ++row) {
                char line[256];
                int n = 0;
                for (int c = 0; c < 8; ++c)
                    n += snprintf(line + n, sizeof(line) - n, " %llx",
                                  (unsigned long long)q[row * 8 + c]);
                logLine("  q%+04X:%s", row * 64 - 256, line);
            }
        }
    }
    logLine("cam dump: done (%d structure(s) dumped)", shown);
}

void camProbeCompare() {
    if (g_camCount == 0) {
        logLine("cam probe: nothing collected yet");
        return;
    }
    HANDLE self = GetCurrentProcess();
    float px = 0, py = 0, pz = 0;
    {
        Snapshot s = bridge::get();
        px = s.px;
        py = s.py;
        pz = s.pz;
    }
    int moved = 0, distStable = 0, hits = 0;
    for (int i = 0; i < g_camCount; ++i) {
        float v[3] = {0, 0, 0};
        SIZE_T got = 0;
        if (!ReadProcessMemory(self, g_camCands[i].addr, v, sizeof(v), &got) || got < 12)
            continue;
        const float d = fabsf(v[0] - g_camCands[i].v[0]) + fabsf(v[1] - g_camCands[i].v[1]) +
                        fabsf(v[2] - g_camCands[i].v[2]);
        if (d <= 0.3f) continue;
        moved++;

        // 相机跟着角色但保持距离：转视角时位置变了，到角色的距离几乎不变
// 同时也排 像方向向 的候选（三个分量都在 ±1.5 之内）
const bool dirLike =
                fabsf(v[0]) < 1.5f && fabsf(v[1]) < 1.5f && fabsf(v[2]) < 1.5f;
        if (dirLike) continue;

        const float ndx = v[0] - px, ndy = v[1] - py, ndz = v[2] - pz;
        const float ndist = sqrtf(ndx * ndx + ndy * ndy + ndz * ndz);
        if (fabsf(ndist - g_camCands[i].dist) > 2.0f) continue;
        distStable++;

        // 距离 2~12 米是最像相机的区间
        if (ndist < 1.5f || ndist > 15.f) continue;
        hits++;
        if (hits <= 25)
            logLine("   CAM? [%d] %p: %.2f %.2f %.2f -> %.2f %.2f %.2f  dist %.1f->%.1f", i,
                    g_camCands[i].addr, g_camCands[i].v[0], g_camCands[i].v[1],
                    g_camCands[i].v[2], v[0], v[1], v[2], g_camCands[i].dist, ndist);
    }
    logLine("cam probe: moved=%d, dist-stable=%d, camera-like=%d (of %d)", moved, distStable,
            hits, g_camCount);
}


// 远程触发：往游戏目录 camprobe1.on / camprobe2.on，overlay 看到就跑一步然后删掉文件
// 调试时不用点菜单，外部脚本就能驱动
DWORD WINAPI camProbeWatcher(LPVOID) {
    char dir[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (slash) *slash = 0;
    char f1[MAX_PATH], f2[MAX_PATH];
    snprintf(f1, sizeof(f1), "%s\\camprobe1.on", dir);
    snprintf(f2, sizeof(f2), "%s\\camprobe2.on", dir);
    for (;;) {
        if (GetFileAttributesA(f1) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA(f1);
            logLine("cam probe: step 1 requested by file");
            camProbeStart();
        }
        if (GetFileAttributesA(f2) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA(f2);
            logLine("cam probe: step 2 requested by file");
            camProbeCompare();
        }
        Sleep(400);
    }
    return 0;
}

void installWndProcHook() {
    if (!g_hwnd || g_origWndProc) return;
    g_origWndProc =
            (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    if (g_origWndProc)
        logLine("window proc hooked (mouse isolated while menu is open)");
    else
        logLine("window proc hook failed (err=%lu)", GetLastError());
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
    installWndProcHook();
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
static volatile int g_injectMmb = 0;   // 1=down 2=up（下一个 RawInput 时序注入）

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
            // 注入中键：游戏读的就是这个缓冲，改它就等于真按键
            if (g_injectMmb != 0) {
                if (g_injectMmb == 1) raw->data.mouse.usButtonFlags |= RI_MOUSE_MIDDLE_BUTTON_DOWN;
                else raw->data.mouse.usButtonFlags |= RI_MOUSE_MIDDLE_BUTTON_UP;
                g_injectMmb = 0;
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


// ---------------------------------------------------------------- 灵魂出窍 v3：接管常量缓冲里的相机矩
//
// 前五轮失败的原因：我们改的是"相机结构体副 ，渲染不用它
// 这次直接改渲染真正用的数 — 每帧上传 GPU 的常量缓冲（constant buffer）
//
// 做法
//   1. hook ID3D11DeviceContext::UpdateSubresource（所 CB 上传都走这里
//   2. 在传进来的数据里 位置向量"：连 3 
// float，值落在玩家附 2~30 
//      （视图矩阵的平移分量就是相机世界坐标，而我们已知玩家坐标）
//   3. 找到之后替换 3 
// float 为我们的自由相机坐标
using UpdateSubresourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT,
                                                    const D3D11_BOX*, const void*, UINT, UINT);
static UpdateSubresourceFn g_origUpdateSubresource = nullptr;
static bool g_cbHookInstalled = false;

static std::atomic<bool> cbFreeOn{false};
static std::atomic<unsigned long long> cbHits{0};
static std::atomic<unsigned long long> cbPatched{0};
static std::atomic<int> cbLogBudget{40};
static float fcX = 0.f, fcY = 0.f, fcZ = 0.f;
static bool fcSeeded = false;

void STDMETHODCALLTYPE hkUpdateSubresource(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub,
                                           const D3D11_BOX* box, const void* data, UINT rowPitch,
                                           UINT depthPitch);


// ---------------------------------------------------------------- 灵魂出窍 v4：Map / Unmap 接管
//
// v3 失败的原因：D3D11 的虚表里 UpdateSubresource 在很靠后的位置，索引 6 
// SetPrivateDataInterface — 钩子装上了但和画面无关。游戏每帧上传常量用的是
// 动态缓冲：Map(WRITE_DISCARD)  写数  Unmap。所以改 Unmap 之前的内存最有效
using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP,
                                          UINT, D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
static MapFn g_origMap = nullptr;
static UnmapFn g_origUnmap = nullptr;

static std::atomic<unsigned long long> stMapAll{0}, stMapBuf{0}, stCand{0}, stPatch{0};
static std::atomic<int> stLogBudget{30};

struct MappedCB {
    ID3D11Resource* res;
    void* data;
    UINT size;
};
static thread_local MappedCB t_map[16];
static thread_local int t_mapCount = 0;

// 视图矩阵 V = [R | t]，其 R  3x3 正交旋转，t = -R^T * eye
// 所以「相机世界坐标」不能直接从 t 读出来，要反算：eye = -(a·t, b·t, c·t)，a/b/c  R 的三行
// 判据 x3 正交（每行长度≈1、两两垂直）+ 反算出的 eye 落在玩家上方 1~40 米
// 相机矩阵 4 种常见布局 × 2 种语义（视图矩阵 / 位姿矩阵），全试一遍：
//   布局 A（行主序，平移在 12/13/14）：basis = 
//   布局 B（列主序，平移在 3/7/11）： basis = 
//   语义 1（view）： eye = -(a·t, b·t, c·t)
//   语义 2（pose）： eye = (t.x, t.y, t.z)
static bool matrixEyeAny(const float* m, const float* player, float* outEye, int* outLayout) {
    struct Layout { int b0, b1, b2, t0; };
    static const Layout kLayouts[2] = {
        {0, 4, 8, 12},   // 行主序： 0  = (m0,m1,m2)，平移在 12
        {0, 1, 2, 3},    // 列主序： 0  = (m0,m4,m8)，平移在 3
    };
    const bool colMajor[2] = {false, true};

    for (int L = 0; L < 2; ++L) {
        const Layout& lo = kLayouts[L];
        const bool col = colMajor[L];
        float a[3], b[3], c[3], t[3];
        if (!col) {
            a[0] = m[lo.b0 + 0]; a[1] = m[lo.b0 + 1]; a[2] = m[lo.b0 + 2];
            b[0] = m[lo.b1 + 0]; b[1] = m[lo.b1 + 1]; b[2] = m[lo.b1 + 2];
            c[0] = m[lo.b2 + 0]; c[1] = m[lo.b2 + 1]; c[2] = m[lo.b2 + 2];
        } else {
            a[0] = m[0]; a[1] = m[4]; a[2] = m[8];
            b[0] = m[1]; b[1] = m[5]; b[2] = m[9];
            c[0] = m[2]; c[1] = m[6]; c[2] = m[10];
        }
        t[0] = m[lo.t0 + 0];
        t[1] = m[lo.t0 + 1];
        t[2] = m[lo.t0 + 2];

        auto len = [](const float* v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
        auto dot = [](const float* u, const float* v) {
            return u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
        };
        if (fabsf(len(a) - 1.f) > 0.12f || fabsf(len(b) - 1.f) > 0.12f ||
            fabsf(len(c) - 1.f) > 0.12f)
            continue;
        if (fabsf(dot(a, b)) > 0.15f || fabsf(dot(a, c)) > 0.15f || fabsf(dot(b, c)) > 0.15f)
            continue;

        float cands[2][3];
        cands[0][0] = -dot(a, t); cands[0][1] = -dot(b, t); cands[0][2] = -dot(c, t);
        cands[1][0] = t[0];       cands[1][1] = t[1];       cands[1][2] = t[2];
        for (int sem = 0; sem < 2; ++sem) {
            const float ex = cands[sem][0], ey = cands[sem][1], ez = cands[sem][2];
            if (!(fabsf(ex) < 20000.f && fabsf(ey) < 20000.f && fabsf(ez) < 20000.f)) continue;
            const float dx = ex - player[0], dy = ey - player[1], dz = ez - player[2];
            if (dy < -2.f || dy > 15.f) continue;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < 0.25f || d2 > 900.f) continue;
            outEye[0] = ex;
            outEye[1] = ey;
            outEye[2] = ez;
            if (outLayout) *outLayout = L * 10 + sem;
            return true;
        }
    }
    return false;
}

// 锁定到某一个具体的视图矩阵，之后只改它（避免误改几万个实体矩阵
static ID3D11Resource* g_vmRes = nullptr;
static UINT g_vmBytes = 0;
static int g_vmOff = -1;


// ---------------------------------------------------------------- 诊断：把每种缓冲里的正交矩阵打出
// 目的：不 玩家附近"这种猜测，而是直接看数据长什么样，人工认出相机矩阵
static bool g_diagDone[16] = {false};
static unsigned int g_diagSize[16] = {0};

static bool ortho3(const float* m) {
    auto len = [](float x, float y, float z) { return sqrtf(x * x + y * y + z * z); };
    const float la = len(m[0], m[1], m[2]);
    const float lb = len(m[4], m[5], m[6]);
    const float lc = len(m[8], m[9], m[10]);
    if (fabsf(la - 1.f) > 0.15f || fabsf(lb - 1.f) > 0.15f || fabsf(lc - 1.f) > 0.15f)
        return false;
    const float ab = m[0] * m[4] + m[1] * m[5] + m[2] * m[6];
    const float ac = m[0] * m[8] + m[1] * m[9] + m[2] * m[10];
    const float bc = m[4] * m[8] + m[5] * m[9] + m[6] * m[10];
    return fabsf(ab) < 0.2f && fabsf(ac) < 0.2f && fabsf(bc) < 0.2f;
}

static void diagBuffer(const float* f, UINT bytes) {
    int slot = -1;
    for (int i = 0; i < 16; ++i) {
        if (g_diagSize[i] == bytes) return;      // 这个尺寸已经打过
if (g_diagSize[i] == 0 && slot < 0) slot = i;
    }
    if (slot < 0) return;

    const int n = (int)(bytes / 4);
    for (int off = 0; off + 15 < n; off += 4) {
        if (!ortho3(f + off)) continue;
        const float* m = f + off;
        const float ex = -(m[0] * m[12] + m[1] * m[13] + m[2] * m[14]);
        const float ey = -(m[4] * m[12] + m[5] * m[13] + m[6] * m[14]);
        const float ez = -(m[8] * m[12] + m[9] * m[13] + m[10] * m[14]);
        g_diagSize[slot] = bytes;
        g_diagDone[slot] = true;
        logLine("diag size=%u off=%d  view_eye=%.1f %.1f %.1f  pose_t=%.1f %.1f %.1f",
                bytes, off, ex, ey, ez, m[12], m[13], m[14]);
        logLine("   m = %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
                m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12],
                m[13], m[14], m[15]);
        return;
    }
}


// 世界坐标级别 500）的矩阵才是我们要找的：相机/角色在世界里的变换
// 每块缓冲都扫，最多打 24 条，避免刷屏
static std::atomic<int> g_worldBudget{80};

static void diagWorld(const float* f, UINT bytes) {
    if (g_worldBudget.load() <= 0) return;
    const int n = (int)(bytes / 4);
    for (int off = 0; off + 15 < n; off += 4) {
        const float* m = f + off;
        // 先做便宜的检查：平移分量够大才算世界坐标
        if (fabsf(m[12]) < 500.f && fabsf(m[13]) < 500.f && fabsf(m[14]) < 500.f &&
            fabsf(m[3]) < 500.f && fabsf(m[7]) < 500.f && fabsf(m[11]) < 500.f)
            continue;
        if (!ortho3(m)) continue;
        if (g_worldBudget.fetch_sub(1) < 0) return;
        const float ex = -(m[0] * m[12] + m[1] * m[13] + m[2] * m[14]);
        const float ey = -(m[4] * m[12] + m[5] * m[13] + m[6] * m[14]);
        const float ez = -(m[8] * m[12] + m[9] * m[13] + m[10] * m[14]);
        logLine("world size=%u off=%d eye=%.1f %.1f %.1f | t=%.1f %.1f %.1f", bytes, off, ex, ey, ez,
                m[12], m[13], m[14]);
        logLine("   m = %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f",
                m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12],
                m[13], m[14], m[15]);
        return;
    }
}

void STDMETHODCALLTYPE hkUnmap(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub) {
    if (res && t_mapCount > 0) {
        for (int i = 0; i < t_mapCount; ++i) {
            if (t_map[i].res != res || !t_map[i].data) continue;
            const float* f = (const float*)t_map[i].data;
            const UINT bytes = t_map[i].size;
            const int n = (int)(bytes / 4);
            Snapshot snap = bridge::get();
            const float player[3] = {snap.px, snap.py, snap.pz};

            // 诊断：每种尺寸都看一次，不管多大

            if (false && (player[0] != 0.f || player[2] != 0.f)) {
                // 已锁定：只验证并改写锁定位置
                if (g_vmRes == res && g_vmBytes == bytes && g_vmOff >= 0 && g_vmOff + 11 < n) {
                    float* m = (float*)t_map[i].data;
                    if (cbFreeOn.load()) {
                        const float* a = &m[g_vmOff + 0];
                        const float* b = &m[g_vmOff + 4];
                        const float* c = &m[g_vmOff + 8];
                        const float t0 = m[g_vmOff + 12], t1 = m[g_vmOff + 13],
                                    t2 = m[g_vmOff + 14];
                        float eye[3];
                        if (matrixEyeAny(m + g_vmOff, player, eye, nullptr)) {
                            // 想让 eye 移动 (fcX, fcY, fcZ)：t' = t - R^T * delta
                            const float d[3] = {fcX - eye[0], fcY - eye[1], fcZ - eye[2]};
                            const float da = a[0] * d[0] + a[1] * d[1] + a[2] * d[2];
                            const float db = b[0] * d[0] + b[1] * d[1] + b[2] * d[2];
                            const float dc = c[0] * d[0] + c[1] * d[1] + c[2] * d[2];
                            m[g_vmOff + 12] = t0 - da;
                            m[g_vmOff + 13] = t1 - db;
                            m[g_vmOff + 14] = t2 - dc;
                            stPatch.fetch_add(1);
                        }
                    }
                } else if (!g_vmRes) {
                    // 还没锁定：在整块缓冲里按 16 字节对齐 4x4 矩阵
                    for (int off = 0; off + 15 < n; off += 4) {
                        float eye[3];
                        int layout = -1;
                        if (!matrixEyeAny(f + off, player, eye, &layout)) continue;
                        stCand.fetch_add(1);
                        g_vmRes = res;
                        g_vmBytes = bytes;
                        g_vmOff = off;
                        logLine("cbmap: CAMERA MATRIX locked res=%p size=%u off=%d layout=%d eye=%.1f %.1f %.1f (player %.1f %.1f %.1f)",
                                (void*)res, bytes, off, layout, eye[0], eye[1], eye[2], player[0],
                                player[1], player[2]);
                        break;
                    }
                }
            }
            t_map[i].res = nullptr;
            break;
        }
    }
    g_origUnmap(ctx, res, sub);
}

HRESULT STDMETHODCALLTYPE hkMap(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub,
                                D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    const HRESULT hr = g_origMap(ctx, res, sub, type, flags, mapped);
    stMapAll.fetch_add(1);
    if (FAILED(hr) || !res || !mapped || !mapped->pData) return hr;

    D3D11_RESOURCE_DIMENSION dim{};
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return hr;

    ID3D11Buffer* buf = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Buffer), (void**)&buf)) || !buf) return hr;
    D3D11_BUFFER_DESC desc{};
    buf->GetDesc(&desc);
    buf->Release();
    stMapBuf.fetch_add(1);
    {
        AcquireSRWLockExclusive(&g_sizeLock);
        int slot = -1;
        for (int i = 0; i < 12; ++i) {
            if (g_sizes[i].size == desc.ByteWidth) {
                slot = i;
                break;
            }
            if (g_sizes[i].size == 0 && slot < 0) slot = i;
        }
        if (slot >= 0) {
            g_sizes[slot].size = desc.ByteWidth;
            g_sizes[slot].count++;
        }
        ReleaseSRWLockExclusive(&g_sizeLock);
    }

    const UINT bytes = desc.ByteWidth;
    if (bytes < 48 || bytes > 200000 || (bytes % 16) != 0) return hr;

    if (t_mapCount >= 16) {
        // 满了就挤掉最旧的
        for (int i = 0; i < 15; ++i) t_map[i] = t_map[i + 1];
        t_mapCount = 15;
    }
    t_map[t_mapCount++] = MappedCB{res, mapped->pData, bytes};
    return hr;
}

void installMapHooks();

void installMapHooks() {
    if (!g_context || (g_origMap && g_origUnmap)) return;
    void** vt = *(void***)g_context;
    void* mapAddr = vt[14];
    void* unmapAddr = vt[15];
    logLine("cbmap: vtable Map=%p Unmap=%p", mapAddr, unmapAddr);
    if (MH_CreateHook(mapAddr, (void*)hkMap, (void**)&g_origMap) == MH_OK &&
        MH_EnableHook(mapAddr) == MH_OK) {
        logLine("cbmap: Map hooked");
    }
    if (MH_CreateHook(unmapAddr, (void*)hkUnmap, (void**)&g_origUnmap) == MH_OK &&
        MH_EnableHook(unmapAddr) == MH_OK) {
        logLine("cbmap: Unmap hooked");
    }
}

unsigned long long cbMapStat(int which) {
    switch (which) {
        case 0: return stMapAll.load();
        case 1: return stMapBuf.load();
        case 2: return stCand.load();
        default: return stPatch.load();
    }
}

void hookUpdateSubresource() {
    if (g_cbHookInstalled || !g_context) return;
    void** vt = *(void***)g_context;
    void* target = vt[6];   // 已废弃：这个索引不是 UpdateSubresource
    if (!target) {
        logLine("cbhook: vtable[6] is null");
        return;
    }
    if (MH_CreateHook(target, (void*)hkUpdateSubresource,
                      (void**)&g_origUpdateSubresource) != MH_OK) {
        logLine("cbhook: MH_CreateHook failed");
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        logLine("cbhook: MH_EnableHook failed");
        return;
    }
    g_cbHookInstalled = true;
    logLine("cbhook: UpdateSubresource hooked at %p", target);
}

void STDMETHODCALLTYPE hkUpdateSubresource(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub,
                                           const D3D11_BOX* box, const void* data, UINT rowPitch,
                                           UINT depthPitch) {
    if (!res || !data) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }
    D3D11_RESOURCE_DIMENSION dim{};
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }
    ID3D11Buffer* buf = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Buffer), (void**)&buf)) || !buf) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }
    D3D11_BUFFER_DESC desc{};
    buf->GetDesc(&desc);
    buf->Release();

    // 只处理小常量缓冲
    const UINT bytes = desc.ByteWidth;
    if (bytes < 48 || bytes > 2048 || (bytes % 16) != 0) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }

    Snapshot snap = bridge::get();
    if (snap.px == 0.f && snap.py == 0.f) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }

    const float* f = (const float*)data;
    const int n = (int)(bytes / 4);
    int found = -1;
    for (int i = 0; i + 2 < n; ++i) {
        const float x = f[i], y = f[i + 1], z = f[i + 2];
        if (!(x > -100000.f && x < 100000.f)) continue;
        const float dx = x - snap.px, dy = y - snap.py, dz = z - snap.pz;
        if (dy < -4.f || dy > 30.f) continue;                  // 相机在玩家上
const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 1.f || d2 > 900.f) continue;                  // 1~30          found = i;
        break;
    }

    if (found < 0) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }

    cbHits.fetch_add(1);
    if (cbLogBudget.load() > 0) {
        cbLogBudget.fetch_sub(1);
        logLine("cb: matrix-shaped CB %u bytes, pos at float[%d] = %.2f %.2f %.2f (player %.1f %.1f %.1f)",
                bytes, found, f[found], f[found + 1], f[found + 2], snap.px, snap.py, snap.pz);
    }

    if (!cbFreeOn.load()) {
        g_origUpdateSubresource(ctx, res, sub, box, data, rowPitch, depthPitch);
        return;
    }

    // 自由相机：复制一份数据，替换那三
    static thread_local std::vector<uint8_t> tmp;
    tmp.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
    float* m = (float*)tmp.data();
    m[found] = fcX;
    m[found + 1] = fcY;
    m[found + 2] = fcZ;
    cbPatched.fetch_add(1);
    g_origUpdateSubresource(ctx, res, sub, box, tmp.data(), rowPitch, depthPitch);
}

void cbFreeToggle(bool on) {
    if (on) {
        logLine("freecam3: disabled in this build (render hook caused frame drops)");
        if (!fcSeeded) {
            Snapshot snap = bridge::get();
            fcX = snap.px;
            fcY = snap.py + 2.f;
            fcZ = snap.pz;
            fcSeeded = true;
            logLine("freecam3: seeded at %.1f %.1f %.1f", fcX, fcY, fcZ);
        }
    }
    if (!on) {
        g_vmRes = nullptr;
        g_vmOff = -1;
    }
    cbFreeOn.store(on);
    logLine("freecam3: %s", on ? "ON" : "OFF");
}
bool cbFreeEnabled() { return cbFreeOn.load(); }
void cbFreeNudge(int axis, float d) {
    if (axis == 0) fcX += d;
    else if (axis == 1) fcY += d;
    else fcZ += d;
}
float cbFreeGet(int i) { return i == 0 ? fcX : (i == 1 ? fcY : fcZ); }
unsigned long long cbFreeHits() { return cbHits.load(); }
unsigned long long cbFreePatched() { return cbPatched.load(); }



// ---------------------------------------------------------------- 主线程任务执
//  Present 钩子里跑，确保在 IL2CPP 主线程上下文中调用游 API 
// ---------------------------------------------------------------- 第一批：Unity API 调用
static void applyUnityOps() {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;

    if (g_fogOp != 0) {
        const int op = g_fogOp;
        g_fogOp = 0;
        using Fn = void (*)(unsigned char value, void* mi);
        ((Fn)((uintptr_t)mod + 0x19BF35F0ull))((unsigned char)(op == 1 ? 0 : 1), nullptr);
        logLine("uops: RenderSettings.set_fog(%d)", op == 1 ? 0 : 1);
    }
    if (g_timeScaleOp != 0.f) {
        const float v = g_timeScaleOp;
        g_timeScaleOp = 0.f;
        using Fn = void (*)(float value, void* mi);
        ((Fn)((uintptr_t)mod + 0x19BE8800ull))(v, nullptr);
        logLine("uops: Time.set_timeScale(%.2f)", v);
    }
    if (g_fovOp != 0.f) {
        const float v = g_fovOp;
        g_fovOp = 0.f;
        using GetMainFn = void* (*)(void* mi);
        using SetFovFn = void (*)(void* camera, float value, void* mi);
        void* cam = ((GetMainFn)((uintptr_t)mod + 0x19BF6D90ull))(nullptr);
        if (cam) {
            ((SetFovFn)((uintptr_t)mod + 0x19BF5C10ull))(cam, v, nullptr);
            logLine("uops: Camera.set_fieldOfView(%.1f) cam=%p", v, cam);
        } else {
            logLine("uops: Camera.get_main() null");
        }
    }
}

// ---------------------------------------------------------------- 第二批：Il2CppString
//
// 布局：[klass][monitor][int32 length][UTF-16 chars...]
// klass 不用猜：随便调一 无参数、返 System.String"的静态方法，读它 q0 就是类指针
// 这里 System.Environment::get_MachineName() x194F95F0），不抛异常、随时可调
static uintptr_t overlayMod() { return (uintptr_t)GetModuleHandleA(nullptr); }

static void* mkIl2CppString(const char* utf8) {
    const uintptr_t mod = overlayMod();
    if (!mod || !utf8) return nullptr;

    if (!g_strKlass) {
        // 不用 Environment.get_MachineName：实测从渲染线程调用会崩（GenshinImpact.exe+0x1FC75F0）。
        // 改走 UnityEngine.Object::get_name()：Camera.get_main() 拿相机，再取它的名字字符串，
        // q0 就是 System.String 的类指针。两步都是无参、无副作用的纯读。
        using GetMainFn = void* (*)(void* mi);
        using GetNameFn = void* (*)(void* obj, void* mi);
        void* cam = ((GetMainFn)(mod + 0x19BF6D90ull))(nullptr);
        if (!cam) {
            logLine("str: no camera yet, cannot probe string klass");
            return nullptr;
        }
        void* probe = ((GetNameFn)(mod + 0x19BFD7B0ull))(cam, nullptr);
        if (!probe) {
            logLine("str: Object.get_name returned null");
            return nullptr;
        }
        g_strKlass = *(void**)probe;
        logLine("str: System.String klass = %p (probe obj %p)", g_strKlass, probe);
    }

    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (wlen <= 0) return nullptr;
    const size_t bytes = 16 + (size_t)(wlen - 1) * 2 + 2;
    auto* obj = (uint8_t*)VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!obj) return nullptr;
    *(void**)(obj + 0) = g_strKlass;
    *(void**)(obj + 8) = nullptr;                 // monitor
    *(int32_t*)(obj + 16) = wlen - 1;             // 不含结尾 0
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, (wchar_t*)(obj + 20), wlen);
    return obj;
}

// ---------------------------------------------------------------- 隐藏 UI
//
// GameObject::Find(name) 是静态：rcx=string, rdx=MethodInfo
// GameObject::SetActive(v) 是实例：rcx=this, dl=value, r8=MethodInfo
static const char* kUiCandidates[] = {
    "Canvas", "UI", "UIRoot", "GUIRoot", "Canvas/UI", "UI/Canvas",
    "GameMain/Canvas", "UICamera/Canvas", "GUI",
};
static const int kUiCandidateCount = (int)(sizeof(kUiCandidates) / sizeof(kUiCandidates[0]));

static void* uiFind(const char* name) {
    const uintptr_t mod = overlayMod();
    if (!mod) return nullptr;
    using FindFn = void* (*)(void* name, void* mi);
    void* str = mkIl2CppString(name);
    if (!str) return nullptr;
    return ((FindFn)(mod + 0x19BE64A0ull))(str, nullptr);
}

// 只探测不写入：日志里列出哪些候选名能解析成 GameObject
static void hideUiProbe() {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    logLine("ui: probe start (%d candidates)", kUiCandidateCount);
    for (int i = 0; i < kUiCandidateCount; ++i) {
        void* go = uiFind(kUiCandidates[i]);
        logLine("ui:   %-18s -> %p", kUiCandidates[i], go);
    }
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\hideui.txt", "r");
    if (f) {
        char line[128];
        if (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;
            void* go = uiFind(line);
            logLine("ui:   hideui.txt[%s] -> %p", line, go);
        }
        fclose(f);
    }
}

static void applyHideUiTick() {
    if (g_hideUiOp == 0) return;
    const int op = g_hideUiOp;
    g_hideUiOp = 0;
    const uintptr_t mod = overlayMod();
    if (!mod) return;

    if (!g_uiRoot) {
        FILE* f = fopen("D:\\Programs\\gs71-runtime\\hideui.txt", "r");
        if (f) {
            char line[128];
            if (fgets(line, sizeof(line), f)) {
                size_t n = strlen(line);
                while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;
                if (n) {
                    g_uiRoot = uiFind(line);
                    logLine("ui: hideui.txt[%s] -> %p", line, g_uiRoot);
                }
            }
            fclose(f);
        }
    }
    if (!g_uiRoot) {
        for (int i = 0; i < kUiCandidateCount; ++i) {
            void* go = uiFind(kUiCandidates[i]);
            if (go) {
                g_uiRoot = go;
                logLine("ui: matched candidate '%s' -> %p", kUiCandidates[i], go);
                break;
            }
        }
    }
    if (!g_uiRoot) {
        logLine("ui: %s failed - no candidate resolved", op == 1 ? "hide" : "show");
        return;
    }

    // GameObject::SetActive(false) ????? UI ????? AV @0x1401080??
    // ???? Canvas ??? enabled???????????? UI ????
    using GetCompByNameFn = void* (*)(void* go, void* name, void* mi);
    using SetEnabledFn = void (*)(void* behaviour, unsigned char value, void* mi);
    void* nameStr = mkIl2CppString("Canvas");
    void* comp = nameStr ? ((GetCompByNameFn)(mod + 0x19BE5E50ull))(g_uiRoot, nameStr, nullptr) : nullptr;
    if (!comp) {
        logLine("ui: no Canvas component on %p", g_uiRoot);
        return;
    }
    ((SetEnabledFn)(mod + 0x19BD76B0ull))(comp, (unsigned char)(op == 1 ? 0 : 1), nullptr);
    logLine("ui: Canvas.enabled = %d (comp %p)", op == 1 ? 0 : 1, comp);
}

// ---------------------------------------------------------------- 世界投影（ESP
//
// 不调 Camera::WorldToScreenPoint — 它的 Vector3 传参 ABI 没验过，猜错就崩
// 改为纯算术：相机位置(已验证的 GetCameraPos) + 相机 forward(Transform::get_forward) + FOV
// 每帧只取一次相机状态（原来每个实体都调 4 个 IL2CPP 方法，FPS 从 60 掉到 24）
struct CamFrame {
    bool ok = false;
    float pos[3] = {0, 0, 0};
    float fwd[3] = {0, 0, 1};
    float right[3] = {1, 0, 0};
    float up[3] = {0, 1, 0};
    float tanHalf = 0.5f;
};
static CamFrame g_camFrame;

static void refreshCamFrame() {
    g_camFrame.ok = false;
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    using GetVec3 = void (*)(void* retBuf, void* mi);
    using GetMainFn = void* (*)(void* mi);
    using GetTransformFn = void* (*)(void* comp, void* mi);
    using GetForwardFn = void (*)(void* retBuf, void* xf, void* mi);
    using GetFovFn = float (*)(void* cam, void* mi);

    void* cam = ((GetMainFn)(mod + 0x19BF6D90ull))(nullptr);
    if (!cam) return;
    void* xf = ((GetTransformFn)(mod + 0x19C07150ull))(cam, nullptr);
    if (!xf) return;
    float fwd[4] = {0, 0, 0, 0};
    ((GetForwardFn)(mod + 0x19C015D0ull))(fwd, xf, nullptr);
    float cp[4] = {0, 0, 0, 0};
    ((GetVec3)(mod + 0x13AB7B80ull))(cp, nullptr);
    const float fov = ((GetFovFn)(mod + 0x19BF5C00ull))(cam, nullptr);

    float fx = fwd[0], fy = fwd[1], fz = fwd[2];
    const float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-4f || fov < 1.f || fov > 179.f) return;
    fx /= fl; fy /= fl; fz /= fl;

    float rx = fz, rz = -fx;
    const float rl = sqrtf(rx * rx + rz * rz);
    if (rl < 1e-4f) return;
    rx /= rl; rz /= rl;

    g_camFrame.ok = true;
    g_camFrame.pos[0] = cp[0]; g_camFrame.pos[1] = cp[1]; g_camFrame.pos[2] = cp[2];
    g_camFrame.fwd[0] = fx; g_camFrame.fwd[1] = fy; g_camFrame.fwd[2] = fz;
    g_camFrame.right[0] = rx; g_camFrame.right[1] = 0.f; g_camFrame.right[2] = rz;
    g_camFrame.up[0] = fy * rz - fz * 0.f;
    g_camFrame.up[1] = fz * rx - fx * rz;
    g_camFrame.up[2] = fx * 0.f - fy * rx;
    g_camFrame.tanHalf = tanf(fov * 0.5f * 3.14159265f / 180.f);
}

bool projImpl(float x, float y, float z, float* sx, float* sy, float vw, float vh) {
    if (!sx || !sy || vw <= 1.f || vh <= 1.f) return false;
    if (!g_camFrame.ok) return false;
    const float* cp = g_camFrame.pos;
    const float* f = g_camFrame.fwd;
    const float* r = g_camFrame.right;
    const float* u = g_camFrame.up;

    const float dx = x - cp[0], dy = y - cp[1], dz = z - cp[2];
    const float zc = dx * f[0] + dy * f[1] + dz * f[2];
    if (zc <= 0.15f) return false;

    const float xc = dx * r[0] + dy * r[1] + dz * r[2];
    const float yc = dx * u[0] + dy * u[1] + dz * u[2];
    const float aspect = vw / vh;
    const float ndcX = xc / (zc * g_camFrame.tanHalf * aspect);
    const float ndcY = yc / (zc * g_camFrame.tanHalf);
    *sx = (ndcX * 0.5f + 0.5f) * vw;
    *sy = (0.5f - ndcY * 0.5f) * vh;
    return true;
}


// 目标相对相机前方的偏角（度，-180..180，正值=右边）
static bool bearingImpl(float x, float y, float z, float* outDeg, float* outDist) {
    if (!g_camFrame.ok || !outDeg) return false;
    const float dx = x - g_camFrame.pos[0];
    const float dz = z - g_camFrame.pos[2];
    const float fdot = dx * g_camFrame.fwd[0] + dz * g_camFrame.fwd[2];
    const float rdot = dx * g_camFrame.right[0] + dz * g_camFrame.right[2];
    if (outDist) *outDist = sqrtf(dx * dx + dz * dz);
    *outDeg = atan2f(rdot, fdot) * 57.29578f;
    return true;
}

// ---------------------------------------------------------------- 原生飞行
//
// 7.1 字段（SDK 表，与已验证 +0x570 rigidbody 同源）：
//   VCHumanoidMove      +0x6d0 = VCHumanoidMoveData*   +0x2e0 isInGravityZero
//                       +0x939 remoteAvatarFlyIgnoreGravity  +0x570 rigidbody
//   VCHumanoidMoveData  +0x396 tryFlyUp  +0x398 tryFlyDown  +0x3bc forceAirStateFly
//
// 思路：不去伪造坐标，而是把游戏自己的"飞行状 开关打开，让它自己算位移/同步
static void applyFlyTick() {
    // V ????????????????? .now ??
    static bool vDown = false;
    const bool vNow = (GetAsyncKeyState('V') & 0x8000) != 0;
    if (vNow && !vDown) {
        g_flyMode = (g_flyMode == 9) ? 0 : 9;
        logLine("fly: hotkey V -> mode %d (9=NoClip position write)", (int)g_flyMode);
    }
    vDown = vNow;

    const uintptr_t mod = overlayMod();
    if (!mod) return;

    // 关飞时：把玩家刚体的碰撞关掉，这样才能穿墙；关飞时恢复
    // 飞行时：关掉玩家刚体的重力（否则悬停会一直下沉）；
    // 碰撞默认保留，只有 flynoclip=1 时才关（关了会穿地面往下掉）
    static int rbFlyState = 0;   // 0=正常 1=关重力 2=关重力+关碰撞
    const int wantState = (g_flyMode == 2 || g_flyMode == 3) ? (g_flyNoClip ? 2 : 1) : 0;
    if (wantState != rbFlyState) {
        const int prev = rbFlyState;
        rbFlyState = wantState;
        using SetBoolFn = void (*)(void* self, unsigned char value, void* mi);
        auto setGravity = (SetBoolFn)(mod + 0x19C5E210ull);
        auto setDetect = (SetBoolFn)(mod + 0x19C5E520ull);
        int ng = 0, nc = 0;
        const long total0 = g_vchmInstCount;
        for (long i = 0; i < total0 && i < 512; ++i) {
            void* a = g_vchmInst[i];
            if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
            void* rb = *(void**)((uint8_t*)a + 0x570);
            if (!rb || *(uint64_t*)rb != 0x500014137b0ull) continue;
            if (wantState >= 1) { setGravity(rb, 0, nullptr); ++ng; }
            else if (prev >= 1) { setGravity(rb, 1, nullptr); ++ng; }
            if (wantState == 2) { setDetect(rb, 0, nullptr); ++nc; }
            else if (prev == 2) { setDetect(rb, 1, nullptr); ++nc; }
        }
        logLine("fly: state %d -> %d (rb touched: gravity %d, collision %d)", prev, wantState, ng, nc);
    }

    if (g_flyMode == 0) return;

    // [ / ] 调速（边沿触发）
    static bool brDown = false, blDown = false;
    const bool brNow = (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0;
    const bool blNow = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0;
    if (brNow && !brDown) { g_flySpeed = (g_flySpeed < 40.f) ? g_flySpeed + 1.f : 40.f; logLine("fly: speed -> %.0f", g_flySpeed); }
    if (blNow && !blDown) { g_flySpeed = (g_flySpeed > 1.f) ? g_flySpeed - 1.f : 1.f; logLine("fly: speed -> %.0f", g_flySpeed); }
    brDown = brNow;
    blDown = blNow;

    const bool wantUp = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    const bool wantDn = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;

    float av[4] = {0, 0, 0, 0};
    {
        using GetVec3 = void (*)(void* retBuf, void* mi);
        ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);   // GetAvatarPos
    }
    const float avatarY = av[1];

    // 贴地检测：一直写向下的速度但 Y 不再下降 → 说明踩地了，再写就会把角色挤进地面
    static float s_lastY = 0.f;
    static int s_stuck = 0;
    if (wantDn) {
        if (s_lastY != 0.f && fabsf(avatarY - s_lastY) < 0.02f) ++s_stuck; else s_stuck = 0;
    } else {
        s_stuck = 0;
    }
    s_lastY = avatarY;
    const bool grounded = (s_stuck > 8);
    if (s_stuck == 9) logLine("fly: ground contact at y=%.2f, descent stopped", avatarY);

    int valid = 0, wrote = 0;
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a) continue;
        if (*(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* md = *(void**)((uint8_t*)a + 0x6d0);
        if (!md || (*(uint64_t*)md >> 40) != 5) continue;
        ++valid;

        if (g_flyMode == 3) {
            // AutoRun：只向前推进，不穿墙不飞
            void* rb3 = *(void**)((uint8_t*)a + 0x570);
            if (!rb3 || *(uint64_t*)rb3 != 0x500014137b0ull) continue;
            using SetVelFn3 = void (*)(void* self, void* vec, void* mi);
            float f3[3] = {g_camFrame.fwd[0], 0.f, g_camFrame.fwd[2]};
            const float l3 = sqrtf(f3[0] * f3[0] + f3[2] * f3[2]);
            if (l3 > 1e-4f) { f3[0] /= l3; f3[2] /= l3; }
            float v3[3] = {f3[0] * g_flySpeed, 0.f, f3[2] * g_flySpeed};
            ((SetVelFn3)(mod + 0x19C5E130ull))(rb3, v3, nullptr);
            ++wrote;
            continue;
        }
        if (g_flyMode == 1) {
            if (g_flyFlags & 1) *(uint8_t*)((uint8_t*)md + 0x3bc) = 1;
            if (g_flyFlags & 2) {
                *(uint8_t*)((uint8_t*)md + 0x396) = wantUp ? 1 : 0;
                *(uint8_t*)((uint8_t*)md + 0x398) = wantDn ? 1 : 0;
            }
            if (g_flyFlags & 4) *(uint8_t*)((uint8_t*)a + 0x2e0) = 1;
            if (g_flyFlags & 8) *(uint8_t*)((uint8_t*)a + 0x939) = 1;
            ++wrote;
        } else {
            void* rb = *(void**)((uint8_t*)a + 0x570);
            if (!rb || *(uint64_t*)rb != 0x500014137b0ull) continue;
            using SetVelFn = void (*)(void* self, void* vec, void* mi);
            // 方向跟镜头 + WASD；不按键就悬停
            float mvx = 0.f, mvz = 0.f;
            const float fx = g_camFrame.fwd[0], fz = g_camFrame.fwd[2];
            const float rx = g_camFrame.right[0], rz = g_camFrame.right[2];
            const bool kW = (GetAsyncKeyState('W') & 0x8000) != 0;
            const bool kS = (GetAsyncKeyState('S') & 0x8000) != 0;
            const bool kA = (GetAsyncKeyState('A') & 0x8000) != 0;
            const bool kD = (GetAsyncKeyState('D') & 0x8000) != 0;
            if (kW) { mvx += fx; mvz += fz; }
            if (kS) { mvx -= fx; mvz -= fz; }
            if (kD) { mvx += rx; mvz += rz; }
            if (kA) { mvx -= rx; mvz -= rz; }
            const float ml = sqrtf(mvx * mvx + mvz * mvz);
            if (ml > 1e-4f) { mvx /= ml; mvz /= ml; } else { mvx = 0.f; mvz = 0.f; }

            float vy = 0.f;
            const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wantUp) vy = g_flySpeed;
            else if (wantDn && !grounded) vy = shiftDown ? -g_flySpeed : -g_flyDescend;
            // 脚掉到地下时：强制往上推一下救回来
            if (avatarY < -80.f) vy = 15.f;

            float v[3] = {mvx * g_flySpeed, vy, mvz * g_flySpeed};
            ((SetVelFn)(mod + 0x19C5E130ull))(rb, v, nullptr);
            ++wrote;
        }
    }

    static DWORD lastBeat = 0;
    const DWORD now = GetTickCount();
    if (now - lastBeat > 2000) {
        lastBeat = now;
        logLine("fly: mode=%d flags=0x%X speed=%.1f vchm=%ld valid=%d wrote=%d up=%d dn=%d",
                g_flyMode, g_flyFlags, g_flySpeed, total, valid, wrote, wantUp ? 1 : 0, wantDn ? 1 : 0);
    }
}

// 对菜单暴露的控制接口
void flySetImpl(bool on) { g_flyMode = on ? 1 : 0; logLine("fly: %s (mode %d)", on ? "ON" : "OFF", (int)g_flyMode); }
bool flyGetImpl() { return g_flyMode != 0; }
void flySpeedSetImpl(float v) {
    if (v < 1.f) v = 1.f;
    if (v > 40.f) v = 40.f;
    g_flySpeed = v;
}
float flySpeedGetImpl() { return g_flySpeed; }
void hideUiImpl(bool on) { g_hideUiOp = on ? 1 : 2; }
bool hideUiGetImpl() { return g_hideUiOp == 1; }


// ---------------------------------------------------------------- Lua 执行探针
static volatile int g_clsFindReq = 0;
static volatile int g_clsGrepReq = 0;
static volatile int g_elDiffReq = 0;
static volatile int g_elDumpReq = 0;
static volatile int g_elEnReq = 0;
static volatile int g_entProbeReq = 0;
static volatile int g_posfReq = 0;
static volatile int g_xfReq = 0;
static volatile int g_locfReq = 0;
static volatile int g_mtabReq = 0;
static volatile int g_mdumpReq = 0;
static volatile int g_fsmdReq = 0;
static volatile int g_hcAReq = 0;
static volatile int g_hcBReq = 0;
static volatile int g_entfReq = 0;
static volatile int g_entclsReq = 0;
static volatile int g_elBtnReq = 0;
static volatile int g_elBtnVal = 1;
static volatile int g_elRefReq = 0;
static volatile int g_luaExecReq = 0;
static void* g_luaMgr = nullptr;
static void* g_luaEnv = nullptr;


// 探针：扫 klass 前 0x80 字节，看哪个是"类名字指针"
static bool readablePtr(void* p);
static void dumpKlassNames(void* obj, const char* tag) {
    if (!obj) return;
    void* klass = *(void**)obj;
    if (!readablePtr(klass)) { logLine("cls[%s]: klass unreadable", tag); return; }
    logLine("cls[%s]: obj=%p klass=%p", tag, obj, klass);
    for (int off = 0; off < 0x80; off += 8) {
        void* q = *(void**)((uint8_t*)klass + off);
        if (!readablePtr(q)) continue;
        // 试着把它当 char* 读
        char buf[64] = {0};
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), q, buf, sizeof(buf) - 1, &got) || got < 3) continue;
        bool printable = true;
        for (int i = 0; i < 40 && buf[i]; ++i) {
            const unsigned char c = (unsigned char)buf[i];
            if (c < 0x20 || c > 0x7E) { printable = false; break; }
        }
        if (printable && buf[0]) logLine("cls[%s]:   +0x%02X -> %p  \"%s\"", tag, off, q, buf);
    }
}
static bool readablePtr(void* p) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    return mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD);
}

static void luaExecProbe() {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    if (!g_luaMgr) { logLine("lua: no LuaManager instance yet (run lua_probe.now first)"); return; }

    using MemFn = float (*)(void* self, void* mi);
    const float mb = ((MemFn)(mod + 0x84E4420ull))(g_luaMgr, nullptr);
    logLine("lua: GetLuaMemoryMB -> %.2f MB (instance looks %s)", mb, (mb > 0.f && mb < 4096.f) ? "OK" : "WRONG");

    using GetLFn = void* (*)(void* self, void* mi);
    auto getL = (GetLFn)(mod + 0xE8D2640ull);

    void* candidates[8] = {};
    int nc = 0;
    void* pt = *(void**)((uint8_t*)g_luaMgr + 0x40);
    candidates[nc++] = *(void**)((uint8_t*)g_luaMgr + 0x30);   // luaMain
    candidates[nc++] = *(void**)((uint8_t*)g_luaMgr + 0x20);   // luaHotfix
    candidates[nc++] = *(void**)((uint8_t*)g_luaMgr + 0x38);   // customLoader
    candidates[nc++] = *(void**)((uint8_t*)g_luaMgr + 0x48);   // luaActorMgr
    if (readablePtr(pt)) {
        candidates[nc++] = *(void**)((uint8_t*)pt + 0x10);      // LuaTable.luaEnv 猜测
        candidates[nc++] = *(void**)((uint8_t*)pt + 0x20);
    }
    for (int i = 0; i < nc; ++i) {
        void* cand = candidates[i];
        if (!readablePtr(cand)) { logLine("lua: cand[%d] %p not readable", i, cand); continue; }
        void* L = getL(cand, nullptr);
        logLine("lua: cand[%d] %p -> get_L() = %p%s", i, cand, L,
                (L && readablePtr((void*)((uintptr_t)L & ~0x7ull))) ? " (readable)" : "");
    }

    // ---- DoString 链：Encoding.get_ASCII -> GetBytes(string) -> LuaEnv.DoString(byte[], name, table)
    void* pt2 = *(void**)((uint8_t*)g_luaMgr + 0x40);
    void* env = g_luaEnv ? g_luaEnv : (pt2 ? *(void**)((uint8_t*)pt2 + 0x10) : nullptr);
    if (!readablePtr(env)) {
        logLine("lua: LuaEnv candidate %p not readable, skip DoString", env);
    } else {
        char chunk[512] = "print('gs71 lua ok')";
        FILE* lf = fopen("D:\\Programs\\gs71-runtime\\lua.txt", "r");
        if (lf) {
            size_t n = fread(chunk, 1, sizeof(chunk) - 1, lf);
            chunk[n] = 0;
            fclose(lf);
        }
        using GetEncFn = void* (*)(void* mi);
        using GetBytesFn = void* (*)(void* enc, void* str, void* mi);
        using DoStringFn = void* (*)(void* env, void* bytes, void* name, void* table, void* mi);
        void* enc = ((GetEncFn)(mod + 0x1952C990ull))(nullptr);          // Encoding.get_ASCII
        void* cs = mkIl2CppString(chunk);
        void* bytes = (enc && cs) ? ((GetBytesFn)(mod + 0x1953C220ull))(enc, cs, nullptr) : nullptr;
        void* nm = mkIl2CppString("gs71_lua");
        logLine("lua: DoString env=%p enc=%p bytes=%p chunk=%d bytes", env, enc, bytes, (int)strlen(chunk));
        if (env && bytes) {
            void* ret = ((DoStringFn)(mod + 0xE8D3A80ull))(env, bytes, nm, nullptr, nullptr);
            logLine("lua: DoString returned %p", ret);
        }
    }

    // 最后才试执行：CallLuaConsole(funcName, args)
    logLine("lua: calling CallLuaConsole(print, ...) ...");
    using ConsoleFn = void (*)(void* self, void* fn, void* args, void* mi);
    void* s1 = mkIl2CppString("print");
    void* s2 = mkIl2CppString("gs71 hello from overlay");
    if (!s1 || !s2) { logLine("lua: mkIl2CppString failed"); return; }
    ((ConsoleFn)(mod + 0x84E4FB0ull))(g_luaMgr, s1, s2, nullptr);
    logLine("lua: CallLuaConsole returned without crash");
}

// ---------------------------------------------------------------- 参数化开 cmd.now
//
// 每行 key=value；watcher  2 秒看一次，改完不用重编
static void applyCmdFile() {
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\cmd.now", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = line;
        const char* v = eq + 1;
        while (*k == ' ' || *k == '\t') ++k;
        while (*v == ' ' || *v == '\t') ++v;
        if (!strcmp(k, "fly")) g_flyMode = atoi(v);
        else if (!strcmp(k, "flyflags")) g_flyFlags = (int)strtol(v, nullptr, 0);
        else if (!strcmp(k, "flyspeed")) flySpeedSetImpl((float)atof(v));
        else if (!strcmp(k, "flynoclip")) g_flyNoClip = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "flydescend")) { float d = (float)atof(v); if (d >= 0.3f && d <= 20.f) g_flyDescend = d; }
        else if (!strcmp(k, "fov")) g_fovOp = (float)atof(v);
        else if (!strcmp(k, "timescale")) g_timeScaleOp = (float)atof(v);
        else if (!strcmp(k, "fog")) g_fogOp = atoi(v) ? 1 : 2;
        else if (!strcmp(k, "hideui")) g_hideUiOp = atoi(v) ? 1 : 2;
        else if (!strcmp(k, "uiprobe")) g_uiProbeReq = 1;
        else {
            logLine("cmd: unknown key '%s'", k);
            continue;
        }
        logLine("cmd: %s = %s", k, v);
    }
    fclose(f);
}


// ---------------------------------------------------------------- 传送请求探针 / LuaManager 探针
//
// TransmitPlayerByServer(ref TransmitRequest)     0x13AEF540
// TransmitPlayerByClientOnly(ref TransmitRequest) 0x13AF2BE0
//   两者都是实例方法 + ref 参数，ABI 无歧义：rcx=this, rdx=&req, r8=MethodInfo
//   探针只抄结构内容再原样转发，不改参数

// 从对象的 klass 读类名（Il2CppClass +0x10 = name）
static const char* classNameOf(void* obj) {
    if (!obj) return nullptr;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(obj, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return nullptr;
    void* klass = nullptr;
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), obj, &klass, 8, &got) || got != 8 || !klass) return nullptr;
    if (!VirtualQuery(klass, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return nullptr;
    const char* name = nullptr;
    if (!ReadProcessMemory(GetCurrentProcess(), (uint8_t*)klass + 0x10, &name, 8, &got) || got != 8 || !name)
        return nullptr;
    if (!VirtualQuery(name, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return nullptr;
    return name;
}

static bool readableStr(const char* s) {
    if (!s) return false;
    for (int i = 0; i < 64; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0) return i >= 2;
        if (c < 0x20 || c > 0x7E) return false;
    }
    return false;
}
static int g_txHits = 0;

static void dumpHexField(const char* tag, void* p, int bytes) {
    if (!p) return;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return;
    if (mbi.State != MEM_COMMIT) return;
    const unsigned char* b = (const unsigned char*)p;
    char line[512];
    int n = 0;
    for (int i = 0; i < bytes && n < 480; i += 16) {
        n += snprintf(line + n, sizeof(line) - n, "%s%02X", i ? " " : "", b[i]);
        for (int j = 1; j < 16 && i + j < bytes; ++j)
            n += snprintf(line + n, sizeof(line) - n, " %02X", b[i + j]);
    }
    logLine("%s @%p: %s", tag, p, line);
    // 顺便把前 0x40 字节按 4 字节整数和浮点各打一行
    for (int off = 0; off < bytes && off < 0x40; off += 16) {
        char l2[256];
        int m = 0;
        for (int k = 0; k < 4; ++k) {
            const uint32_t iv = *(const uint32_t*)(b + off + k * 4);
            const float fv = *(const float*)(b + off + k * 4);
            m += snprintf(l2 + m, sizeof(l2) - m, " [%02X]=i:%u/f:%.2f", off + k * 4, iv, fv);
        }
        logLine("    +0x%02X%s", off, l2);
    }
}

using TxFn = bool (*)(void* self, void* reqPtr, void* mi);
static TxFn g_origTxServer = nullptr;
static TxFn g_origTxClient = nullptr;

static void hkTxServer(void* self, void* req, void* mi) {
    if (g_txHits < 6) {
        ++g_txHits;
        logLine("tx: ByServer self=%p req=%p", self, req);
        dumpHexField("tx.server", req, 0x40);
    }
    if (g_origTxServer) g_origTxServer(self, req, mi);
}
static void hkTxClient(void* self, void* req, void* mi) {
    if (g_txHits < 6) {
        ++g_txHits;
        logLine("tx: ByClientOnly self=%p req=%p", self, req);
        dumpHexField("tx.client", req, 0x40);
    }
    if (g_origTxClient) g_origTxClient(self, req, mi);
}

void installTransmitProbe(bool enable) {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    void* a = (void*)(mod + 0x13AEF540ull);
    void* b = (void*)(mod + 0x13AF2BE0ull);
    if (enable) {
        if (MH_CreateHook(a, (void*)hkTxServer, (void**)&g_origTxServer) == MH_OK) {
            MH_EnableHook(a);
            logLine("tx: hooked TransmitPlayerByServer at %p", a);
        } else {
            logLine("tx: hook ByServer failed");
        }
        if (MH_CreateHook(b, (void*)hkTxClient, (void**)&g_origTxClient) == MH_OK) {
            MH_EnableHook(b);
            logLine("tx: hooked TransmitPlayerByClientOnly at %p", b);
        } else {
            logLine("tx: hook ByClientOnly failed");
        }
    } else {
        MH_DisableHook(a);
        MH_RemoveHook(a);
        MH_DisableHook(b);
        MH_RemoveHook(b);
        g_origTxServer = nullptr;
        g_origTxClient = nullptr;
        logLine("tx: hooks removed");
    }
}

// ---- LuaManager 实例：GCTick 每帧都调，抄 rcx 就是 this
using LuaTickFn = void (*)(void* self, void* mi);
static LuaTickFn g_origLuaGcTick = nullptr;


// XLua.LuaEnv::Tick() 0xE8D3610 —— 直接把 LuaEnv 实例抓出来
static LuaTickFn g_origLuaEnvTick = nullptr;
static void hkLuaEnvTick(void* self, void* mi) {
    if (!g_luaEnv && self) {
        g_luaEnv = self;
        logLine("lua: LuaEnv instance = %p (q0=%p)", self, *(void**)self);
    }
    if (g_origLuaEnvTick) g_origLuaEnvTick(self, mi);
}

static void hkLuaGcTick(void* self, void* mi) {
    if (!g_luaMgr && self) {
        g_luaMgr = self;
        logLine("lua: LuaManager instance = %p (q0=%p)", self, *(void**)self);
        logLine("lua: LuaManager class name = %s", readableStr(classNameOf(self)) ? classNameOf(self) : "(unknown)");
        dumpKlassNames(self, "LuaManager");
        if (g_luaEnv) dumpKlassNames(g_luaEnv, "LuaEnv");
        for (int off = 0x18; off <= 0x58; off += 8) {
            void* f = *(void**)((uint8_t*)self + off);
            if (!f) { logLine("lua:   +0x%02X = null", off); continue; }
            const char* cn = classNameOf(f);
            logLine("lua:   +0x%02X = %p  class=%s", off, f, (readableStr(cn) ? cn : "?"));
        }
        // patchTable (+0x40) 内容：找 LuaEnv
        void* pt = *(void**)((uint8_t*)self + 0x40);
        if (pt) {
            logLine("lua: patchTable = %p class=%s", pt,
                    readableStr(classNameOf(pt)) ? classNameOf(pt) : "?");
            for (int off = 0x00; off < 0x40; off += 8) {
                void* q = *(void**)((uint8_t*)pt + off);
                if (!q) { logLine("lua:   pt+0x%02X = null", off); continue; }
                const char* cn2 = classNameOf(q);
                logLine("lua:   pt+0x%02X = %p  class=%s", off, q, (readableStr(cn2) ? cn2 : "?"));
            }
        }
    }
    if (g_origLuaGcTick) g_origLuaGcTick(self, mi);
}

void installLuaProbe(bool enable) {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    // GCTick 不一定每帧跑，所以把 Tick / LateTick 一起挂上
    static const unsigned long long kLuaTicks[] = {0x84E5160ull, 0x84E5D10ull, 0x84E5D70ull, 0xE8D3610ull};
    static const char* kLuaNames[] = {"GCTick", "Tick", "LateTick", "LuaEnv.Tick"};
    static TxFn g_origLua[4] = {nullptr, nullptr, nullptr, nullptr};
    static void* s_targets[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int k = 0; k < 4; ++k) {
        void* t = (void*)(mod + kLuaTicks[k]);
        s_targets[k] = t;
        if (enable) {
            void* hookFn = (k == 3) ? (void*)hkLuaEnvTick : (void*)hkLuaGcTick;
            if (MH_CreateHook(t, hookFn, (void**)&g_origLua[k]) == MH_OK) {
                MH_EnableHook(t);
                logLine("lua: %s hooked at %p", kLuaNames[k], t);
            } else {
                logLine("lua: hook %s failed", kLuaNames[k]);
            }
        } else {
            MH_DisableHook(t);
            MH_RemoveHook(t);
            g_origLua[k] = nullptr;
            logLine("lua: %s unhooked", kLuaNames[k]);
        }
    }
}


// ---------------------------------------------------------------- 按类名找类（klass）+ 找实例
// 实测：klass+0x18 就是类名字指针（LuaManager 验证过）
static uintptr_t scanAsciiFrom(uintptr_t lo, uintptr_t hi, const char* needle) {
    const size_t n = strlen(needle);
    if (n < 3) return 0;
    uintptr_t p = lo;
    MEMORY_BASIC_INFORMATION mbi{};
    while (p < hi) {
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            const uint8_t* b = (const uint8_t*)base;
            for (uintptr_t i = 0; i + n < size; ++i) {
                if (b[i] == (uint8_t)needle[0] && memcmp(b + i, needle, n) == 0) return base + i;
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    return 0;
}

static uintptr_t scanPtr(uintptr_t lo, uintptr_t hi, uintptr_t value) {
    uintptr_t p = lo;
    MEMORY_BASIC_INFORMATION mbi{};
    while (p < hi) {
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            const uintptr_t* q = (const uintptr_t*)base;
            for (uintptr_t i = 0; i + 8 < size; i += 8) {
                if (q[i / 8] == value) return base + i;
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    return 0;
}

// 在整个程序堆里找 q0 == klass 的对象
static int countInstances(uintptr_t klass, uintptr_t* outFirst, int maxScanMs) {
    if (!klass) return 0;
    const DWORD t0 = GetTickCount();
    int found = 0;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t limit = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    while (p < limit) {
        if (GetTickCount() - t0 > (DWORD)maxScanMs) break;
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) && size >= 16) {
            const uintptr_t* q = (const uintptr_t*)base;
            const uintptr_t words = size / 8;
            for (uintptr_t i = 0; i < words; ++i) {
                if (q[i] == klass) {
                    const uintptr_t obj = base + i * 8;
                    const uintptr_t mon = *(const uintptr_t*)(obj + 8);
                    if (mon > 0x1000) continue;   // 真实例的 q1 是 monitor，很小
                    if (outFirst && found == 0) outFirst[0] = obj;
                    ++found;
                    if (found > 512) return found;
                }
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    return found;
}


// 一次性构建"类名 -> klass"表：klass+0x18 是类名指针
struct ClsEntry { char name[56]; uintptr_t klass; };
static ClsEntry g_clsMap[400000];
static int g_clsMapCount = 0;
static bool g_clsMapBuilt = false;

static bool readCStr8(uintptr_t p, char* out, int cap) {
    SIZE_T got = 0;
    if (!p || !ReadProcessMemory(GetCurrentProcess(), (void*)p, out, cap - 1, &got) || got < 2) return false;
    out[cap - 1] = 0;
    for (int i = 0; i < cap - 1 && out[i]; ++i) {
        const unsigned char c = (unsigned char)out[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return out[0] != 0;
}

static void buildClassMap() {
    if (g_clsMapBuilt) return;
    g_clsMapBuilt = true;
    const uintptr_t kLo = 0x50000000000ull, kHi = 0x500060000000ull;
    const DWORD t0 = GetTickCount();
    uintptr_t p = kLo;
    MEMORY_BASIC_INFORMATION mbi{};
    while (p < kHi && g_clsMapCount < 400000) {
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            const uint8_t* b = (const uint8_t*)base;
            for (uintptr_t off = 0; off + 0x20 < size; off += 8) {
                const uintptr_t k = base + off;
                // 校验：+0x00 image 指针必须也在元数据区，+0x18 name 指针是可读字符串
                const uintptr_t imagePtr = *(const uintptr_t*)(b + off);
                if (imagePtr < kLo || imagePtr >= kHi) continue;
                uintptr_t namePtr = *(const uintptr_t*)(b + off + 0x18);
                if (namePtr < kLo || namePtr >= kHi) continue;
                char nm[56];
                if (!readCStr8(namePtr, nm, sizeof(nm))) continue;
                if (strlen(nm) < 3) continue;
                strncpy(g_clsMap[g_clsMapCount].name, nm, sizeof(g_clsMap[g_clsMapCount].name) - 1);
                g_clsMap[g_clsMapCount].name[sizeof(g_clsMap[g_clsMapCount].name) - 1] = 0;
                g_clsMap[g_clsMapCount].klass = k;
                ++g_clsMapCount;
                if (g_clsMapCount >= 400000) break;
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    logLine("cls: class map built, %d entries (%ums)", g_clsMapCount, GetTickCount() - t0);
}

static uintptr_t findClassByName(const char* name) {
    buildClassMap();
    for (int i = 0; i < g_clsMapCount; ++i) {
        if (strcmp(g_clsMap[i].name, name) == 0) return g_clsMap[i].klass;
    }
    return 0;
}

// 按类名查找：输入 cls.txt，打 cls_find.now

// 在全类名表里搜子串（看 7.1 是否有可读的替代类名）
static void classGrepProbe() {
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\grep.txt", "r");
    if (!f) { logLine("grep: grep.txt missing"); return; }
    char pat[64] = {0};
    if (!fgets(pat, sizeof(pat), f)) { fclose(f); return; }
    fclose(f);
    size_t n = strlen(pat);
    while (n && (pat[n - 1] == '\r' || pat[n - 1] == '\n')) pat[--n] = 0;
    if (!n) return;
    buildClassMap();
    int hits = 0;
    for (int i = 0; i < g_clsMapCount && hits < 40; ++i) {
        if (!strstr(g_clsMap[i].name, pat)) continue;
        logLine("grep[%s]: %s  klass=%p", pat, g_clsMap[i].name, (void*)g_clsMap[i].klass);
        ++hits;
    }
    logLine("grep[%s]: %d hit(s) of %d classes", pat, hits, g_clsMapCount);
}

static void findClassProbe() {
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\cls.txt", "r");
    if (!f) { logLine("cls: cls.txt missing"); return; }
    static char names[64][128];
    int nameCount = 0;
    while (nameCount < 64 && fgets(names[nameCount], sizeof(names[0]), f)) {
        size_t k = strlen(names[nameCount]);
        while (k && (names[nameCount][k - 1] == '\r' || names[nameCount][k - 1] == '\n')) names[nameCount][--k] = 0;
        if (k) ++nameCount;
    }
    fclose(f);
    for (int ni = 0; ni < nameCount; ++ni) {
    const char* name = names[ni];
    if (!name[0]) continue;

    const DWORD t0 = GetTickCount();
    const uintptr_t klass = findClassByName(name);
    if (!klass) {
        logLine("cls[%s]: NOT FOUND", name);
        continue;
    }
    uintptr_t inst = 0;
    const int n2 = countInstances(klass, &inst, 700);
    logLine("cls[%s]: klass=%p instances=%d first=%p (%ums)", name, (void*)klass, n2, (void*)inst,
            GetTickCount() - t0);
    }
}


// 元素视野探针：快照 -> 按住鼠标中键 -> 再快照 -> 比较哪个字节变了
static int collectInstances(uintptr_t klass, uintptr_t* out, int cap, int maxScanMs) {
    if (!klass || !out || cap <= 0) return 0;
    const DWORD t0 = GetTickCount();
    int found = 0;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t limit = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    while (p < limit && found < cap) {
        if (GetTickCount() - t0 > (DWORD)maxScanMs) break;
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) && size >= 16) {
            const uintptr_t* q = (const uintptr_t*)base;
            const uintptr_t words = size / 8;
            for (uintptr_t i = 0; i < words && found < cap; ++i) {
                if (q[i] != klass) continue;
                const uintptr_t obj = base + i * 8;
                const uintptr_t mon = *(const uintptr_t*)(obj + 8);
                if (mon > 0x1000) continue;
                out[found++] = obj;
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    return found;
}

static void elDiffProbe() {
    const uintptr_t klass = findClassByName("ElementView");
    if (!klass) { logLine("el: ElementView class not found"); return; }
    static uintptr_t objs[64];
    const int n = collectInstances(klass, objs, 64, 2500);
    logLine("el: %d ElementView instance(s), sampling 0x60 bytes each", n);
    if (n <= 0) return;

    static uint8_t before[64][0x60];
    static uint8_t after[64][0x60];
    SIZE_T got = 0;
    for (int i = 0; i < n; ++i) {
        ReadProcessMemory(GetCurrentProcess(), (void*)objs[i], before[i], sizeof(before[i]), &got);
    }

    logLine("el: injecting MMB down via RawInput hook ...");
    g_injectMmb = 1;
    Sleep(1500);
    for (int i = 0; i < n; ++i) {
        ReadProcessMemory(GetCurrentProcess(), (void*)objs[i], after[i], sizeof(after[i]), &got);
    }
    g_injectMmb = 2;
    Sleep(200);

    int reported = 0;
    for (int i = 0; i < n && reported < 20; ++i) {
        for (int off = 0x10; off < 0x60; ++off) {
            if (before[i][off] == after[i][off]) continue;
            logLine("el: obj[%d]=%p +0x%02X  %02X -> %02X", i, (void*)objs[i], off, before[i][off], after[i][off]);
            ++reported;
            break;
        }
    }
    logLine("el: diff done (%d object(s) changed)", reported);
}


// 元素视野：先只读，看 ElementViewRenderer 实例里有什么字段
static void elDumpProbe() {
    const uintptr_t klass = findClassByName("ElementViewRenderer");
    if (!klass) { logLine("el: ElementViewRenderer class not found"); return; }
    static uintptr_t objs[16];
    const int n = collectInstances(klass, objs, 16, 2500);
    logLine("el: %d ElementViewRenderer instance(s) (klass=%p)", n, (void*)klass);
    SIZE_T got = 0;
    for (int i = 0; i < n && i < 6; ++i) {
        uint8_t buf[0x80] = {0};
        ReadProcessMemory(GetCurrentProcess(), (void*)objs[i], buf, sizeof(buf), &got);
        char hex[400];
        int hn = 0;
        for (int k = 0; k < 0x40 && hn < 380; k += 16) {
            hn += snprintf(hex + hn, sizeof(hex) - hn, "%s%02X", k ? " " : "", buf[k]);
            for (int j = 1; j < 16; ++j) hn += snprintf(hex + hn, sizeof(hex) - hn, " %02X", buf[k + j]);
        }
        logLine("el: obj[%d]=%p  %s", i, (void*)objs[i], hex);
        for (int off = 0x10; off < 0x50; off += 8) {
            const int iv = *(const int*)(buf + off);
            const float fv = *(const float*)(buf + off);
            logLine("el:   +0x%02X  i=%-12d f=%.3f", off, iv, fv);
        }
    }
}

// 试着直接把 ElementViewRenderer 的 enabled 打开（如果它是 Behaviour）
static void elEnableProbe() {
    const uintptr_t klass = findClassByName("ElementViewRenderer");
    if (!klass) { logLine("el: class not found"); return; }
    static uintptr_t objs[16];
    const int n = collectInstances(klass, objs, 16, 2500);
    logLine("el: enabling %d renderer instance(s) ...", n);
    using SetEnabledFn = void (*)(void* self, unsigned char value, void* mi);
    auto fn = (SetEnabledFn)(overlayMod() + 0x19BD76B0ull);
    int poked = 0;
    for (int i = 0; i < n; ++i) {
        if (objs[i] < 0x60000000000ull) { logLine("el: skip %p (metadata, not heap)", (void*)objs[i]); continue; }
        fn((void*)objs[i], 1, nullptr);
        ++poked;
        logLine("el: set_enabled(true) on %p", (void*)objs[i]);
    }
    logLine("el: poked %d heap instance(s)", poked);
    logLine("el: enable probe done (if nothing appears, it is not a Behaviour)");
}


// 元素视野第三条路：找 MonoInLevelMainPage 实例 -> get_elemViewButton() -> Toggle.set_isOn(true)
// get_elemViewButton 0x121D66B0   Toggle.set_isOn 0x19D12D50
static void elButtonProbe(bool on) {
    const uintptr_t mod = overlayMod();
    const uintptr_t klass = findClassByName("MonoInLevelMainPage");
    if (!klass) { logLine("elbtn: MonoInLevelMainPage class not found"); return; }
    static uintptr_t objs[8];
    const int n = collectInstances(klass, objs, 8, 2500);
    logLine("elbtn: MonoInLevelMainPage instances=%d klass=%p", n, (void*)klass);
    using GetBtnFn = void* (*)(void* self, void* mi);
    using SetOnFn = void (*)(void* self, unsigned char v, void* mi);
    auto getBtn = (GetBtnFn)(mod + 0x121D66B0ull);
    auto setOn = (SetOnFn)(mod + 0x19D12D50ull);
    for (int i = 0; i < n; ++i) {
        void* tog = getBtn((void*)objs[i], nullptr);
        logLine("elbtn: inst[%d]=%p toggle=%p", i, (void*)objs[i], tog);
        if (!tog) continue;
        setOn(tog, (unsigned char)(on ? 1 : 0), nullptr);
        logLine("elbtn: isOn(%d) on toggle %p", on ? 1 : 0, tog);
    }
}


// ActorUtils::RefreshElemViewHandler() 0x13ACD620（静态，无参）
static void elRefreshProbe() {
    const uintptr_t mod = overlayMod();
    using RefreshFn = void (*)(void* mi);
    logLine("elref: calling ActorUtils.RefreshElemViewHandler() ...");
    ((RefreshFn)(mod + 0x13ACD620ull))(nullptr);
    logLine("elref: returned");
}


// ---------------------------------------------------------------- NoClip （写位置版）
// Akebi 做法：每帧 newPos = pos + dir*speed*dt 然后写回实体的位置字段
static void* g_playerEnt = nullptr;

static void* findPlayerEntity() {
    const uintptr_t mod = overlayMod();
    if (!mod) return nullptr;
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    static const char* kNames[] = {"Avatar", "BaseActor", "GadgetActor"};
    for (int n = 0; n < 3; ++n) {
        const uintptr_t klass = findClassByName(kNames[n]);
        if (!klass) continue;
        static uintptr_t objs[32];
        const int cnt = collectInstances(klass, objs, 32, 1200);
        for (int i = 0; i < cnt; ++i) {
            const float* p3 = (const float*)((uint8_t*)objs[i] + 0x38c);
            const float dx = p3[0] - av[0], dy = p3[1] - av[1], dz = p3[2] - av[2];
            if (fabsf(dx) < 3.f && fabsf(dy) < 3.f && fabsf(dz) < 3.f) {
                logLine("noclip: player entity = %p (%s) pos %.1f %.1f %.1f", (void*)objs[i], kNames[n],
                        p3[0], p3[1], p3[2]);
                return (void*)objs[i];
            }
        }
    }
    return nullptr;
}

static void entProbe() {
    const uintptr_t mod = overlayMod();
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    logLine("ent: avatar pos = %.2f %.2f %.2f", av[0], av[1], av[2]);
    static const char* kNames[] = {"Avatar", "BaseActor", "GadgetActor", "VCHumanoidMove"};
    for (int n = 0; n < 4; ++n) {
        const uintptr_t klass = findClassByName(kNames[n]);
        if (!klass) { logLine("ent: %s class missing", kNames[n]); continue; }
        static uintptr_t objs[32];
        const int cnt = collectInstances(klass, objs, 32, 1200);
        logLine("ent: %s instances=%d", kNames[n], cnt);
        for (int i = 0; i < cnt && i < 8; ++i) {
            const float* p3 = (const float*)((uint8_t*)objs[i] + 0x38c);
            logLine("ent:   %s[%d]=%p +38c=%.1f %.1f %.1f", kNames[n], i, (void*)objs[i], p3[0], p3[1],
                    p3[2]);
        }
    }
}

static void applyNoClipTick() {
    const uintptr_t mod = overlayMod();
    if (!mod || !g_playerEnt) return;
    using GetVec3 = void (*)(void* retBuf, void* mi);
    using GetDtFn = float (*)(void* mi);
    using SetVelFn = void (*)(void* self, void* vec, void* mi);
    float dt = ((GetDtFn)(mod + 0x19BE86F0ull))(nullptr);
    if (dt <= 0.f || dt > 0.2f) dt = 0.016f;

    float* p3 = (float*)((uint8_t*)g_playerEnt + 0x38c);
    float mvx = 0.f, mvz = 0.f, mvy = 0.f;
    const float fx = g_camFrame.fwd[0], fz = g_camFrame.fwd[2];
    const float rx = g_camFrame.right[0], rz = g_camFrame.right[2];
    const bool kW = (GetAsyncKeyState('W') & 0x8000) != 0;
    const bool kS = (GetAsyncKeyState('S') & 0x8000) != 0;
    const bool kA = (GetAsyncKeyState('A') & 0x8000) != 0;
    const bool kD = (GetAsyncKeyState('D') & 0x8000) != 0;
    if (kW) { mvx += fx; mvz += fz; }
    if (kS) { mvx -= fx; mvz -= fz; }
    if (kD) { mvx += rx; mvz += rz; }
    if (kA) { mvx -= rx; mvz -= rz; }
    const float ml = sqrtf(mvx * mvx + mvz * mvz);
    if (ml > 1e-4f) { mvx /= ml; mvz /= ml; }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) mvy = 1.f;
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) mvy = -1.f;

    p3[0] += mvx * g_flySpeed * dt;
    p3[1] += mvy * g_flySpeed * dt;
    p3[2] += mvz * g_flySpeed * dt;

    // 顺手把骨架速度清零，免得物理又把人拖回去
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* rb = *(void**)((uint8_t*)a + 0x570);
        if (!rb || *(uint64_t*)rb != 0x500014137b0ull) continue;
        float v0[3] = {0.f, 0.f, 0.f};
        ((SetVelFn)(mod + 0x19C5E130ull))(rb, v0, nullptr);
    }
}


// 在 Avatar/BaseActor 实例里找"真实坐标字段"：扫 +/-0x800 找和 GetAvatarPos 匹配的浮点三元组
static void posFieldProbe() {
    const uintptr_t mod = overlayMod();
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    logLine("posf: avatar pos = %.2f %.2f %.2f", av[0], av[1], av[2]);

    static const char* kNames[] = {"Avatar", "BaseActor"};
    for (int n = 0; n < 2; ++n) {
        const uintptr_t klass = findClassByName(kNames[n]);
        if (!klass) continue;
        static uintptr_t objs[16];
        const int cnt = collectInstances(klass, objs, 16, 1200);
        logLine("posf: %s instances=%d", kNames[n], cnt);
        for (int i = 0; i < cnt; ++i) {
            uint8_t* base = (uint8_t*)objs[i];
            static uint8_t buf[0x800];
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), base, buf, sizeof(buf), &got) || got < 0x40) {
                logLine("posf:   %s[%d]=%p read failed", kNames[n], i, (void*)base);
                continue;
            }
            const uint8_t* b = buf;
            const int lim = (int)(got < sizeof(buf) ? got : sizeof(buf));
            int hits = 0;
            for (int off = 0x10; off + 12 < lim && hits < 3; off += 4) {
                const float* f = (const float*)(b + off);
                if (fabsf(f[0] - av[0]) < 1.5f && fabsf(f[1] - av[1]) < 1.5f &&
                    fabsf(f[2] - av[2]) < 1.5f) {
                    logLine("posf:   %s[%d]=%p  +0x%X = %.2f %.2f %.2f", kNames[n], i, (void*)base, off,
                            f[0], f[1], f[2]);
                    ++hits;
                }
            }
        }
    }
}


// ---------------------------------------------------------------- Transform 路径：从 VCHumanoidMove 拿 footTrans，再读/写世界坐标
// Transform.get_position 0x19C00BD0（sret）   Transform::SetPosition 0x15C4A80
static void* g_playerXf = nullptr;

static bool readXfPos(void* xf, float* out) {
    const uintptr_t mod = overlayMod();
    using GetPosFn = void (*)(void* ret, void* self, void* mi);
    float buf[4] = {0, 0, 0, 0};
    ((GetPosFn)(mod + 0x19C00BD0ull))(buf, xf, nullptr);
    const float l = sqrtf(buf[0] * buf[0] + buf[1] * buf[1] + buf[2] * buf[2]);
    if (!(l > 0.01f) || l > 1e6f) return false;
    out[0] = buf[0];
    out[1] = buf[1];
    out[2] = buf[2];
    return true;
}

static void xfProbe() {
    const uintptr_t mod = overlayMod();
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    logLine("xf: avatar pos %.2f %.2f %.2f", av[0], av[1], av[2]);

    const long total = g_vchmInstCount;
    int found = 0;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        for (int f = 0; f < 2; ++f) {
            const int off = f == 0 ? 0x700 : 0x6f0;
            void* xf = *(void**)((uint8_t*)a + off);
            if (!xf) continue;
            float p[3] = {0, 0, 0};
            if (!readXfPos(xf, p)) continue;
            const float d = sqrtf((p[0] - av[0]) * (p[0] - av[0]) + (p[1] - av[1]) * (p[1] - av[1]) +
                                  (p[2] - av[2]) * (p[2] - av[2]));
            logLine("xf: vchm[%ld]=%p +0x%X -> xf=%p pos %.2f %.2f %.2f  d=%.2f", i, a, off, xf, p[0], p[1],
                    p[2], d);
            if (d < 3.f && !g_playerXf) {
                g_playerXf = xf;
                logLine("xf: PLAYER transform = %p", xf);
            }
            ++found;
            if (found > 24) return;
        }
    }
    logLine("xf: scanned %d transform(s)", found);
}

using SetXfPosFn = void (*)(void* self, float x, float y, float z, void* mi);
static void applyXfFlyTick() {
    const uintptr_t mod = overlayMod();
    if (!mod || !g_playerXf) return;
    using GetDtFn = float (*)(void* mi);
    float dt = ((GetDtFn)(mod + 0x19BE86F0ull))(nullptr);
    if (dt <= 0.f || dt > 0.2f) dt = 0.016f;

    float p[3] = {0, 0, 0};
    if (!readXfPos(g_playerXf, p)) return;

    float mvx = 0.f, mvz = 0.f, mvy = 0.f;
    const float fx = g_camFrame.fwd[0], fz = g_camFrame.fwd[2];
    const float rx = g_camFrame.right[0], rz = g_camFrame.right[2];
    if (GetAsyncKeyState('W') & 0x8000) { mvx += fx; mvz += fz; }
    if (GetAsyncKeyState('S') & 0x8000) { mvx -= fx; mvz -= fz; }
    if (GetAsyncKeyState('D') & 0x8000) { mvx += rx; mvz += rz; }
    if (GetAsyncKeyState('A') & 0x8000) { mvx -= rx; mvz -= rz; }
    const float ml = sqrtf(mvx * mvx + mvz * mvz);
    if (ml > 1e-4f) { mvx /= ml; mvz /= ml; }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) mvy = 1.f;
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) mvy = -1.f;

    const float nx = p[0] + mvx * g_flySpeed * dt;
    const float ny = p[1] + mvy * g_flySpeed * dt;
    const float nz = p[2] + mvz * g_flySpeed * dt;
    ((SetXfPosFn)(mod + 0x15C4A80ull))(g_playerXf, nx, ny, nz, nullptr);

    static int n = 0;
    if ((n++ % 60) == 0) {
        float chk[3] = {0, 0, 0};
        readXfPos(g_playerXf, chk);
        logLine("xfly: want %.1f %.1f %.1f  now %.1f %.1f %.1f", nx, ny, nz, chk[0], chk[1], chk[2]);
    }
}


// ---------------------------------------------------------------- 坐标映射探针 + 本地坐标写入
// 实测：Transform.get_position 读出来是镜像的局部坐标（x/z 取负，y 偏移 -1212.8）
static float g_mapAx = 0.f, g_mapAy = 0.f, g_mapAz = 0.f;
static bool g_mapReady = false;

static void computePosMapping() {
    if (!g_playerXf) return;
    float lx[3] = {0, 0, 0};
    if (!readXfPos(g_playerXf, lx)) return;
    const uintptr_t mod = overlayMod();
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    g_mapAx = lx[0] + av[0];
    g_mapAy = lx[1] - av[1];
    g_mapAz = lx[2] + av[2];
    g_mapReady = true;
    logLine("map: local = (-x + %.2f, y + %.2f, -z + %.2f)  (local %.1f %.1f %.1f / world %.1f %.1f %.1f)",
            g_mapAx, g_mapAy, g_mapAz, lx[0], lx[1], lx[2], av[0], av[1], av[2]);
}

using SetLocalPosFn = void (*)(void* self, float x, float y, float z, void* mi);
static void applyLocalFlyTick() {
    const uintptr_t mod = overlayMod();
    if (!mod || !g_playerXf) return;
    if (!g_mapReady) computePosMapping();
    if (!g_mapReady) return;
    using GetDtFn = float (*)(void* mi);
    float dt = ((GetDtFn)(mod + 0x19BE86F0ull))(nullptr);
    if (dt <= 0.f || dt > 0.2f) dt = 0.016f;

    float lx[3] = {0, 0, 0};
    if (!readXfPos(g_playerXf, lx)) return;
    // 局部 -> 世界
    float wx = -lx[0] + g_mapAx;
    float wy = lx[1] - g_mapAy;
    float wz = -lx[2] + g_mapAz;

    float mvx = 0.f, mvz = 0.f, mvy = 0.f;
    const float fx = g_camFrame.fwd[0], fz = g_camFrame.fwd[2];
    const float rx = g_camFrame.right[0], rz = g_camFrame.right[2];
    if (GetAsyncKeyState('W') & 0x8000) { mvx += fx; mvz += fz; }
    if (GetAsyncKeyState('S') & 0x8000) { mvx -= fx; mvz -= fz; }
    if (GetAsyncKeyState('D') & 0x8000) { mvx += rx; mvz += rz; }
    if (GetAsyncKeyState('A') & 0x8000) { mvx -= rx; mvz -= rz; }
    const float ml = sqrtf(mvx * mvx + mvz * mvz);
    if (ml > 1e-4f) { mvx /= ml; mvz /= ml; }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) mvy = 1.f;
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) mvy = -1.f;

    const float twx = wx + mvx * g_flySpeed * dt;
    const float twy = wy + mvy * g_flySpeed * dt;
    const float twz = wz + mvz * g_flySpeed * dt;
    // 世界 -> 局部
    const float nlx = -twx + g_mapAx;
    const float nly = twy + g_mapAy;
    const float nlz = -twz + g_mapAz;
    ((SetLocalPosFn)(mod + 0x15C4990ull))(g_playerXf, nlx, nly, nlz, nullptr);

    static int n = 0;
    if ((n++ % 45) == 0) {
        float chk[3] = {0, 0, 0};
        readXfPos(g_playerXf, chk);
        logLine("lfly: want local %.1f %.1f %.1f  now %.1f %.1f %.1f", nlx, nly, nlz, chk[0], chk[1], chk[2]);
    }
}


// 用"镜像局部坐标"去找实体里的位置字段（先从 VCHumanoidMove.footTrans 读出来一个坐标作为目标）
static void localPosProbe() {
    const uintptr_t mod = overlayMod();
    // 1) 找一个"有非零水平坐标"的 transform 当目标
    float target[3] = {0, 0, 0};
    bool have = false;
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512 && !have; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* xf = *(void**)((uint8_t*)a + 0x700);
        if (!xf) continue;
        float p3[3] = {0, 0, 0};
        if (!readXfPos(xf, p3)) continue;
        if (fabsf(p3[0]) > 10.f || fabsf(p3[2]) > 10.f) {
            target[0] = p3[0];
            target[1] = p3[1];
            target[2] = p3[2];
            have = true;
            logLine("locf: target(local) %.2f %.2f %.2f from vchm[%ld]", target[0], target[1], target[2], i);
        }
    }
    if (!have) { logLine("locf: no transform with non-zero horizontal pos"); return; }

    static const char* kNames[] = {"Avatar", "BaseActor", "GadgetActor", "VCHumanoidMove"};
    for (int n = 0; n < 4; ++n) {
        const uintptr_t klass = findClassByName(kNames[n]);
        if (!klass) continue;
        static uintptr_t objs[16];
        const int cnt = collectInstances(klass, objs, 16, 1200);
        for (int i = 0; i < cnt; ++i) {
            static uint8_t buf[0x1000];
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (void*)objs[i], buf, sizeof(buf), &got) || got < 0x40)
                continue;
            const int lim = (int)(got < sizeof(buf) ? got : sizeof(buf));
            int hits = 0;
            for (int off = 0x10; off + 12 < lim && hits < 2; off += 4) {
                const float* f = (const float*)(buf + off);
                if (fabsf(f[0] - target[0]) < 1.5f && fabsf(f[1] - target[1]) < 1.5f &&
                    fabsf(f[2] - target[2]) < 1.5f) {
                    logLine("locf:   %s[%d]=%p  +0x%X = %.2f %.2f %.2f", kNames[n], i, (void*)objs[i], off,
                            f[0], f[1], f[2]);
                    ++hits;
                }
            }
        }
    }
    logLine("locf: done");
}


// mode 7：用"取正后的水平坐标"认出玩家的 Transform，然后直接 SetPosition(世界坐标)
struct V3f { float x, y, z; };
using SetPosV3Fn = void (*)(void* self, V3f v, void* mi);

static bool pickPlayerTransform() {
    const uintptr_t mod = overlayMod();
    if (!mod) return false;
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);

    float bestScore = 1e9f;
    void* bestXf = nullptr;
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* xf = *(void**)((uint8_t*)a + 0x700);
        if (!xf) continue;
        float p3[3] = {0, 0, 0};
        if (!readXfPos(xf, p3)) continue;
        const float sx = fabsf(-p3[0] - av[0]) + fabsf(-p3[2] - av[2]);
        if (sx < bestScore) {
            bestScore = sx;
            bestXf = xf;
        }
    }
    if (bestXf && bestScore < 50.f) {
        g_playerXf = bestXf;
        logLine("xf7: player transform = %p (score %.2f)", bestXf, bestScore);
        return true;
    }
    logLine("xf7: no candidate (best score %.1f)", bestScore);
    return false;
}

static void applyFly7Tick() {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    if (!g_playerXf && !pickPlayerTransform()) return;
    using GetVec3 = void (*)(void* retBuf, void* mi);
    using GetDtFn = float (*)(void* mi);
    float dt = ((GetDtFn)(mod + 0x19BE86F0ull))(nullptr);
    if (dt <= 0.f || dt > 0.2f) dt = 0.016f;

    float av[4] = {0, 0, 0, 0};
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    if (fabsf(av[0]) < 1.f) return;

    float mvx = 0.f, mvz = 0.f, mvy = 0.f;
    const float fx = g_camFrame.fwd[0], fz = g_camFrame.fwd[2];
    const float rx = g_camFrame.right[0], rz = g_camFrame.right[2];
    if (GetAsyncKeyState('W') & 0x8000) { mvx += fx; mvz += fz; }
    if (GetAsyncKeyState('S') & 0x8000) { mvx -= fx; mvz -= fz; }
    if (GetAsyncKeyState('D') & 0x8000) { mvx += rx; mvz += rz; }
    if (GetAsyncKeyState('A') & 0x8000) { mvx -= rx; mvz -= rz; }
    const float ml = sqrtf(mvx * mvx + mvz * mvz);
    if (ml > 1e-4f) { mvx /= ml; mvz /= ml; }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) mvy = 1.f;
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) mvy = -1.f;

    V3f target{av[0] + mvx * g_flySpeed * dt, av[1] + mvy * g_flySpeed * dt,
               av[2] + mvz * g_flySpeed * dt};
    using SetPosPtrFn = void (*)(void* self, void* vecPtr, void* mi);
    ((SetPosPtrFn)(mod + 0x15C4A80ull))(g_playerXf, &target, nullptr);

    static int n = 0;
    if ((n++ % 45) == 0) {
        float now[4] = {0, 0, 0, 0};
        ((GetVec3)(mod + 0x13AAF060ull))(now, nullptr);
        logLine("fly7: want %.1f %.1f %.1f  actual %.1f %.1f %.1f", target.x, target.y, target.z, now[0],
                now[1], now[2]);
    }
}


// mode 8/9：用 Rigidbody 的位置接口（set_position 0x19C5E570 / MovePositionDelta 0x19C5E650）
// 和 set_velocity 一样用"指针传 Vector3"（这个约定已经在��行 mode 2 里验证过）
static int g_rbKinApplied = 0;

static void restoreRbState() {
    if (!g_rbKinApplied) return;
    g_rbKinApplied = 0;
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    using SetBoolFn = void (*)(void* self, unsigned char v, void* mi);
    auto setKin = (SetBoolFn)(mod + 0x19C5E250ull);
    auto setDet = (SetBoolFn)(mod + 0x19C5E520ull);
    int n = 0;
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* rb = *(void**)((uint8_t*)a + 0x570);
        if (!rb || *(uint64_t*)rb != 0x500014137b0ull) continue;
        setKin(rb, 0, nullptr);
        setDet(rb, 1, nullptr);
        ++n;
    }
    logLine("noclip: rigidbody state restored on %d body(s)", n);
}

static void applyRbPosTick(int variant) {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    using GetVec3 = void (*)(void* retBuf, void* mi);
    using GetDtFn = float (*)(void* mi);
    using SetVecFn = void (*)(void* self, void* vec, void* mi);
    float dt = ((GetDtFn)(mod + 0x19BE86F0ull))(nullptr);
    if (dt <= 0.f || dt > 0.2f) dt = 0.016f;

    float av[4] = {0, 0, 0, 0};
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    if (fabsf(av[0]) < 1.f) return;

    float mvx = 0.f, mvz = 0.f, mvy = 0.f;
    const float fx = g_camFrame.fwd[0], fz = g_camFrame.fwd[2];
    const float rx = g_camFrame.right[0], rz = g_camFrame.right[2];
    if (GetAsyncKeyState('W') & 0x8000) { mvx += fx; mvz += fz; }
    if (GetAsyncKeyState('S') & 0x8000) { mvx -= fx; mvz -= fz; }
    if (GetAsyncKeyState('D') & 0x8000) { mvx += rx; mvz += rz; }
    if (GetAsyncKeyState('A') & 0x8000) { mvx -= rx; mvz -= rz; }
    const float ml = sqrtf(mvx * mvx + mvz * mvz);
    if (ml > 1e-4f) { mvx /= ml; mvz /= ml; }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) mvy = 1.f;
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) mvy = -1.f;

    // 安全阀：坐标爆掉就自动停
    if (fabsf(av[0]) > 100000.f || fabsf(av[2]) > 100000.f) {
        g_flyMode = 0;
        logLine("rbpos: position runaway (%.1f %.1f), fly forced off", av[0], av[2]);
        return;
    }
    float target[3] = {av[0] + mvx * g_flySpeed * dt, av[1] + mvy * g_flySpeed * dt,
                       av[2] + mvz * g_flySpeed * dt};
    // delta 用"镜像局部"空间：x/z 取负
    float delta[3] = {-mvx * g_flySpeed * dt, mvy * g_flySpeed * dt, -mvz * g_flySpeed * dt};
    float zero[3] = {0.f, 0.f, 0.f};

    int n = 0;
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* rb = *(void**)((uint8_t*)a + 0x570);
        if (!rb || *(uint64_t*)rb != 0x500014137b0ull) continue;
        if (variant == 9) {
            using SetBoolFn = void (*)(void* self, unsigned char v, void* mi);
            ((SetBoolFn)(mod + 0x19C5E250ull))(rb, 1, nullptr);          // set_isKinematic(true)
            g_rbKinApplied = 1;
            ((SetBoolFn)(mod + 0x19C5E520ull))(rb, 0, nullptr);          // detectCollisions(false)
            ((SetVecFn)(mod + 0x19C5E650ull))(rb, delta, nullptr);      // MovePositionDelta
        } else {
            ((SetVecFn)(mod + 0x19C5E570ull))(rb, target, nullptr);      // set_position
        }
        ((SetVecFn)(mod + 0x19C5E130ull))(rb, zero, nullptr);            // 顺手清零速度
        ++n;
    }
    static int beat = 0;
    if ((beat++ % 60) == 0) {
        float now[4] = {0, 0, 0, 0};
        ((GetVec3)(mod + 0x13AAF060ull))(now, nullptr);
        logLine("rbpos%d: rb=%d want %.1f %.1f %.1f  actual %.1f %.1f %.1f", variant, n, target[0], target[1],
                target[2], now[0], now[1], now[2]);
    }
}


// ---------------------------------------------------------------- 方法表布局探针
// 目标：找到 Il2CppClass 里"methods 数组"的偏移（用已知 RVA 的 LuaManager 验证）
// 验证方法：klass + off 指向数组，数组元素是 MethodInfo*，MethodInfo+0x0 是函数指针
static void mtabProbe() {
    const uintptr_t mod = overlayMod();
    const uintptr_t klass = findClassByName("LuaManager");
    if (!klass) { logLine("mtab: LuaManager class not found"); return; }
    const uint64_t want[3] = {mod + 0x84E5080ull, mod + 0x84E5160ull, mod + 0x84E5D10ull};
    logLine("mtab: klass=%p mod=%p want=%llX %llX %llX", (void*)klass, (void*)mod, (unsigned long long)want[0],
            (unsigned long long)want[1], (unsigned long long)want[2]);

    uint8_t kb[0x140] = {0};
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)klass, kb, sizeof(kb), &got)) {
        logLine("mtab: klass read failed");
        return;
    }
    for (int off = 0x00; off + 8 <= (int)got; off += 8) {
        const uintptr_t arr = *(const uintptr_t*)(kb + off);
        if (arr < 0x10000ull) continue;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery((void*)arr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) continue;
        uint64_t entries[48] = {0};
        SIZE_T g2 = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)arr, entries, sizeof(entries), &g2) || g2 < 16)
            continue;
        int matched = 0, plausible = 0;
        for (int i = 0; i < (int)(g2 / 8); ++i) {
            const uintptr_t mi = entries[i];
            if (mi < 0x10000ull) continue;
            MEMORY_BASIC_INFORMATION m2{};
            if (!VirtualQuery((void*)mi, &m2, sizeof(m2)) || m2.State != MEM_COMMIT) continue;
            uint64_t fn = 0;
            SIZE_T g3 = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (void*)mi, &fn, 8, &g3) || g3 != 8) continue;
            if (fn >= mod && fn < mod + 0x40000000ull) {
                ++plausible;
                for (int w = 0; w < 3; ++w)
                    if (fn == want[w]) { ++matched; break; }
            }
        }
        if (plausible >= 3 || matched > 0) {
            logLine("mtab: klass+0x%02X -> %p  plausible=%d matched=%d", off, (void*)arr, plausible, matched);
        }
    }
    logLine("mtab: done");
}


// 通用：按类名或 TypeInfo 把方法表打出来（klass+0x20 = 方法数组）
static void mdumpProbe() {
    const uintptr_t mod = overlayMod();
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\mclass.txt", "r");
    if (!f) { logLine("mdump: mclass.txt missing"); return; }
    char line[128] = {0};
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }
    fclose(f);
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;

    uintptr_t klass = 0;
    if (strncmp(line, "0x", 2) == 0 || strncmp(line, "0X", 2) == 0) {
        klass = (uintptr_t)strtoull(line + 2, nullptr, 16);
    } else {
        klass = findClassByName(line);
    }
    if (!klass) { logLine("mdump[%s]: class not found", line); return; }

    uintptr_t arr = 0;
    ReadProcessMemory(GetCurrentProcess(), (void*)(klass + 0x20), &arr, 8, nullptr);
    logLine("mdump[%s]: klass=%p methods=%p", line, (void*)klass, (void*)arr);
    if (!arr) return;
    uint64_t entries[64] = {0};
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)arr, entries, sizeof(entries), &got)) return;
    int shown = 0;
    for (int i = 0; i < (int)(got / 8) && shown < 40; ++i) {
        const uintptr_t mi = entries[i];
        if (mi < 0x10000ull) continue;
        uint64_t fn = 0;
        SIZE_T g = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)mi, &fn, 8, &g) || g != 8) continue;
        if (fn < mod || fn >= mod + 0x40000000ull) continue;
        logLine("mdump[%s]: [%02d] rva=0x%llX", line, i, (unsigned long long)(fn - mod));
        ++shown;
    }
    logLine("mdump[%s]: %d method(s) shown", line, shown);
}


// 把玩家 FSM 实例的 int 字段打出来，找 _layerMaskScene（Akebi 设 2）
static void fsmDumpProbe() {
    const uintptr_t mod = overlayMod();
    // 找玩家的 VCHumanoidMove：用 footTrans 取正后匹配 avatar 坐标
    float av[4] = {0, 0, 0, 0};
    using GetVec3 = void (*)(void* retBuf, void* mi);
    ((GetVec3)(mod + 0x13AAF060ull))(av, nullptr);
    void* best = nullptr;
    float bestScore = 1e9f;
    const long total = g_vchmInstCount;
    for (long i = 0; i < total && i < 512; ++i) {
        void* a = g_vchmInst[i];
        if (!a || *(uint64_t*)a != 0x500026f1fe0ull) continue;
        void* xf = *(void**)((uint8_t*)a + 0x700);
        if (!xf) continue;
        float p3[3] = {0, 0, 0};
        if (!readXfPos(xf, p3)) continue;
        const float s = fabsf(-p3[0] - av[0]) + fabsf(-p3[2] - av[2]);
        if (s < bestScore) { bestScore = s; best = a; }
    }
    if (!best) { logLine("fsmd: no player VCHumanoidMove"); return; }
    void* fsm = *(void**)((uint8_t*)best + 0x698);
    logLine("fsmd: vchm=%p fsm=%p (score %.2f)", best, fsm, bestScore);
    if (!fsm) return;
    uint8_t buf[0x400] = {0};
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), fsm, buf, sizeof(buf), &got)) return;
    for (int off = 0x10; off + 4 <= (int)got; off += 4) {
        const int v = *(const int*)(buf + off);
        const float fv = *(const float*)(buf + off);
        const bool maskLike = (v > 0x100 && (v & 0xFFFFFF) != 0) && ((v & (v - 1)) != 0) && v < 0x7FFFFFFF;
        const bool smallInt = (v >= 0 && v <= 8);
        if (maskLike || (smallInt && fabsf(fv) > 1e-6f && fabsf(fv) < 100.f))
            logLine("fsmd: +0x%03X  int=%d (0x%X)  float=%.3f", off, v, v, fv);
    }
    logLine("fsmd: done");
}


// ---------------------------------------------------------------- 堆扫类实例计数 + 差分
// 用法：hc_a.now 存 A；生怪/spawn 后 hc_b.now 存 B 并打印"实例数增加的类"
struct HcEntry { uintptr_t klass; int count; char name[48]; };
static HcEntry g_hcA[8192];
static int g_hcACount = 0;
static std::mutex g_hcLock;

static int hcFind(uintptr_t klass, HcEntry* arr, int n) {
    for (int i = 0; i < n; ++i)
        if (arr[i].klass == klass) return i;
    return -1;
}

static int heapCountScan(HcEntry* out, int cap) {
    int n = 0;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t limit = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    while (p < limit) {
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) && size >= 0x1000) {
            SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), (void*)base, buf, sizeof(buf), &got) && got >= 16) {
                const uintptr_t* q = (const uintptr_t*)buf;
                for (uintptr_t i = 0; i + 2 < got / 8; ++i) {
                    const uintptr_t k = q[i];
                    if (k < 0x50000000000ull || k >= 0x500060000000ull) continue;
                    const uintptr_t mon = q[i + 1];
                    if (mon > 0x1000) continue;
                    int idx = hcFind(k, out, n);
                    if (idx < 0) {
                        if (n >= cap) continue;
                        // 校验：klass+0x18 必须是可读的合法类名字串
                        uintptr_t np = 0;
                        SIZE_T gk2 = 0;
                        char nm2[48] = {0};
                        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(k + 0x18), &np, 8, &gk2) ||
                            gk2 != 8 || !readCStr8(np, nm2, sizeof(nm2)))
                            continue;
                        idx = n++;
                        out[idx].klass = k;
                        out[idx].count = 0;
                        strncpy(out[idx].name, nm2, sizeof(out[idx].name) - 1);
                        out[idx].name[sizeof(out[idx].name) - 1] = 0;
                    }
                    ++out[idx].count;
                }
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    return n;
}

static void hcSnapshotA() {
    std::lock_guard<std::mutex> lk(g_hcLock);
    const DWORD t0 = GetTickCount();
    g_hcACount = heapCountScan(g_hcA, 8192);
    logLine("hcA: %d class(es) with instances (%ums)", g_hcACount, GetTickCount() - t0);
}

static void hcSnapshotB() {
    std::lock_guard<std::mutex> lk(g_hcLock);
    static HcEntry b[8192];
    const DWORD t0 = GetTickCount();
    const int nb = heapCountScan(b, 8192);
    logLine("hcB: %d class(es) (%ums). diff vs A:", nb, GetTickCount() - t0);
    int shown = 0;
    for (int i = 0; i < nb && shown < 40; ++i) {
        const int j = hcFind(b[i].klass, g_hcA, g_hcACount);
        const int oldC = (j >= 0) ? g_hcA[j].count : 0;
        const char* nm = b[i].name;
        if (strstr(nm, "`1") || strstr(nm, "`2") || strstr(nm, "`3")) continue;
        if (!strcmp(nm, "String") || !strcmp(nm, "String[]") || !strcmp(nm, "Object") ||
            !strcmp(nm, "UInt32") || !strcmp(nm, "UInt64") || !strcmp(nm, "Int32") ||
            !strcmp(nm, "Single") || !strcmp(nm, "Boolean") || !strcmp(nm, "Byte[]") ||
            !strncmp(nm, "List", 4) || !strncmp(nm, "Dict", 4) || !strncmp(nm, "HashSet", 7) ||
            !strncmp(nm, "Action", 6) || !strncmp(nm, "Func", 4) || !strncmp(nm, "Tween", 5) ||
            !strncmp(nm, "Image", 5) || !strncmp(nm, "Mask", 4) || !strncmp(nm, "ILayout", 7))
            continue;
        if (b[i].count > oldC + 1) {
            logLine("hcB:   +%d  %s  (klass=%p, %d -> %d)", b[i].count - oldC, b[i].name, (void*)b[i].klass,
                    oldC, b[i].count);
            ++shown;
        }
    }
    logLine("hcB: %d class(es) increased", shown);
}


// 用"怪物世界坐标"在堆里反查实体对象：命中就把前面那个 klass 读出来
static void entFindProbe() {
    Snapshot s = bridge::get();
    const EntInfo* mon = nullptr;
    for (const auto& e : s.ents) {
        if (e.kind == 'M') { mon = &e; break; }
    }
    if (!mon) { logLine("entf: no monster in feed"); return; }
    logLine("entf: monster id=%d typeId=%d pos %.1f %.1f %.1f dist=%.1f", mon->id, mon->typeId, mon->x,
            mon->y, mon->z, mon->dist);

    const float tx = mon->x, ty = mon->y, tz = mon->z;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t limit = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int hits = 0, scanned = 0;
    const DWORD t0 = GetTickCount();
    while (p < limit && hits < 12 && GetTickCount() - t0 < 8000) {
        if (!VirtualQuery((void*)p, &mbi, sizeof(mbi))) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) && size >= 0x1000) {
            SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), (void*)base, buf, sizeof(buf), &got) && got >= 16) {
                scanned += (int)(got >> 20);
                for (SIZE_T i = 0; i + 12 < got; i += 4) {
                    const float* f = (const float*)(buf + i);
                    if (!(f[0] == f[0] && f[1] == f[1] && f[2] == f[2])) continue;   // 排 NaN
                    if (fabsf(f[0]) < 100.f || fabsf(f[2]) < 100.f) continue;
                    if (fabsf(f[0] - tx) > 0.6f || fabsf(f[1] - ty) > 3.0f || fabsf(f[2] - tz) > 0.6f)
                        continue;
                    const uintptr_t at = base + i;
                    // 往前找 klass 指针
                    int bestOff = -1;
                    char bestName[48] = {0};
                    for (int back = 8; back <= 0x800; back += 8) {
                        const uintptr_t cand = at - back;
                        uintptr_t q0 = 0;
                        SIZE_T g = 0;
                        if (!ReadProcessMemory(GetCurrentProcess(), (void*)cand, &q0, 8, &g) || g != 8) break;
                        if (q0 < 0x50000000000ull || q0 >= 0x500060000000ull) continue;
                        uintptr_t np = 0;
                        char nm[48] = {0};
                        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(q0 + 0x18), &np, 8, &g) || g != 8)
                            continue;
                        if (!readCStr8(np, nm, sizeof(nm))) continue;
                        bestOff = back;
                        strncpy(bestName, nm, sizeof(bestName) - 1);
                        break;
                    }
                    logLine("entf: hit %p  posOffsetGuess=%d  klassName=%s  %.2f %.2f %.2f", (void*)at, bestOff,
                            bestName[0] ? bestName : "?", f[0], f[1], f[2]);
                    ++hits;
                    if (hits >= 12) break;
                }
            }
        }
        const uintptr_t next = base + size;
        if (next <= p) break;
        p = next;
    }
    logLine("entf: done, %d hit(s), ~%d MB scanned", hits, scanned);
}


// 验证"候选类"：实例数 / +0x40 坐标 / 里面找实体 id
static void entClsProbe() {
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\mclass.txt", "r");
    if (!f) { logLine("entcls: mclass.txt missing"); return; }
    char line[128] = {0};
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }
    fclose(f);
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;

    const uintptr_t klass = findClassByName(line);
    if (!klass) { logLine("entcls[%s]: class not found", line); return; }
    static uintptr_t objs[64];
    const int cnt = collectInstances(klass, objs, 64, 1500);
    logLine("entcls[%s]: klass=%p instances=%d", line, (void*)klass, cnt);
    // 拿一只怪的坐标当目标
    float target[3] = {0, 0, 0};
    bool haveTarget = false;
    {
        Snapshot s2 = bridge::get();
        for (const auto& e : s2.ents) {
            if (e.kind == 'M') { target[0] = e.x; target[1] = e.y; target[2] = e.z; haveTarget = true; break; }
        }
    }
    logLine("entcls[%s]: target monster pos %.2f %.2f %.2f (found=%d)", line, target[0], target[1], target[2],
            haveTarget ? 1 : 0);
    int posHit = 0;
    for (int i = 0; i < cnt; ++i) {
        static uint8_t buf[0x400];
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)objs[i], buf, sizeof(buf), &got)) continue;
        int at = -1;
        if (haveTarget) {
            for (int off = 0x8; off + 12 <= (int)got; off += 4) {
                const float* f = (const float*)(buf + off);
                if (!(f[0] == f[0] && f[1] == f[1] && f[2] == f[2])) continue;
                if (fabsf(f[0] - target[0]) < 0.6f && fabsf(f[1] - target[1]) < 3.f &&
                    fabsf(f[2] - target[2]) < 0.6f) { at = off; break; }
            }
        }
        int idAt = -1;
        uint32_t idVal = 0;
        for (int off = 0; off + 4 < (int)got; off += 4) {
            const uint32_t v = *(const uint32_t*)(buf + off);
            if (v > 4100000u && v < 4300000u) { idAt = off; idVal = v; break; }
        }
        if (at >= 0) {
            logLine("entcls[%s]: [%d]=%p  POS @ +0x%X = %.2f %.2f %.2f  idGuess@0x%X=%u", line, i,
                    (void*)objs[i], at, *(const float*)(buf + at), *(const float*)(buf + at + 4),
                    *(const float*)(buf + at + 8), idAt, idVal);
            ++posHit;
            if (posHit >= 6) break;
        }
    }
    logLine("entcls[%s]: %d instance(s) hold the monster position", line, posHit);
}

void Gs71TxProbe(bool on) { installTransmitProbe(on); }
void Gs71LuaProbe(bool on) { installLuaProbe(on); }

void mainThreadTick() {
    // ---- 每帧飞行：读当前位置  沿相机前方推   AvatarMoveTo ----
    applyUnityOps();
    if (g_uiProbeReq) { g_uiProbeReq = 0; hideUiProbe(); }
    if (g_luaExecReq) { g_luaExecReq = 0; luaExecProbe(); }
    if (g_clsFindReq) { g_clsFindReq = 0; findClassProbe(); }
    if (g_clsGrepReq) { g_clsGrepReq = 0; classGrepProbe(); }
    if (g_elDiffReq) { g_elDiffReq = 0; elDiffProbe(); }
    if (g_elDumpReq) { g_elDumpReq = 0; elDumpProbe(); }
    if (g_elEnReq) { g_elEnReq = 0; elEnableProbe(); }
    if (g_elBtnReq) { g_elBtnReq = 0; elButtonProbe(g_elBtnVal != 0); }
    if (g_entProbeReq) { g_entProbeReq = 0; entProbe(); g_playerEnt = findPlayerEntity(); }
    if (g_posfReq) { g_posfReq = 0; posFieldProbe(); }
    if (g_xfReq) { g_xfReq = 0; xfProbe(); }
    if (g_locfReq) { g_locfReq = 0; localPosProbe(); }
    if (g_mtabReq) { g_mtabReq = 0; mtabProbe(); }
    if (g_mdumpReq) { g_mdumpReq = 0; mdumpProbe(); }
    if (g_fsmdReq) { g_fsmdReq = 0; fsmDumpProbe(); }
    if (g_hcAReq) { g_hcAReq = 0; hcSnapshotA(); }
    if (g_hcBReq) { g_hcBReq = 0; hcSnapshotB(); }
    if (g_entfReq) { g_entfReq = 0; entFindProbe(); }
    if (g_entclsReq) { g_entclsReq = 0; entClsProbe(); }
    if (g_elRefReq) { g_elRefReq = 0; elRefreshProbe(); }
    refreshCamFrame();
    applyHideUiTick();
    menu::tickHotkeys();
    if (g_flyMode != 9) restoreRbState();
    if (g_flyMode == 8) applyRbPosTick(8);
    else if (g_flyMode == 9) applyRbPosTick(9);
    else if (g_flyMode == 7) applyFly7Tick();
    else if (g_flyMode == 6) applyLocalFlyTick();
    else if (g_flyMode == 5) applyXfFlyTick();
    else if (g_flyMode == 4) applyNoClipTick();
    else applyFlyTick();
    if (g_flyLoopOn) {
        uint8_t* m2 = (uint8_t*)GetModuleHandleA(nullptr);
        if (m2) {
            using GetVec3 = void (*)(void* retBuf, void* mi);
            using MoveFn2 = void (*)(void* posPtr, float len, void* cs, void* isWalk, void* mi);
            auto getPos = (GetVec3)((uintptr_t)m2 + 0x13AAF060ull);
            auto getCam = (GetVec3)((uintptr_t)m2 + 0x13AB7B80ull);
            auto move = (MoveFn2)((uintptr_t)m2 + 0x13AA8850ull);
            float p[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0};
            getPos(p, nullptr);
            getCam(c, nullptr);
            if (fabsf(p[0]) > 1.f) {
                // 相机  角色 的水平方
float dx = p[0] - c[0], dz = p[2] - c[2];
                const float len = sqrtf(dx * dx + dz * dz);
                if (len > 0.01f) {
                    dx /= len;
                    dz /= len;
                } else {
                    dx = 0.f;
                    dz = 1.f;
                }
                const float step = 0.35f;   // 每帧 0.35 米（ 20 m/s
                float pos[3] = {p[0] + dx * step, p[1], p[2] + dz * step};
                move(pos, 0.f, (void*)1, (void*)(intptr_t)g_flyIsWalk, nullptr);
                static int n = 0;
                if ((n++ % 60) == 0)
                    logLine("fly: from %.1f %.1f %.1f -> %.1f %.1f %.1f", p[0], p[1], p[2], pos[0],
                            pos[1], pos[2]);
            }
        }
    }
    if (!g_mainMovePending) return;
    g_mainMovePending = false;
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    float pos[3] = {g_mainMovePos[0], g_mainMovePos[1], g_mainMovePos[2]};
    if (g_mainMoveVariant == 2) {
        // AvatarGotoPoint(Vector3 pos, Action cb, Action<Vector3> updateCB, Dir dir)
        using GotoFn = void (*)(void* posPtr, void* cb, void* updateCB, void* dir, void* mi);
        auto g = (GotoFn)((uintptr_t)mod + 0x13ABA7B0ull);
        g(pos, nullptr, nullptr, (void*)0, nullptr);
        logLine("main: AvatarGotoPoint(%.2f %.2f %.2f)", pos[0], pos[1], pos[2]);
    } else {
        using MoveFn = void (*)(void* posPtr, float len, void* changeState, void* isWalk, void* mi);
        auto fn = (MoveFn)((uintptr_t)mod + 0x13AA8850ull);
        const void* cs = (g_mainMoveVariant == 0) ? (void*)1 : (void*)0;
        fn(pos, g_mainMoveLen, (void*)cs, (void*)0, nullptr);
        logLine("main: AvatarMoveTo(%.2f %.2f %.2f, len=%.1f, changeState=%d)", pos[0], pos[1],
                pos[2], g_mainMoveLen, g_mainMoveVariant == 0 ? 1 : 0);
    }
}

HRESULT WINAPI hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    // Map/Unmap 钩子已禁用：每秒百万次回调会拖垮帧率，且相机矩阵不在这些缓冲 
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

    mainThreadTick();
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

    // ---- 方案 A：跳 SDK 那个 VM 派发表的非法读取 ----
    // 崩溃指令 x17112B  movzx eax, byte ptr [rdi+rbx]   （表基址/索引非法
// 处理：把 rax  0（派发索引归零，落到合法入口），RIP += 3 跳过这条指令，继续执行
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

    // 硬件写断点命中：记录后继续执行（不修改任何东西）
    if (code == 0x80000004 && g_bpArmed.load() && ep->ContextRecord) {
        bpLogHitRegs(ep->ContextRecord);
        return EXCEPTION_CONTINUE_EXECUTION;
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
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&g_selfModule, &g_selfModule);
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
    CreateThread(nullptr, 0, camProbeWatcher, nullptr, 0, nullptr);
    camFreeStartThread();
    autoTalkStart();
    autoProbeTypesStart();
    CreateThread(nullptr, 0, worldAvatarProbeThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, scanNowWatcher, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, bpProbeFromFlag, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, bpFromFileThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, dumpAddrsFromFile, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, posWriteThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, xrefProbeThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, dumpMappedMetadata, nullptr, 0, nullptr);
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\ctor_auto.on") != INVALID_FILE_ATTRIBUTES) {
        installCtorCapture(true);
        installVchmCtorCapture();
    }
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") != INVALID_FILE_ATTRIBUTES) {
        installMetadataHooks();
        CreateThread(nullptr, 0, huntMhyBuffer, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, findCollisionHelper, nullptr, 0, nullptr);
    }
    CreateThread(nullptr, 0, pairProbeThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, strictClassThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, cbSizeLogThread, nullptr, 0, nullptr);
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
                // is handled by the dedicated external tool instead (see 新电脑部 md §11).
                logLine("fps probe requested but disabled in this build");
            }
        }
    }
    CreateThread(nullptr, 0, stackProbeThread, nullptr, 0, nullptr);

    logLine("overlay ready");
    return 0;
}

}  // namespace




// ---------------------------------------------------------------- 导出解密后的 IL2CPP 元数
static bool dumpIl2CppMetadata(const char* outPath, unsigned long long maxBytes) {
    HANDLE self = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    const uint32_t kMagic = 0xFAB11BAF;

    while (p < limit) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                              !(mbi.Protect & PAGE_NOACCESS) &&
                              (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                               mbi.Protect == PAGE_WRITECOPY || mbi.Protect == PAGE_EXECUTE_READ ||
                               mbi.Protect == PAGE_EXECUTE_READWRITE);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (readable && size > 0x1000) {
            for (size_t off = 0; off + 64 < size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(self, base + off, buf, chunk, &got) || got < 64) continue;
                for (size_t i = 0; i + 64 <= got; i += 4) {
                    if (*(const uint32_t*)(buf + i) != kMagic) continue;
                    const uint32_t version = *(const uint32_t*)(buf + i + 4);
                    if (version < 16 || version > 40) continue;
                    const uint32_t p0 = *(const uint32_t*)(buf + i + 8);
                    if (p0 < 64 || p0 > 0x20000000u) continue;
                    uint8_t* hit = base + off + i;
                    logLine("metadata: magic found at %p version=%u firstOff=%u", (void*)hit, version, p0);
                    FILE* f = fopen(outPath, "wb");
                    if (!f) return false;
                    unsigned long long written = 0;
                    static uint8_t obuf[1 << 20];
                    while (written < maxBytes) {
                        SIZE_T take = sizeof(obuf);
                        if (maxBytes - written < take) take = (SIZE_T)(maxBytes - written);
                        SIZE_T rd = 0;
                        if (!ReadProcessMemory(self, hit + written, obuf, take, &rd) || rd == 0) break;
                        fwrite(obuf, 1, rd, f);
                        written += rd;
                        if (rd < take) break;
                    }
                    fclose(f);
                    logLine("metadata: wrote %llu MB to %s", written / 1048576ull, outPath);
                    return true;
                }
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    logLine("metadata: magic 0xFAB11BAF not found in memory");
    return false;
}

void Gs71DumpMetadata() {
    CreateThread(
            nullptr, 0,
            [](LPVOID) -> DWORD {
                const char* out = "D:\\Programs\\gs71-runtime\\il2cppdumper\\out\\metadata_dumped.dat";
                CreateDirectoryA("D:\\Programs\\gs71-runtime\\il2cppdumper\\out", nullptr);
                dumpIl2CppMetadata(out, 128ull * 1024 * 1024);
                return 0;
            },
            nullptr, 0, nullptr);
}

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


// ----  menu.cpp 用的包装（匿名命名空间里的符号只在单元内可见 ---
void Gs71CamProbeStart() { camProbeStart(); }
void Gs71CamProbeCompare() { camProbeCompare(); }

void Gs71CamProbeDump() { camProbeDump(); }

void Gs71CamAnchor() { camProbeAnchor(); }
void Gs71CamPointerScan() { camProbePointerScan(); }
void Gs71CamDumpHolders() { camProbeDumpHolders(); }

void Gs71CamBpProbe() { camBpProbe(); }

void Gs71CamFreeToggle(bool on) { camFreeToggle(on); }
bool Gs71CamFreeEnabled() { return camFreeEnabled(); }
int Gs71CamFreeWrites() { return camFreeWriteCount(); }
float Gs71CamFreeGet(int i) { return camFreeGet(i); }
void Gs71CamFreeNudge(int axis, float d) { camFreeNudge(axis, d); }

void Gs71CamFreeHookToggle(bool on) {
    if (on) {
        camFreeHookSetTarget();
        camFreeHookSeed();
    }
    camFreeHookToggle(on);
}
unsigned long long Gs71CamSkipCount() { return g_camSkipCount; }
bool Gs71CamFreeHookEnabled() { return camFreeHookEnabled(); }
float Gs71CamFreeHookGet(int i) { return camFreeHookGet(i); }
void Gs71CamFreeHookNudge(int a, float d) { camFreeHookNudge(a, d); }

unsigned long long Gs71CamHookHits() { return g_camHookHits; }

void Gs71AutoTalkToggle(bool on) { autoTalkToggle(on); }
bool Gs71AutoTalkEnabled() { return autoTalkEnabled(); }
int Gs71AutoTalkCount() { return autoTalkCount(); }





// ---------------------------------------------------------------- 自动类型探针（开机自跑）
//
// 目的：找 IL2CPP 类对象， 改碰撞层实现穿墙"铺路
// 做法
//   1. 在内存里搜类型名字符串（元数据运行时已解密成 C 字符串）
//   2.  指向该字符串 qword"  候选就 Il2CppClass.name 字段（位于类对象 +0x10
//   3. 回退 0x10 得到 Il2CppClass 基址，读 image / namespaze 验证
// 全程只读
static const char* kTypeNames[] = {
        "MonoEntityLayerCollisionHelper",
        "MonoWallCollisionHelper",
        "CameraStateMgr",
        "MonoCameraWorldShiftHelper",
};
static const int kTypeNameCount = 4;

struct TypeHit {
    const char* name;
    uintptr_t strAddr;
    uintptr_t classCand;      // 候 Il2CppClass 基址
    uintptr_t namespacePtr;
    uintptr_t imagePtr;
};
static TypeHit g_typeHits[16];
static int g_typeHitCount = 0;

static bool readCStr(HANDLE h, uintptr_t addr, char* out, size_t cap) {
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (void*)addr, out, cap - 1, &got) || got == 0) return false;
    out[got < cap ? got : cap - 1] = 0;
    for (size_t i = 0; i < got; ++i)
        if (out[i] < 0x20 || out[i] > 0x7e) {
            out[i] = 0;
            break;
        }
    return out[0] != 0;
}

// ---------------------------------------------------------------- 严格 Il2CppClass 校验
//
// 布局来自官方 il2cpp.h
//   +0x00 image        +0x08 gc_desc      +0x10 name        +0x18 namespaze
//   +0x20 byval_arg    +0x28 this_arg
// 判据：name 指向目标串、namespaze 可读且短、image 可读、byval_arg 可读
static bool readablePtr(HANDLE h, uintptr_t p, size_t minBytes) {
    if (p < 0x10000 || p > 0x7fffffffffffULL) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(h, (void*)p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (size_t)((uintptr_t)mbi.BaseAddress + mbi.RegionSize - p) >= minBytes;
}

static bool strictClassCheck(HANDLE h, uintptr_t cand, const char* expectName, char* outNs) {
    if (!readablePtr(h, cand, 0x30)) return false;
    uintptr_t image = 0, namePtr = 0, nsPtr = 0, byval = 0;
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (void*)(cand + 0x00), &image, 8, &got) || got < 8) return false;
    if (!ReadProcessMemory(h, (void*)(cand + 0x10), &namePtr, 8, &got) || got < 8) return false;
    if (!ReadProcessMemory(h, (void*)(cand + 0x18), &nsPtr, 8, &got) || got < 8) return false;
    if (!ReadProcessMemory(h, (void*)(cand + 0x20), &byval, 8, &got) || got < 8) return false;

    if (!readablePtr(h, image, 0x20)) return false;
    if (!readablePtr(h, namePtr, 8)) return false;
    if (!readablePtr(h, byval, 0x10)) return false;

    char name[96] = {0};
    if (!readCStr(h, namePtr, name, sizeof(name))) return false;
    if (strcmp(name, expectName) != 0) return false;

    if (nsPtr) {
        if (!readablePtr(h, nsPtr, 1)) return false;
        if (!readCStr(h, nsPtr, outNs, 64)) outNs[0] = 0;
    } else {
        outNs[0] = 0;
    }

    // 决定性判据：Il2CppImage 的第一个字段是指向程序集名的字符串（xxx.dll
    uintptr_t imgNamePtr = 0;
    if (!ReadProcessMemory(h, (void*)image, &imgNamePtr, 8, &got) || got < 8) return false;
    if (!imgNamePtr || !readablePtr(h, imgNamePtr, 4)) return false;
    char imgName[96] = {0};
    if (!readCStr(h, imgNamePtr, imgName, sizeof(imgName))) return false;
    const size_t il = strlen(imgName);
    if (il < 4) return false;
    if (strcmp(imgName + il - 4, ".dll") != 0 && strcmp(imgName + il - 4, ".DLL") != 0)
        return false;
    // 命名空间不能等于类型名（那是误报特征
if (strcmp(outNs, expectName) == 0) return false;
    return true;
}


DWORD WINAPI autoProbeTypesThread(LPVOID) {
    Sleep(9000);   // 等游戏加载完元数
// 只有存在 probe.on 才扫描，正常启动不做无用
if (GetFileAttributesA("D:\\\\Programs\\\\gs71-runtime\\\\probe.on") == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    HANDLE self = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];

    // 第一遍：找类型名字符
// 每个类型名可能存在多份（元数据里的、堆上的临时拷贝），全收集起
static const int kMaxCopies = 8;
    uintptr_t strAddr[4][kMaxCopies] = {};
    int strCount[4] = {0, 0, 0, 0};
    while (p < limit) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                              !(mbi.Protect & PAGE_NOACCESS) &&
                              (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                               mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (readable && size > 0x1000) {
            for (size_t off = 0; off < size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(self, base + off, buf, chunk, &got) || got < 32) continue;
                for (int k = 0; k < kTypeNameCount; ++k) {
                    if (strCount[k] >= kMaxCopies) continue;
                    const size_t nlen = strlen(kTypeNames[k]);
                    if (nlen + 2 > got) continue;
                    for (size_t i = 0; i + nlen + 2 <= got; ++i) {
                        if (memcmp(buf + i, kTypeNames[k], nlen) != 0) continue;
                        if (buf[i + nlen] != 0) continue;
                        if (i > 0 && buf[i - 1] != 0) continue;
                        strAddr[k][strCount[k]++] = (uintptr_t)(base + off + i);
                        i += nlen;
                        if (strCount[k] >= kMaxCopies) break;
                    }
                }
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }

    for (int k = 0; k < kTypeNameCount; ++k) {
        logLine("typeprobe: %-32s copies=%d", kTypeNames[k], strCount[k]);
        for (int c = 0; c < strCount[k]; ++c)
            logLine("     copy[%d] = %p", c, (void*)strAddr[k][c]);
    }

    // 第二遍：找指向这些字符串的指针（= Il2CppClass.name     p = (uint8_t*)si.lpMinimumApplicationAddress;
    static uint8_t q[1 << 20];
    int found[4] = {0, 0, 0, 0};
    while (p < limit && g_typeHitCount < 16) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                              !(mbi.Protect & PAGE_NOACCESS) &&
                              (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                               mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (readable && size > 0x1000) {
            for (size_t off = 0; off + 8 <= size; off += sizeof(q)) {
                size_t chunk = size - off;
                if (chunk > sizeof(q)) chunk = sizeof(q);
                SIZE_T got = 0;
                if (!ReadProcessMemory(self, base + off, q, chunk, &got) || got < 8) continue;
                for (size_t i = 0; i + 8 <= got; i += 8) {
                    const uintptr_t v = *(const uintptr_t*)(q + i);
                    for (int k = 0; k < kTypeNameCount; ++k) {
                        bool isCopy = false;
                        for (int c = 0; c < strCount[k]; ++c)
                            if (v == strAddr[k][c]) {
                                isCopy = true;
                                break;
                            }
                        if (!isCopy) continue;
                        if (found[k] >= 3) continue;
                        found[k]++;
                        const uintptr_t nameField = (uintptr_t)(base + off + i);
                        const uintptr_t cls = nameField - 0x10;   // Il2CppClass.name 位于 +0x10
                        {
                            char nsCheck[64] = {0};
                            if (!strictClassCheck(self, cls, kTypeNames[k], nsCheck)) {
                                continue;   // 不是真的类对象（栈帧/指针数组），跳过
                            }
                        }
                        char ns[64] = {0};
                        char cls2[64] = {0};
                        uintptr_t nsPtr = 0, imgPtr = 0;
                        SIZE_T r2 = 0;
                        ReadProcessMemory(self, (void*)(cls + 0x18), &nsPtr, 8, &r2);
                        ReadProcessMemory(self, (void*)(cls + 0x00), &imgPtr, 8, &r2);
                        if (nsPtr) readCStr(self, nsPtr, ns, sizeof(ns));
                        logLine("typeprobe: HIT %-32s nameField=%p classCand=%p image=%p ns=%p(\"%s\")",
                                kTypeNames[k], (void*)nameField, (void*)cls, (void*)imgPtr,
                                (void*)nsPtr, ns);
                        // 命中即转储：此刻内存一定有
{
                            static FILE* lf = nullptr;
                            if (!lf)
                                lf = fopen("D:\\Programs\\gs71-runtime\\layout_inline.txt", "w");
                            if (lf) {
                                fprintf(lf, "\n=== %s nameField=0x%llx ===\n", kTypeNames[k],
                                        (unsigned long long)nameField);
                                for (int delta = -0x40; delta <= 0x80; delta += 8) {
                                    const uintptr_t at = nameField + delta;
                                    uintptr_t vv = 0;
                                    SIZE_T g2 = 0;
                                    if (!ReadProcessMemory(self, (void*)at, &vv, 8, &g2) ||
                                        g2 < 8) {
                                        fprintf(lf, "  %+04X: <unreadable>\n", delta);
                                        continue;
                                    }
                                    char t2[64] = {0};
                                    bool s2 = false;
                                    if (vv > 0x10000 && vv < 0x7fffffffffffULL)
                                        s2 = readCStr(self, vv, t2, sizeof(t2));
                                    fprintf(lf, "  %+04X: 0x%016llx %s%s\n", delta,
                                            (unsigned long long)vv, s2 ? "-> \"" : "",
                                            s2 ? t2 : "");
                                }
                                fflush(lf);
                            }
                        }
                        g_typeHits[g_typeHitCount].name = kTypeNames[k];
                        g_typeHits[g_typeHitCount].strAddr = v;
                        g_typeHits[g_typeHitCount].classCand = cls;
                        g_typeHits[g_typeHitCount].namespacePtr = nsPtr;
                        g_typeHits[g_typeHitCount].imagePtr = imgPtr;
                        g_typeHitCount++;
                        if (g_typeHitCount >= 16) break;
                    }
                    if (g_typeHitCount >= 16) break;
                }
                if (g_typeHitCount >= 16) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    logLine("typeprobe: done, %d candidate class object(s)", g_typeHitCount);
    return 0;
}

void autoProbeTypesStart();


//  指向类型名字符串的指 附近 0x100 字节 qword 原样打出来，
// 用来反推 Il2CppClass 的真实布局（name 字段到底在类对象的哪个偏移）
static void dumpAroundName(HANDLE h, uintptr_t nameField, const char* typeName) {
    for (int delta = -0x20; delta <= 0x60; delta += 8) {
        const uintptr_t at = nameField + delta;
        uintptr_t v = 0;
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)at, &v, 8, &got) || got < 8) continue;
        char s2[64] = {0};
        bool strish = false;
        if (v > 0x10000 && v < 0x7fffffffffffULL) {
            strish = readCStr(h, v, s2, sizeof(s2));
        }
        logLine("  layout %-30s nameField%+03X = %p %s%s", typeName, delta, (void*)v,
                strish ? "-> " : "", strish ? s2 : "");
    }
}

DWORD WINAPI layoutProbeThread(LPVOID) {
    Sleep(12000);
    HANDLE h = GetCurrentProcess();
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\layout_inline.txt", "w");
    if (!f) {
        logLine("layout: cannot open output file");
        return 0;
    }
    fprintf(f, "typeHitCount = %d\n", g_typeHitCount);
    for (int k = 0; k < g_typeHitCount && k < 12; ++k) {
        const uintptr_t nameField = g_typeHits[k].classCand + 0x10;
        fprintf(f, "\n=== [%d] %s  nameField=0x%llx ===\n", k, g_typeHits[k].name,
                (unsigned long long)nameField);
        for (int delta = -0x40; delta <= 0x80; delta += 8) {
            const uintptr_t at = nameField + delta;
            uintptr_t v = 0;
            SIZE_T got = 0;
            if (!ReadProcessMemory(h, (void*)at, &v, 8, &got) || got < 8) {
                fprintf(f, "  %+04X: <unreadable>\n", delta);
                continue;
            }
            char txt[64] = {0};
            bool strish = false;
            if (v > 0x10000 && v < 0x7fffffffffffULL) strish = readCStr(h, v, txt, sizeof(txt));
            fprintf(f, "  %+04X: 0x%016llx  %s%s\n", delta, (unsigned long long)v,
                    strish ? "-> \"" : "", strish ? txt : "");
            if (strish) fprintf(f, "\"\n");
        }
    }
    fclose(f);
    logLine("layout: written to D:\\Programs\\gs71-runtime\\layout_inline.txt");
    return 0;
}



// ----------------------------------------------------------------  il2cpp 段里直接扫类对象
//
// 思路：不去找"指向某个字符串的指针"，而是 il2cpp 段里把每 8 字节对齐位置当成
// Il2CppClass.name 字段 0x10）来校验：cand = pos - 0x10
// 然后要求 name 字符串等于目标名、namespaze 可读、image 可读、byval_arg 可读
static void scanIl2CppSection(HANDLE h, uintptr_t base, size_t size) {
    static uint8_t buf[4 << 20];
    unsigned long long checks = 0;
    int found = 0;
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\il2cpp_classes.txt", "w");
    if (!f) return;

    for (size_t off = 0; off + sizeof(buf) <= size; off += sizeof(buf)) {
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)(base + off), buf, sizeof(buf), &got) || got < 64) continue;
        for (size_t i = 0; i + 8 <= got; i += 8) {
            const uintptr_t pos = base + off + i;          // 假设这是 name 字段
            const uintptr_t cand = pos - 0x10;
            // 先做一次便宜的预筛：name 指针必须落在可读区（ VirtualQueryEx 太慢
// 改为直接尝试读字符串，失败就跳过
uintptr_t namePtr = *(const uintptr_t*)(buf + i);
            if (namePtr < 0x10000 || namePtr > 0x7fffffffffffULL) continue;
            char name[96] = {0};
            if (!readCStr(h, namePtr, name, sizeof(name))) continue;
            if (name[0] == 0) continue;
            bool match = false;
            for (int k = 0; k < kTypeNameCount; ++k) {
                if (strcmp(name, kTypeNames[k]) == 0) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;

            checks++;
            char ns[64] = {0};
            if (!strictClassCheck(h, cand, name, ns)) continue;

            found++;
            fprintf(f, "CLASS %p  ns=\"%s\"  name=\"%s\"\n", (void*)cand, ns, name);
            fflush(f);
            logLine("strict: CLASS FOUND %p ns=%s name=%s", (void*)cand, ns, name);
            if (found >= 20) {
                fclose(f);
                logLine("strict: done, %d class(es), %llu name checks", found, checks);
                return;
            }
        }
    }
    fclose(f);
    logLine("strict: done, %d class(es), %llu name checks", found, checks);
}

DWORD WINAPI strictClassThread(LPVOID) {
    Sleep(12000);
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    // il2cpp 段：vaddr 0x6C04000，大 0x131A05B0
    const uintptr_t base = (uintptr_t)mod + 0x6C04000;
    const size_t size = 0x131A05B0;
    logLine("strict: scanning il2cpp section at %p (%zu MB)", (void*)base, size / 1048576);
    scanIl2CppSection(h, base, size);
    return 0;
}


// ---------------------------------------------------------------- 地址配对法找 Il2CppClass
//
// 判据（不猜布局，只做地址配对）：
//   Il2CppClass + 0x10   name 指针，指 "类型 
//   Il2CppClass + 0x18   namespaze 指针，指 "命名空间"
// 两个地址必须分别命中我们收集到的字符串副本，配对成功才算真类
struct TypeTarget {
    const char* name;      // 类型
const char* ns;        // 命名空间
};
static const TypeTarget kTargets[] = {
        {"MonoEntityLayerCollisionHelper", "MoleMole"},
        {"MonoWallCollisionHelper", "MoleMole"},
        {"MonoCameraWorldShiftHelper", "MoleMole"},
        {"CameraStateMgr", "miHoYoCamera"},
};
static const int kTargetCount = 4;

static const int kCopies = 6;
static uintptr_t g_nameCopies[4][kCopies];
static int g_nameCopyCount[4] = {0};
static uintptr_t g_nsCopies[4][kCopies];
static int g_nsCopyCount[4] = {0};

// 收集某个字符串在内存里的所有副本地址
static void collectCopies(HANDLE h, const char* str, uintptr_t out[], int* count, int maxCopies) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    const size_t nlen = strlen(str);
    *count = 0;

    while (p < limit && *count < maxCopies) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                              !(mbi.Protect & PAGE_NOACCESS) &&
                              (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                               mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (readable && size > 0x1000) {
            for (size_t off = 0; off < size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < nlen + 2) continue;
                for (size_t i = 0; i + nlen + 2 <= got; ++i) {
                    if (memcmp(buf + i, str, nlen) != 0) continue;
                    if (buf[i + nlen] != 0) continue;
                    if (i > 0 && buf[i - 1] != 0) continue;
                    out[(*count)++] = (uintptr_t)(base + off + i);
                    i += nlen;
                    if (*count >= maxCopies) break;
                }
                if (*count >= maxCopies) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
}


// 只在指定区间里收集字符串副本（避免被 .rdata 里的同名串占满名额）
static int collectCopiesInRange(HANDLE h, uintptr_t base, size_t size, const char* str,
                                uintptr_t out[], int maxCopies) {
    static uint8_t buf[4 << 20];
    const size_t nlen = strlen(str);
    int count = 0;
    for (size_t off = 0; off + 64 < size && count < maxCopies; off += sizeof(buf)) {
        size_t chunk = size - off;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)(base + off), buf, chunk, &got) || got < nlen + 2) continue;
        for (size_t i = 0; i + nlen + 2 <= got; ++i) {
            if (memcmp(buf + i, str, nlen) != 0) continue;
            if (buf[i + nlen] != 0) continue;
            if (i > 0 && buf[i - 1] != 0) continue;
            out[count++] = (uintptr_t)(base + off + i);
            i += nlen;
            if (count >= maxCopies) break;
        }
    }
    return count;
}

DWORD WINAPI pairProbeThread(LPVOID) {
    Sleep(12000);
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE h = GetCurrentProcess();

    uint8_t* modBase = (uint8_t*)GetModuleHandleA(nullptr);
    const uintptr_t secBase = (uintptr_t)modBase + 0x6C04000;
    const size_t secSize = 0x131A05B0;   // il2cpp 段：元数 + 代码
    logLine("pair: scanning strings inside il2cpp section %p (%zu MB)", (void*)secBase,
            secSize / 1048576);

    for (int i = 0; i < kTargetCount; ++i) {
        g_nameCopyCount[i] = collectCopiesInRange(h, secBase, secSize, kTargets[i].name,
                                                  g_nameCopies[i], kCopies);
        g_nsCopyCount[i] = collectCopiesInRange(h, secBase, secSize, kTargets[i].ns,
                                                g_nsCopies[i], kCopies);
        logLine("pair: %-32s nameCopies=%d nsPtrs=%d", kTargets[i].name, g_nameCopyCount[i],
                g_nsCopyCount[i]);
    }

    // 扫指针：name 指针命中  检 +0x08 是否落在该类型的 ns 副本集合
SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t q[1 << 20];
    int found = 0;
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\pairs.txt", "w");

    while (p < limit && found < 12) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                              !(mbi.Protect & PAGE_NOACCESS) &&
                              (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                               mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (readable && size > 0x1000) {
            for (size_t off = 0; off + 16 <= size; off += sizeof(q)) {
                size_t chunk = size - off;
                if (chunk > sizeof(q)) chunk = sizeof(q);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, q, chunk, &got) || got < 16) continue;
                for (size_t i = 0; i + 16 <= got; i += 8) {
                    const uintptr_t v = *(const uintptr_t*)(q + i);
                    for (int k = 0; k < kTargetCount; ++k) {
                        bool isName = false;
                        for (int c = 0; c < g_nameCopyCount[k]; ++c)
                            if (v == g_nameCopies[k][c]) {
                                isName = true;
                                break;
                            }
                        if (!isName) continue;
                        //  pos+0x08 处的值（
// class+0x18 = namespaze
                                                 const uintptr_t pos = (uintptr_t)(base + off + i);
                        uintptr_t nsVal = 0;
                        SIZE_T g2 = 0;
                        if (i + 16 <= got) {
                            nsVal = *(const uintptr_t*)(q + i + 8);
                        } else {
                            ReadProcessMemory(h, (void*)(pos + 8), &nsVal, 8, &g2);
                        }
                        bool nsOk = false;
                        for (int c = 0; c < g_nsCopyCount[k]; ++c)
                            if (nsVal == g_nsCopies[k][c]) {
                                nsOk = true;
                                break;
                            }
                        if (!nsOk) continue;
                        const uintptr_t cls = pos - 0x10;
                        found++;
                        if (f) {
                            fprintf(f, "CLASS %p  %s::%s\n", (void*)cls, kTargets[k].ns,
                                    kTargets[k].name);
                            // 转储这张表前后各 0x80 字节，看清结
fprintf(f, "  --- context around %p ---\n", (void*)cls);
                            for (int d = -0x40; d <= 0x60; d += 8) {
                                const uintptr_t at = cls + d;
                                uintptr_t vv = 0;
                                SIZE_T g3 = 0;
                                if (!ReadProcessMemory(h, (void*)at, &vv, 8, &g3) || g3 < 8) {
                                    fprintf(f, "    %+04X: <unreadable>\n", d);
                                    continue;
                                }
                                char t2[64] = {0};
                                bool st = false;
                                if (vv > 0x10000 && vv < 0x7fffffffffffULL)
                                    st = readCStr(h, vv, t2, sizeof(t2));
                                fprintf(f, "    %+04X: 0x%016llx %s%s\n", d,
                                        (unsigned long long)vv, st ? "->" : "", st ? t2 : "");
                            }
                        }
                        logLine("pair: CLASS FOUND %p  ns=\"%s\" name=\"%s\"", (void*)cls,
                                kTargets[k].ns, kTargets[k].name);
                        if (found >= 12) break;
                    }
                    if (found >= 12) break;
                }
                if (found >= 12) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    if (f) fclose(f);
    logLine("pair: done, %d class(es)", found);
    return 0;
}


// ---------------------------------------------------------------- 世界内角色对象探针（等待型）
//
// 设计：开机后一直等，直 bridge 里出现玩家坐标（说明人已经进世界），
// 然后按坐标去内存里找"角色对象"，并把候选附近的内存全部转储
// 这样用户一登录就自动采到数据，不需要人在场操作 
// 把指针解析成"类名"：如果它指向的是一段可 ASCII，就打印出来
// 用来在角色对象周围认出组件（比如 xxxCollisionHelper）
static bool resolveName(HANDLE h, uintptr_t p, char* out, size_t cap) {
    out[0] = 0;
    if (p < 0x10000 || p > 0x7fffffffffffULL) return false;
    char buf[96] = {0};
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (void*)p, buf, sizeof(buf) - 1, &got) || got < 4) return false;
    buf[got] = 0;
    // 必须是可打印 ASCII 且以 0 结尾或长度合
int n = 0;
    for (size_t i = 0; i < got; ++i) {
        const unsigned char c = (unsigned char)buf[i];
        if (c == 0) break;
        if (c < 0x20 || c > 0x7e) return false;
        ++n;
    }
    if (n < 4) return false;
    strncpy(out, buf, cap - 1);
    out[cap - 1] = 0;
    return true;
}


// ---------------------------------------------------------------- 用写断点定位实体对象
//
// 思路：坐 血量字段所在的地址已经找到了（扫描得到），在它上面 写断 
// 谁写它就说明谁在更新这个实体。异常发生时 RIP + 通用寄存器全部记下来
// 寄存器里通常就有对象基址（例 mov [rcx+0x18], xmm0 里的 rcx）
static uintptr_t g_bpAddr1 = 0;   // 坐标字段
static uintptr_t g_bpAddr2 = 0;   // 血量字段（坐标 + 0x18
static std::atomic<int> g_bpLogCount{0};

static int bpApply(uintptr_t a1, uintptr_t a2) {
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int done = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            if (SuspendThread(th) == (DWORD)-1) {
                CloseHandle(th);
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &ctx)) {
                if (a1) {
                    // DR0：写断点  字节（LEN=11                     ctx.Dr0 = a1;
                    ctx.Dr7 = (ctx.Dr7 & ~0xF0003ULL) | 0x0001 | (0x1ULL << 16) | (0x3ULL << 18);
                    if (a2) {
                        // DR1：第二个写断
ctx.Dr1 = a2;
                        ctx.Dr7 = (ctx.Dr7 & ~0xF00003ULL) | (0x1ULL << 2) | (0x1ULL << 20) |
                                  (0x3ULL << 22);
                    }
                } else {
                    ctx.Dr0 = 0;
                    ctx.Dr1 = 0;
                    ctx.Dr7 = ctx.Dr7 & ~0xF0000FULL;
                }
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) done++;
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return done;
}

void bpLogHitRegs(CONTEXT* c) {
    const int n = g_bpLogCount.fetch_add(1);
    if (n >= 8) return;
    HMODULE m = nullptr;
    char nm[64] = "?";
    unsigned long long rva = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)c->Rip, &m) &&
        m) {
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* b = strrchr(path, '\\');
        snprintf(nm, sizeof(nm), "%s", b ? b + 1 : path);
        rva = (unsigned long long)(c->Rip - (uintptr_t)m);
    }
    logLine("bphit #%d  %s+0x%llX  rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p r8=%p r9=%p rbp=%p",
            n + 1, nm, rva, (void*)c->Rax, (void*)c->Rbx, (void*)c->Rcx, (void*)c->Rdx,
            (void*)c->Rsi, (void*)c->Rdi, (void*)c->R8, (void*)c->R9, (void*)c->Rbp);
}

// 看有没有 bp.now 文件，有就装断点 8 
DWORD WINAPI bpProbeFromFlag(LPVOID) {
    for (;;) {
        Sleep(2000);
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\bp.now") == INVALID_FILE_ATTRIBUTES)
            continue;
        DeleteFileA("D:\\Programs\\gs71-runtime\\bp.now");
        if (!g_bpAddr1) {
            logLine("bpflag: no candidate address yet, run a scan first");
            continue;
        }
        g_bpLogCount.store(0);
        g_bpArmed.store(true);
        const int n = bpApply(g_bpAddr1, g_bpAddr2);
        logLine("bpflag: armed write-BP on %p / %p for 8s (%d threads)", (void*)g_bpAddr1,
                (void*)g_bpAddr2, n);
        for (int i = 0; i < 80 && g_bpLogCount.load() < 8; ++i) Sleep(100);
        const int cleared = bpApply(0, 0);
        g_bpArmed.store(false);
        logLine("bpflag: cleared (%d threads), %d hit(s)", cleared, g_bpLogCount.load());
    }
    return 0;
}

DWORD WINAPI worldAvatarProbeThread(LPVOID) {
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES) return 0;

    // 等玩家进世界（最 12 小时
float px = 0, py = 0, pz = 0, hp = 0;
    for (int i = 0; i < 43200; ++i) {
        Snapshot snap = bridge::get();
        if (snap.px != 0.f || snap.pz != 0.f) {
            px = snap.px;
            py = snap.py;
            pz = snap.pz;
            hp = snap.hp;
            break;
        }
        Sleep(1000);
    }
    if (px == 0.f && pz == 0.f) {
        logLine("worldprobe: player never entered world, abort");
        return 0;
    }
    logLine("worldprobe: player at %.2f %.2f %.2f hp=%.1f, scanning HEAP only", px, py, pz, hp);

    HANDLE h = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\avatar_objects.txt", "w");
    int found = 0;
    int scannedMB = 0;

    while (p < limit && found < 20) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        // 只扫"私有可写"堆：排除代码段和只读数据
        const bool heapish = (mbi.State == MEM_COMMIT) && (mbi.Type == MEM_PRIVATE) &&
                             (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) &&
                             !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (heapish && size > 0x1000 && size < 0x40000000) {
            for (size_t off = 0; off + 16 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 16) continue;
                scannedMB += (int)(got / 1048576);
                const float* fl = (const float*)buf;
                const size_t nf = got / 4;
                for (size_t k = 0; k + 2 < nf; ++k) {
                    // 注意：NaN 与任何数比较都是 false，所以必须写 不满 <= 就跳
// 否则 NaN 会被当成命中（上一版就是这么被坑的
if (!(fabsf(fl[k] - px) <= 0.5f)) continue;
                    if (!(fabsf(fl[k + 1] - py) <= 0.5f)) continue;
                    if (!(fabsf(fl[k + 2] - pz) <= 0.5f)) continue;
                    if (!(fabsf(fl[k]) < 100000.f) || !(fabsf(fl[k + 1]) < 100000.f) ||
                        !(fabsf(fl[k + 2]) < 100000.f))
                        continue;
                    const uintptr_t hit = (uintptr_t)(base + off + k * 4);
                    if (found == 0) {
                        g_bpAddr1 = hit;          // 坐标字段
                        g_bpAddr2 = hit + 0x18;   // 血量字
logLine("worldprobe: bp targets = %p / %p", (void*)g_bpAddr1,
                                (void*)g_bpAddr2);
                    }
                    found++;
                    if (f) {
                        fprintf(f,
                                "\n=== avatar candidate %d at 0x%llx  (%.2f %.2f %.2f) ===\n",
                                found, (unsigned long long)hit, fl[k], fl[k + 1], fl[k + 2]);
                        // 浮点视图： x40（看有没有血量等相邻字段
fprintf(f, "  --- floats around ---\n");
                        for (int d = -0x40; d <= 0x40; d += 16) {
                            float fv[4] = {0, 0, 0, 0};
                            SIZE_T g2 = 0;
                            if (!ReadProcessMemory(h, (void*)(hit + d), fv, 16, &g2) || g2 < 16)
                                continue;
                            fprintf(f, "  f%+04X: %.3f %.3f %.3f %.3f\n", d, fv[0], fv[1], fv[2],
                                    fv[3]);
                        }
                        // 指针视图： x400（找组件引用
fprintf(f, "  --- pointers around ---\n");
                        for (int d = -0x100; d <= 0x400; d += 8) {
                            uintptr_t vv = 0;
                            SIZE_T g3 = 0;
                            if (!ReadProcessMemory(h, (void*)(hit + d), &vv, 8, &g3) || g3 < 8)
                                continue;
                            if (vv < 0x10000 || vv > 0x7fffffffffffULL) continue;
                            char nm[96] = {0};
                            const bool hasName = resolveName(h, vv, nm, sizeof(nm));
                            fprintf(f, "  q%+05X: 0x%016llx %s%s\n", d, (unsigned long long)vv,
                                    hasName ? "-> " : "", hasName ? nm : "");
                        }
                        fflush(f);
                    }
                    logLine("worldprobe: candidate %d at %p", found, (void*)hit);
                    k += 3;
                    if (found >= 20) break;
                }
                if (found >= 20) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    if (f) fclose(f);
    logLine("worldprobe: done, %d candidate(s), scanned %d MB heap", found, scannedMB);
    return 0;
}

// 运行时按需触发：外部放一 scan.now 文件，就地跑一次角色对象扫描并删掉
static std::atomic<bool> g_scanRequested{false};

DWORD WINAPI scanNowWatcher(LPVOID) {
    for (;;) {
        Sleep(2000);
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\diff1.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\diff1.now");
            logLine("diff: phase 1 requested");
            CreateThread(nullptr, 0, diffScanThread, (LPVOID)(intptr_t)1, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\diff2.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\diff2.now");
            logLine("diff: phase 2 requested");
            CreateThread(nullptr, 0, diffScanThread, (LPVOID)(intptr_t)2, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\ctor_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\ctor_on.now");
            installCtorCapture(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\rbdf_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\rbdf_off.now");
            CreateThread(nullptr, 0, rbdFromFileThread, (LPVOID)(intptr_t)0, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\rbdf_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\rbdf_on.now");
            CreateThread(nullptr, 0, rbdFromFileThread, (LPVOID)(intptr_t)1, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\vel0_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\vel0_on.now");
            if (!g_vel0On) {
                g_vel0On = true;
                logLine("vel0: ON");
                CreateThread(nullptr, 0, zeroVelocityThread, nullptr, 0, nullptr);
            }
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\vel0_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\vel0_off.now");
            g_vel0On = false;
            logLine("vel0: OFF");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\getpos_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\getpos_on.now");
            installGetPositionHook(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\getpos_report.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\getpos_report.now");
            reportGetPosition();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\noclip_atomic.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\noclip_atomic.now");
            CreateThread(nullptr, 0, noclipAtomicThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\api_get.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\api_get.now");
            CreateThread(nullptr, 0, apiProbeThread, (LPVOID)(intptr_t)0, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fog_now.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fog_now.now");
            g_fogOp = (g_fogOp == 1) ? 2 : 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\time2x.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\time2x.now");
            g_timeScaleOp = 2.0f;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\time1x.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\time1x.now");
            g_timeScaleOp = 1.0f;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fov90.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fov90.now");
            g_fovOp = 90.f;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fov45.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fov45.now");
            g_fovOp = 45.f;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\legacyfly_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\legacyfly_on.now");
            g_flyLoopOn = false; logLine("legacy fly loop disabled (AvatarMoveTo per frame crashes)");
            logLine("fly: loop ON (isWalk=%d)", g_flyIsWalk);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\legacyfly_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\legacyfly_off.now");
            g_flyLoopOn = false;
            logLine("fly: loop OFF");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\legacyfly_walk.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\legacyfly_walk.now");
            g_flyIsWalk = 1 - g_flyIsWalk;
            logLine("fly: isWalk=%d", g_flyIsWalk);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\api_move0.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\api_move0.now");
            const Snapshot s3 = bridge::get();
            g_mainMovePos[0] = s3.px + 5.f;
            g_mainMovePos[1] = s3.py;
            g_mainMovePos[2] = s3.pz;
            g_mainMoveLen = 5.f;
            g_mainMoveVariant = 1;
            g_mainMovePending = true;
            logLine("api: queued MoveTo variant1 (changeState=0)");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\api_goto.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\api_goto.now");
            const Snapshot s4 = bridge::get();
            g_mainMovePos[0] = s4.px + 5.f;
            g_mainMovePos[1] = s4.py;
            g_mainMovePos[2] = s4.pz;
            g_mainMoveVariant = 2;
            g_mainMovePending = true;
            logLine("api: queued AvatarGotoPoint");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\api_move.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\api_move.now");
            g_mainMoveVariant = 0;
            {
                const Snapshot s2 = bridge::get();
                g_mainMovePos[0] = s2.px + 5.f;
                g_mainMovePos[1] = s2.py;
                g_mainMovePos[2] = s2.pz;
                g_mainMoveLen = 5.f;
                g_mainMovePending = true;
                logLine("api: queued AvatarMoveTo on main thread");
            }
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\findent.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\findent.now");
            CreateThread(nullptr, 0, findEntityByPosThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\mmstack_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\mmstack_on.now");
            installStackProbe(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\mmstack_report.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\mmstack_report.now");
            reportStackProbe();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\mmoron_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\mmoron_on.now");
            installMmoronProbe(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\mmoron_report.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\mmoron_report.now");
            reportMmoron();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\key_w.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\key_w.now");
            CreateThread(nullptr, 0, holdKeyThread, (LPVOID)(intptr_t)1, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\key_space.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\key_space.now");
            CreateThread(nullptr, 0, holdKeyThread, (LPVOID)(intptr_t)2, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\key_shift.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\key_shift.now");
            CreateThread(nullptr, 0, holdKeyThread, (LPVOID)(intptr_t)3, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\tfind.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\tfind.now");
            CreateThread(nullptr, 0, findTransformOffsetThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsmscan.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsmscan.now");
            CreateThread(nullptr, 0, fsmMaskScanThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsmfields.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsmfields.now");
            CreateThread(nullptr, 0, fsmFieldsThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsmmask.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsmmask.now");
            CreateThread(nullptr, 0, fsmMaskThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsmc_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsmc_on.now");
            installFsmCtorCapture(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsmc_report.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsmc_report.now");
            reportFsmTransforms();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsm_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsm_on.now");
            installFsmHooks(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsm_report.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsm_report.now");
            reportFsm();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsm_skip_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsm_skip_on.now");
            g_fsmSkip = true;
            logLine("fsm: SKIP ON (noclip)");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsm_skip_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsm_skip_off.now");
            g_fsmSkip = false;
            logLine("fsm: SKIP OFF");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\rbd_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\rbd_off.now");
            CreateThread(nullptr, 0, toggleRigidbodyCollisions, (LPVOID)(intptr_t)0, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\rbd_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\rbd_on.now");
            CreateThread(nullptr, 0, toggleRigidbodyCollisions, (LPVOID)(intptr_t)1, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\ft.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\ft.now");
            logLine("ft: scan requested");
            CreateThread(nullptr, 0, findPlayerTransform, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\xft_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\xft_on.now");
            installPlayerTransformHook(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\xft_dump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\xft_dump.now");
            reportPlayerTransform();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\xf_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\xf_on.now");
            installTransformProbe(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\xf_dump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\xf_dump.now");
            dumpTransformProbe();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\xf_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\xf_off.now");
            installTransformProbe(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\vchm_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\vchm_on.now");
            g_vchmInstCount = 0;
            installVchmCtorCapture();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\classwalk.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\classwalk.now");
            CreateThread(nullptr, 0, classWalkThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\vchm_dump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\vchm_dump.now");
            findPlayerVchm();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\ctor_dump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\ctor_dump.now");
            dumpCtorCaptures();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\ctor_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\ctor_off.now");
            installCtorCapture(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\solo3_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\solo3_on.now");
            patchSolo3Check(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\solo3_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\solo3_off.now");
            patchSolo3Check(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\w1345_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\w1345_on.now");
            patchWrap1345(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\w1345_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\w1345_off.now");
            patchWrap1345(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\w823_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\w823_on.now");
            patchWrap823(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\w823_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\w823_off.now");
            patchWrap823(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\solo2_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\solo2_on.now");
            patchSolo2Check(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\solo2_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\solo2_off.now");
            patchSolo2Check(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\solo_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\solo_on.now");
            patchSoloMoveCheck(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\solo_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\solo_off.now");
            patchSoloMoveCheck(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\count_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\count_on.now");
            installCounterStubsV2(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\count_dump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\count_dump.now");
            dumpCountersV2();
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\count_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\count_off.now");
            installCounterStubsV2(false);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\pmove.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\pmove.now");
            logLine("pmove: scan requested");
            CreateThread(nullptr, 0, findPlayerMoveInst, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\findplayer.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\findplayer.now");
            logLine("player: find requested");
            CreateThread(nullptr, 0, findPlayerActor, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\planA.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\planA.now");
            logLine("planA: scan requested");
            CreateThread(nullptr, 0, planAThread, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\multi.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\multi.now");
            logLine("multi: scan requested");
            CreateThread(nullptr, 0, scanMultiClasses, nullptr, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\noclip_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\noclip_on.now");
            logLine("coll: NOCLIP ON requested (all layers)");
            CreateThread(nullptr, 0, collDisableAllThread, (LPVOID)(intptr_t)1, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\noclip_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\noclip_off.now");
            logLine("coll: NOCLIP OFF requested (restore all layers)");
            CreateThread(nullptr, 0, collDisableAllThread, (LPVOID)(intptr_t)0, 0, nullptr);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\scan.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\scan.now");
            logLine("scannow: requested, spawning world probe");
            CreateThread(nullptr, 0, worldAvatarProbeThread, nullptr, 0, nullptr);
        }

        // ---- 第二批开 ----
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\tx_probe.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\tx_probe.now");
            installTransmitProbe(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\lua_probe.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\lua_probe.now");
            installLuaProbe(true);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\cls_find.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\cls_find.now");
            g_clsFindReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\cls_grep.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\cls_grep.now");
            g_clsGrepReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\eldiff.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\eldiff.now");
            g_elDiffReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\el_dump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\el_dump.now");
            g_elDumpReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\el_en.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\el_en.now");
            g_elEnReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\el_btn.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\el_btn.now");
            g_elBtnReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\el_btn_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\el_btn_on.now");
            g_elBtnVal = 1;
            g_elBtnReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\el_btn_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\el_btn_off.now");
            g_elBtnVal = 0;
            g_elBtnReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\el_refresh.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\el_refresh.now");
            g_elRefReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\ent_probe.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\ent_probe.now");
            g_entProbeReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\posf.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\posf.now");
            g_posfReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\xf_probe.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\xf_probe.now");
            g_xfReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\locf.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\locf.now");
            g_locfReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\mtab.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\mtab.now");
            g_mtabReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\mdump.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\mdump.now");
            g_mdumpReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fsmd.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fsmd.now");
            g_fsmdReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\hc_a.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\hc_a.now");
            g_hcAReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\hc_b.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\hc_b.now");
            g_hcBReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\entfind.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\entfind.now");
            g_entfReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\entcls.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\entcls.now");
            g_entclsReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\cmd.now") != INVALID_FILE_ATTRIBUTES) {
            applyCmdFile();
            DeleteFileA("D:\\Programs\\gs71-runtime\\cmd.now");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fly_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fly_on.now");
            g_flyMode = 1;
            logLine("fly: ON (mode 1, native state flags=0x%X)", g_flyFlags);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fly2_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fly2_on.now");
            g_flyMode = 2;
            logLine("fly: ON (mode 2, velocity override %.1f m/s)", g_flySpeed);
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\fly_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\fly_off.now");
            g_flyMode = 0;
            logLine("fly: OFF");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\hideui_probe.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\hideui_probe.now");
            g_uiProbeReq = 1;
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\hideui_on.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\hideui_on.now");
            g_hideUiOp = 1;
            logLine("ui: hide requested");
        }
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\hideui_off.now") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA("D:\\Programs\\gs71-runtime\\hideui_off.now");
            g_hideUiOp = 2;
            logLine("ui: show requested");
        }
    }
    return 0;
}


// ---------------------------------------------------------------- 差分扫描：找"真正在更 的坐标字
//
// 步骤
//   第一遍（diff1.now）：记录当前坐标，扫堆，把所有出现该三元组的地址存起
//   用户走两
//   第二遍（diff2.now）：记录新坐标，扫堆找新三元组，若地址在第一遍的集合  就是活字
// 优点：不依赖断点触发时机，也不需要用户精确配合
static std::vector<uintptr_t> g_diffOld;
static float g_diffPos[3] = {0, 0, 0};
static std::atomic<int> g_diffPhase{0};
static std::mutex g_diffMutex;



// NtQueryInformationThread 的最小声明（避免依赖 winternl.h 的版本差异）
typedef struct _MY_THREAD_BASIC_INFORMATION {
    LONG ExitStatus;
    PVOID TebBaseAddress;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR UniqueThreadId;
    ULONG_PTR AffinityMask;
    LONG Priority;
    LONG BasePriority;
} MY_THREAD_BASIC_INFORMATION;
typedef MY_THREAD_BASIC_INFORMATION THREAD_BASIC_INFORMATION;
typedef LONG THREADINFOCLASS;
typedef LONG(NTAPI* NtQitFn)(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
static NtQitFn getNtQit() {
    static NtQitFn fn = (NtQitFn)GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                                "NtQueryInformationThread");
    return fn;
}

// ---------------------------------------------------------------- 线程栈范围（用于过滤差分结果里的栈副本）
struct StackRange {
    uintptr_t lo, hi;
};
static StackRange g_stackRanges[512];
static int g_stackRangeCount = 0;
static volatile long g_stackRangesBuilt = 0;

// TEB 里的栈范围（x64）：StackBase = TEB+0x08，StackLimit = TEB+0x10
static void buildThreadStacks() {
    g_stackRangeCount = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    HANDLE h = GetCurrentProcess();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) continue;
            THREAD_BASIC_INFORMATION tbi{};
            ULONG ret = 0;
            NtQitFn qit = getNtQit();
            if (qit && qit(t, (THREADINFOCLASS)0, &tbi, sizeof(tbi), &ret) == 0 &&
                tbi.TebBaseAddress) {
                uint64_t base = 0, limit = 0;
                SIZE_T got = 0;
                if (ReadProcessMemory(h, (uint8_t*)tbi.TebBaseAddress + 0x08, &base, 8, &got) &&
                    got == 8 &&
                    ReadProcessMemory(h, (uint8_t*)tbi.TebBaseAddress + 0x10, &limit, 8, &got) &&
                    got == 8 && base > limit && g_stackRangeCount < 512) {
                    g_stackRanges[g_stackRangeCount].lo = (uintptr_t)limit;
                    g_stackRanges[g_stackRangeCount].hi = (uintptr_t)base;
                    g_stackRangeCount++;
                }
            }
            CloseHandle(t);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    logLine("stacks: %d thread stack range(s)", g_stackRangeCount);
}

static bool inThreadStack(uintptr_t a) {
    for (int i = 0; i < g_stackRangeCount; ++i) {
        if (a >= g_stackRanges[i].lo && a < g_stackRanges[i].hi) return true;
    }
    return false;
}

static void diffScan(std::vector<uintptr_t>& out, const float pos[3]) {
    out.clear();
    buildThreadStacks();
    HANDLE h = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    while (p < limit) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool heapish = (mbi.State == MEM_COMMIT) && (mbi.Type == MEM_PRIVATE) &&
                             (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) &&
                             !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (heapish && size > 0x1000 && size < 0x40000000) {
            for (size_t off = 0; off + 16 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 16) continue;
                const float* fl = (const float*)buf;
                const size_t nf = got / 4;
                for (size_t k = 0; k + 2 < nf; ++k) {
                    if (!(fabsf(fl[k] - pos[0]) <= 0.05f)) continue;
                    if (!(fabsf(fl[k + 1] - pos[1]) <= 0.05f)) continue;
                    if (!(fabsf(fl[k + 2] - pos[2]) <= 0.05f)) continue;
                    const uintptr_t hitAddr = (uintptr_t)(base + off + k * 4);
                    if (!inThreadStack(hitAddr)) out.push_back(hitAddr);
                    k += 3;
                    if (out.size() > 20000) return;
                }
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
}

DWORD WINAPI diffScanThread(LPVOID param) {
    const int phase = (int)(intptr_t)param;
    Snapshot snap = bridge::get();
    if (snap.px == 0.f && snap.pz == 0.f) {
        logLine("diff: player not in world");
        return 0;
    }
    const float pos[3] = {snap.px, snap.py, snap.pz};
    logLine("diff%d: scanning for %.2f %.2f %.2f", phase, pos[0], pos[1], pos[2]);
    std::vector<uintptr_t> hits;
    diffScan(hits, pos);
    logLine("diff%d: %zu address(es)", phase, hits.size());

    std::lock_guard<std::mutex> lock(g_diffMutex);
    if (phase == 1) {
        g_diffOld = hits;
        g_diffPos[0] = pos[0];
        g_diffPos[1] = pos[1];
        g_diffPos[2] = pos[2];
        g_diffPhase.store(1);
        logLine("diff1: baseline saved");
    } else {
        if (g_diffOld.empty()) {
            logLine("diff2: no baseline, run diff1 first");
            return 0;
        }
        FILE* f = fopen("D:\\Programs\\gs71-runtime\\diff_result.txt", "w");
        int live = 0;
        for (uintptr_t a : hits) {
            for (uintptr_t b : g_diffOld) {
                if (a == b) {
                    live++;
                    if (f)
                        fprintf(f, "LIVE pos field at 0x%llx  (old %.2f %.2f %.2f -> new %.2f %.2f %.2f)\n",
                                (unsigned long long)a, g_diffPos[0], g_diffPos[1], g_diffPos[2],
                                pos[0], pos[1], pos[2]);
                    logLine("diff2: LIVE FIELD %p", (void*)a);
                    if (live >= 40) break;
                }
            }
            if (live >= 40) break;
        }
        if (f) fclose(f);
        logLine("diff2: %d live field(s) -> diff_result.txt", live);
    }
    return 0;
}


// ---------------------------------------------------------------- 按文件里的地址列表转储上下
// addrs.txt 每行一个十六进制地址；dump.now 触发；每个地址转储 ±0x800，指针解析成类名
DWORD WINAPI dumpAddrsFromFile(LPVOID) {
    for (;;) {
        Sleep(2000);
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\dump.now") == INVALID_FILE_ATTRIBUTES)
            continue;
        DeleteFileA("D:\\Programs\\gs71-runtime\\dump.now");

        FILE* in = fopen("D:\\Programs\\gs71-runtime\\addrs.txt", "r");
        if (!in) {
            logLine("dumpaddrs: addrs.txt not found");
            continue;
        }
        FILE* out = fopen("D:\\Programs\\gs71-runtime\\addr_dump.txt", "w");
        if (!out) {
            fclose(in);
            continue;
        }
        HANDLE h = GetCurrentProcess();
        char line[64];
        int n = 0;
        while (fgets(line, sizeof(line), in)) {
            uintptr_t addr = (uintptr_t)strtoull(line, nullptr, 16);
            if (!addr) continue;
            ++n;
            fprintf(out, "\n=== address 0x%llx  (+-%d) ===\n", (unsigned long long)addr, 0x800);
            // 浮点视图
            fprintf(out, "  --- floats ---\n");
            for (int d = -0x40; d <= 0x40; d += 16) {
                float fv[4] = {0, 0, 0, 0};
                SIZE_T g = 0;
                if (!ReadProcessMemory(h, (void*)(addr + d), fv, 16, &g) || g < 16) continue;
                fprintf(out, "  f%+05X: %.3f %.3f %.3f %.3f\n", d, fv[0], fv[1], fv[2], fv[3]);
            }
            // 指针视图（含类名解析
fprintf(out, "  --- pointers ---\n");
            for (int d = -0x800; d <= 0x800; d += 8) {
                uintptr_t v = 0;
                SIZE_T g = 0;
                if (!ReadProcessMemory(h, (void*)(addr + d), &v, 8, &g) || g < 8) continue;
                if (v < 0x10000 || v > 0x7fffffffffffULL) continue;
                char nm[96] = {0};
                const bool hasName = resolveName(h, v, nm, sizeof(nm));
                fprintf(out, "  q%+05X: 0x%016llx %s%s\n", d, (unsigned long long)v,
                        hasName ? "-> " : "", hasName ? nm : "");
            }
            fflush(out);
        }
        fclose(in);
        fclose(out);
        logLine("dumpaddrs: %d address(es) -> addr_dump.txt", n);
    }
    return 0;
}


// ---------------------------------------------------------------- 客户端坐标写入（noclip 基础
//
//  poswrite.txt：第一 "x y z"，第二行 "毫秒 ，其余行是地址（十六进制）
// 在指定时间内持续把这些地址 (x,y,z) 写成目标坐标
// 如果客户端接受，角色会移动到目标位置，而且不会触发场景加载
DWORD WINAPI posWriteThread(LPVOID) {
    for (;;) {
        Sleep(500);
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\poswrite.now") == INVALID_FILE_ATTRIBUTES)
            continue;
        DeleteFileA("D:\\Programs\\gs71-runtime\\poswrite.now");

        FILE* in = fopen("D:\\Programs\\gs71-runtime\\poswrite.txt", "r");
        if (!in) {
            logLine("poswrite: poswrite.txt not found");
            continue;
        }
        float tx = 0, ty = 0, tz = 0;
        int ms = 2000;
        if (fscanf(in, "%f %f %f", &tx, &ty, &tz) != 3) {
            logLine("poswrite: bad header");
            fclose(in);
            continue;
        }
        if (fscanf(in, "%d", &ms) != 1) ms = 2000;
        if (ms < 100) ms = 100;
        if (ms > 15000) ms = 15000;

        std::vector<uintptr_t> addrs;
        char line[64];
        while (fgets(line, sizeof(line), in)) {
            uintptr_t a = (uintptr_t)strtoull(line, nullptr, 16);
            if (a) addrs.push_back(a);
        }
        fclose(in);

        logLine("poswrite: target %.2f %.2f %.2f for %d ms across %zu address(es)", tx, ty, tz, ms,
                addrs.size());
        HANDLE h = GetCurrentProcess();
        const DWORD start = GetTickCount();
        int writes = 0;
        while ((int)(GetTickCount() - start) < ms) {
            float v[3] = {tx, ty, tz};
            for (uintptr_t a : addrs) {
                SIZE_T w = 0;
                if (WriteProcessMemory(h, (void*)a, v, 12, &w)) writes++;
            }
            Sleep(8);
        }
        logLine("poswrite: done, %d write(s)", writes);
    }
    return 0;
}


// ---------------------------------------------------------------- 交叉引用扫描：找"谁在读名字表"
//
// 1)  .rdata 里定 (namePtr, nsPtr) 表项，算出它的模块内偏移
// 2) 在代码段里搜 RIP 相对寻址（lea/mov reg,[rip+disp32]）指向该表项的指
//     那些指令所在的函数就是 IL2CPP 的类查找函数
// 只读，不改任何东西
DWORD WINAPI xrefProbeThread(LPVOID) {
    Sleep(10000);
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES)
        return 0;

    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    logLine("xref: module base %p", (void*)mod);

    // 1) 找目标名字字符串和它的指针表
const char* wantName = "MonoEntityLayerCollisionHelper";
    const char* wantNs = "MoleMole";
    uintptr_t nameStr = 0, nsStr = 0;
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
        uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
        MEMORY_BASIC_INFORMATION mbi{};
        static uint8_t buf[1 << 20];
        while (p < limit && (!nameStr || !nsStr)) {
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                                  !(mbi.Protect & PAGE_NOACCESS) &&
                                  (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                                   mbi.Protect == PAGE_EXECUTE_READ ||
                                   mbi.Protect == PAGE_EXECUTE_READWRITE);
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            size_t size = mbi.RegionSize;
            if (readable && size > 0x1000) {
                for (size_t off = 0; off < size; off += sizeof(buf)) {
                    size_t chunk = size - off;
                    if (chunk > sizeof(buf)) chunk = sizeof(buf);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 40) continue;
                    for (size_t i = 0; i + 40 <= got; ++i) {
                        // 只接受「映射区/镜像区」里的字符串（元数据 mmap 进来的，
                        // 堆上的那些副本没有指针引用它们）
                        const uintptr_t here = (uintptr_t)(base + off + i);
                        const bool inModule = (mbi.Type == MEM_MAPPED) || (mbi.Type == MEM_IMAGE);
                        if (inModule && !nameStr &&
                            memcmp(buf + i, wantName, strlen(wantName)) == 0 &&
                            buf[i + strlen(wantName)] == 0)
                            nameStr = here;
                        if (inModule && !nsStr &&
                            memcmp(buf + i, wantNs, strlen(wantNs)) == 0 &&
                            buf[i + strlen(wantNs)] == 0)
                            nsStr = here;
                        if (nameStr && nsStr) break;
                    }
                }
            }
            uint8_t* next = base + size;
            if (next <= p) break;
            p = next;
        }
    }
    if (!nameStr) {
        logLine("xref: name string not found");
        return 0;
    }
    logLine("xref: nameStr=%p rva=0x%llX   nsStr=%p rva=0x%llX", (void*)nameStr,
            (unsigned long long)(nameStr - (uintptr_t)mod), (void*)nsStr,
            nsStr ? (unsigned long long)(nsStr - (uintptr_t)mod) : 0ull);

    // 2) 找表项：连续两个 qword == (nameStr, nsStr)
    uintptr_t entry = 0;
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
        uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
        MEMORY_BASIC_INFORMATION mbi{};
        static uint8_t q[1 << 20];
        while (p < limit && !entry) {
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            const bool readable = (mbi.State == MEM_COMMIT) && !(mbi.Protect & PAGE_GUARD) &&
                                  !(mbi.Protect & PAGE_NOACCESS) &&
                                  (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                                   mbi.Protect == PAGE_EXECUTE_READ ||
                                   mbi.Protect == PAGE_EXECUTE_READWRITE);
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            size_t size = mbi.RegionSize;
            if (readable && size > 0x1000) {
                for (size_t off = 0; off + 16 <= size; off += sizeof(q)) {
                    size_t chunk = size - off;
                    if (chunk > sizeof(q)) chunk = sizeof(q);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(h, base + off, q, chunk, &got) || got < 16) continue;
                    for (size_t i = 0; i + 16 <= got; i += 8) {
                        const uintptr_t a = *(const uintptr_t*)(q + i);
                        const uintptr_t b = *(const uintptr_t*)(q + i + 8);
                        // 任意位置（排除私有堆）引用到名字串都算候
const uintptr_t here = (uintptr_t)(base + off + i);
                        if (a == nameStr && mbi.Type != MEM_PRIVATE) {
                            entry = here;
                            logLine("xref: candidate ref at %p (next=%p)", (void*)here, (void*)b);
                            break;
                        }
                    }
                }
            }
            uint8_t* next = base + size;
            if (next <= p) break;
            p = next;
        }
    }
    if (!entry) {
        logLine("xref: (name,ns) pair entry not found");
        return 0;
    }
    const uintptr_t entryRva = entry - (uintptr_t)mod;
    logLine("xref: table entry at %p  rva=0x%llX", (void*)entry, (unsigned long long)entryRva);

    // 3) 在代码段里搜 RIP 相对引用 8/4C 8D/8B xx disp32
    const uintptr_t textBase = (uintptr_t)mod + 0x1000;
    const size_t textSize = 0x021A8A96;
    static uint8_t code[4 << 20];
    int found = 0;
    for (size_t off = 0; off + 16 < textSize && found < 12; off += sizeof(code)) {
        size_t chunk = textSize - off;
        if (chunk > sizeof(code)) chunk = sizeof(code);
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)(textBase + off), code, chunk, &got) || got < 16) continue;
        for (size_t i = 0; i + 7 < got && found < 12; ++i) {
            // lea/mov r64, [rip+disp32]：REX.W 8B/8D, ModRM mod=00 rm=101
            const uint8_t rex = code[i];
            if (rex != 0x48 && rex != 0x4C) continue;
            const uint8_t op = code[i + 1];
            if (op != 0x8D && op != 0x8B) continue;
            if ((code[i + 2] & 0xC7) != 0x05) continue;
            int32_t disp = 0;
            memcpy(&disp, code + i + 3, 4);
            const uintptr_t insnEnd = textBase + off + i + 7;
            const uintptr_t target = insnEnd + (intptr_t)disp;
            if (target < entry - 0x40 || target > entry + 0x40) continue;
            found++;
            logLine("xref: ref #%d at rva=0x%llX -> %p", found,
                    (unsigned long long)(textBase + off + i - (uintptr_t)mod), (void*)target);
        }
    }
    logLine("xref: done, %d reference(s)", found);
    return 0;
}


// ---------------------------------------------------------------- 导出映射区里 解密后元数据"
//
// 线索：类型名字符串出现在一 MEM_MAPPED 区域里（0x7ffd...），
// 说明游戏把解密后的元数据映射到独立内存。把整块 dump 出来 Il2CppDumper 试试
DWORD WINAPI dumpMappedMetadata(LPVOID) {
    Sleep(25000);
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES)
        return 0;
    HANDLE h = GetCurrentProcess();

    // 先找一处类型名字符串（只在映射区里找）
    const char* want = "MonoEntityLayerCollisionHelper";
    uintptr_t found = 0;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    while (p < limit && !found) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool mapped = (mbi.State == MEM_COMMIT) &&
                            (mbi.Type == MEM_MAPPED || mbi.Type == MEM_IMAGE) &&
                            !(mbi.Protect & PAGE_GUARD) && !(mbi.Protect & PAGE_NOACCESS);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (mapped && size > 0x1000) {
            for (size_t off = 0; off < size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 64) continue;
                const size_t nlen = strlen(want);
                for (size_t i = 0; i + nlen + 1 <= got; ++i) {
                    if (memcmp(buf + i, want, nlen) == 0 && buf[i + nlen] == 0) {
                        found = (uintptr_t)(base + off + i);
                        break;
                    }
                }
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    if (!found) {
        logLine("meta: mapped string not found");
        return 0;
    }

    // 找它所在的映射区起
MEMORY_BASIC_INFORMATION mm{};
    if (!VirtualQuery((void*)found, &mm, sizeof(mm))) return 0;
    const uintptr_t regionBase = (uintptr_t)mm.BaseAddress;
    const size_t regionSize = mm.RegionSize;
    logLine("meta: string at %p, mapped region base=%p size=%zu MB type=%lu", (void*)found,
            (void*)regionBase, regionSize / 1048576, mm.Type);

    // 打印区域开 64 字节，看是不 MHY
uint8_t head[64] = {0};
    SIZE_T got = 0;
    if (ReadProcessMemory(h, (void*)regionBase, head, sizeof(head), &got) && got == sizeof(head)) {
        char hex[200] = {0};
        int n = 0;
        for (int i = 0; i < 32; ++i) n += snprintf(hex + n, sizeof(hex) - n, "%02X ", head[i]);
        logLine("meta: region head = %s", hex);
    }

    // 整块导出（最 512 MB
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\mapped_meta.bin", "wb");
    if (!f) {
        logLine("meta: cannot create file");
        return 0;
    }
    size_t cap = regionSize > (512ull << 20) ? (512ull << 20) : regionSize;
    size_t written = 0;
    static uint8_t obuf[1 << 20];
    while (written < cap) {
        size_t take = sizeof(obuf);
        if (cap - written < take) take = cap - written;
        SIZE_T rd = 0;
        if (!ReadProcessMemory(h, (void*)(regionBase + written), obuf, take, &rd) || rd == 0) break;
        fwrite(obuf, 1, rd, f);
        written += rd;
        if (rd < take) break;
    }
    fclose(f);
    logLine("meta: wrote %zu MB to mapped_meta.bin (offset of string inside = 0x%llX)", written / 1048576,
            (unsigned long long)(found - regionBase));
    return 0;
}


static int bpApply(uintptr_t a1, uintptr_t a2);
void bpLogHitRegs(CONTEXT* c);

// ---------------------------------------------------------------- 追元数据的读取与解密
//
// 1) hook CreateFileW / ReadFile / MapViewOfFile
// 2) 发现打开的是 global-metadata.dat，就记住句柄 / 缓冲
// 3)  读到的那块缓冲区"上装写断点， 谁在写它"= 解密函数
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                     HANDLE);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using MapViewFn = LPVOID(WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);

static CreateFileWFn g_origCreateFileW = nullptr;
static ReadFileFn g_origReadFile = nullptr;
static MapViewFn g_origMapView = nullptr;

static HANDLE g_metaFile = INVALID_HANDLE_VALUE;
static std::atomic<uintptr_t> g_metaBuf{0};
static std::atomic<DWORD> g_metaSize{0};

HANDLE WINAPI hkCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                            DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = g_origCreateFileW(name, access, share, sa, disp, flags, tmpl);
    if (name && h != INVALID_HANDLE_VALUE) {
        const wchar_t* p = name;
        // 粗略匹配 "metadata"
        for (const wchar_t* q = p; *q; ++q) {
            if ((q[0] == L'm' || q[0] == L'M') && (q[1] == L'e' || q[1] == L'E') &&
                (q[2] == L't' || q[2] == L'T') && (q[3] == L'a' || q[3] == L'A')) {
                g_metaFile = h;
                logLine("metafile: opened handle=%p", h);
                break;
            }
        }
    }
    return h;
}

BOOL WINAPI hkReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD read, LPOVERLAPPED ov) {
    const BOOL ok = g_origReadFile(h, buf, toRead, read, ov);
    if (ok && h == g_metaFile && buf && read && *read > 0) {
        logLine("metafile: read %lu bytes into %p", *read, buf);
        g_metaBuf.store((uintptr_t)buf);
        g_metaSize.store(*read);
        // 在这块缓冲区上装写断点，抓解密函
bpApply((uintptr_t)buf, (uintptr_t)buf + 0x40);
        g_bpArmed.store(true);
        logLine("metafile: write-BP armed on buffer (decryptor hunt)");
        // 8 秒后拆掉
        CreateThread(
                nullptr, 0,
                [](LPVOID) -> DWORD {
                    Sleep(8000);
                    const int n = bpApply(0, 0);
                    g_bpArmed.store(false);
                    logLine("metafile: write-BP cleared (%d threads), %d hit(s)", n,
                            g_bpLogCount.load());
                    return 0;
                },
                nullptr, 0, nullptr);
    }
    return ok;
}

LPVOID WINAPI hkMapView(HANDLE h, DWORD access, DWORD hi, DWORD lo, SIZE_T viewsize) {
    LPVOID r = g_origMapView(h, access, hi, lo, viewsize);
    if (r && h == g_metaFile) {
        logLine("metafile: mapped at %p size=%zu", r, (size_t)viewsize);
    }
    return r;
}

void installMetadataHooks() {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return;
    void* cf = (void*)GetProcAddress(k32, "CreateFileW");
    void* rf = (void*)GetProcAddress(k32, "ReadFile");
    void* mv = (void*)GetProcAddress(k32, "MapViewOfFile");
    if (cf && MH_CreateHook(cf, (void*)hkCreateFileW, (void**)&g_origCreateFileW) == MH_OK)
        MH_EnableHook(cf);
    if (rf && MH_CreateHook(rf, (void*)hkReadFile, (void**)&g_origReadFile) == MH_OK)
        MH_EnableHook(rf);
    if (mv && MH_CreateHook(mv, (void*)hkMapView, (void**)&g_origMapView) == MH_OK)
        MH_EnableHook(mv);
    logLine("metafile: hooks installed (CreateFileW=%p ReadFile=%p MapViewOfFile=%p)", cf, rf, mv);
}


// ---------------------------------------------------------------- 找加密元数据缓冲区并抓解
//
// 加密文件头是 "MHY\0"（字 4D 48 59 00），后面 0x0059484D
// 游戏读进来后会在原地解密，所以在这块内存上装写断点就能抓到解密函数
DWORD WINAPI huntMhyBuffer(LPVOID) {
    // 越早越好：DLL 加载 2 秒就开始找（元数据可能 Unity 初始化时才读
for (int attempt = 0; attempt < 12; ++attempt) {
        Sleep(attempt == 0 ? 2000 : 3000);
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES)
            return 0;

        HANDLE h = GetCurrentProcess();
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
        uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
        MEMORY_BASIC_INFORMATION mbi{};
        static uint8_t buf[1 << 20];
        uintptr_t hit = 0;

        while (p < limit && !hit) {
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            const bool rw = (mbi.State == MEM_COMMIT) &&
                            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) &&
                            !(mbi.Protect & PAGE_GUARD);
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            size_t size = mbi.RegionSize;
            if (rw && size > 0x100000) {   // 只看 >= 1 MB 的可写区（元数据 78.9 MB                 
                for (size_t off = 0; off < size; off += sizeof(buf)) {
                    size_t chunk = size - off;
                    if (chunk > sizeof(buf)) chunk = sizeof(buf);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 16) continue;
                    for (size_t i = 0; i + 16 <= got; i += 4) {
                        if (buf[i] == 0x4D && buf[i + 1] == 0x48 && buf[i + 2] == 0x59 &&
                            buf[i + 3] == 0x00) {
                            // 后面 4 字节应该 0x0059484D
                            const uint32_t v = *(const uint32_t*)(buf + i + 4);
                            if (v != 0x0059484D) continue;
                            hit = (uintptr_t)(base + off + i);
                            break;
                        }
                    }
                }
            }
            uint8_t* next = base + size;
            if (next <= p) break;
            p = next;
        }

        if (!hit) {
            logLine("mhy: attempt %d - MHY header not in memory yet", attempt + 1);
            continue;
        }
        logLine("mhy: encrypted metadata buffer FOUND at %p (attempt %d)", (void*)hit, attempt + 1);
        // 装写断点抓解
g_bpLogCount.store(0);
        g_bpArmed.store(true);
        const int n = bpApply(hit + 0x40, hit + 0x1000);
        logLine("mhy: write-BP armed at %p / %p on %d thread(s)", (void*)(hit + 0x40),
                (void*)(hit + 0x1000), n);
        for (int i = 0; i < 200 && g_bpLogCount.load() < 8; ++i) Sleep(100);
        const int cleared = bpApply(0, 0);
        g_bpArmed.store(false);
        logLine("mhy: cleared (%d threads), %d hit(s)", cleared, g_bpLogCount.load());
        return 0;
    }
    logLine("mhy: gave up, header never appeared");
    return 0;
}


// ----------------------------------------------------------------  TypeInfo 定位类对象并找实
//
// 来自未知作弊论坛 7.1 dump 的信息：
//   Class: MoleMole.MonoEntityLayerCollisionHelper
//   TypeInfo: 0x50005454088        （dump 的地址空间，模块基址 = 0x500000000
//   字段: +0x18 CLIJGFEKEJM type
//
// 本机真实地址 = GetModuleHandle() + (TypeInfo - 0x500000000)
static const unsigned long long kMonoEntityHelperTypeInfo = 0x50005454088ull;
static const unsigned long long kDumpImageBase = 0x50000000000ull;   // dump 的镜像基址

static int g_collInstanceCount = 0;
static uintptr_t g_collInstances[8] = {0};
static bool g_collFindDone = false;

DWORD WINAPI findCollisionHelper(LPVOID) {
    Sleep(8000);
    if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\probe.on") == INVALID_FILE_ATTRIBUTES)
        return 0;

    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    const uintptr_t cls = (uintptr_t)mod + (uintptr_t)(kMonoEntityHelperTypeInfo - kDumpImageBase);
    logLine("coll: module=%p  class(MonoEntityLayerCollisionHelper)=%p", (void*)mod, (void*)cls);

    // 先读类对象前 0x40 字节，确认不是垃
uintptr_t head[8] = {0};
    SIZE_T got = 0;
    if (ReadProcessMemory(h, (void*)cls, head, sizeof(head), &got) && got == sizeof(head)) {
        logLine("coll: class head = %p %p %p %p %p %p %p %p", (void*)head[0], (void*)head[1],
                (void*)head[2], (void*)head[3], (void*)head[4], (void*)head[5], (void*)head[6],
                (void*)head[7]);
    } else {
        logLine("coll: class address unreadable");
        return 0;
    }

    // 全堆 第一 qword == 类地址"的对象（MonoBehaviour 实例
SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int found = 0;
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\collision_helpers.txt", "w");

    while (p < limit && found < 40) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool rw = (mbi.State == MEM_COMMIT) &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY ||
                         mbi.Protect == PAGE_EXECUTE_READWRITE) &&
                        !(mbi.Protect & PAGE_GUARD) &&
                        (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (rw && size > 0x1000) {
            for (size_t off = 0; off + 0x20 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T g2 = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &g2) || g2 < 0x20) continue;
                for (size_t i = 0; i + 0x20 <= g2; i += 8) {
                    const uintptr_t v = *(const uintptr_t*)(buf + i);
                    if (v != cls) continue;
                    const uintptr_t obj = (uintptr_t)(base + off + i);
                    const int typeVal = *(const int*)(buf + i + 0x18);
                    if (g_collInstanceCount < 8) {
                        g_collInstances[g_collInstanceCount++] = obj;
                        g_collFindDone = true;
                    }
                    found++;
                    if (f) {
                        fprintf(f, "OBJ 0x%llx  type(+0x18)=%d\n", (unsigned long long)obj, typeVal);
                        fflush(f);
                    }
                    logLine("coll: instance %d at %p  type=%d", found, (void*)obj, typeVal);
                    if (found >= 40) break;
                }
                if (found >= 40) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    if (f) fclose(f);
    logLine("coll: done, %d instance(s) -> collision_helpers.txt", found);
    return 0;
}


// ---------------------------------------------------------------- 客户端穿墙：直接调用 Unity  IgnoreLayerCollision
//
// 7.1 dump 给出 RVA
//   MonoEntityLayerCollisionHelper.GetCollisionLayer  0x7A466E0  (instance method -> int)
//   UnityEngine.Physics.IgnoreLayerCollision          0x19C5DA70 (static: int,int,bool)
//
// IL2CPP  ABI：实例方 = (this, ...args, MethodInfo*)；静态方 = (...args, MethodInfo*)
// 这两个方法都不使 MethodInfo，所以传 nullptr 即可
using GetLayerFn = int (*)(void* self, void* mi);
using IgnoreLayerFn = void (*)(int l1, int l2, bool ignore, void* mi);

static const unsigned long long kRvaGetCollisionLayer = 0x7A466E0ull;
static const unsigned long long kRvaIgnoreLayerCollision = 0x19C5DA70ull;


// 找组件实例（复用类地址
static int findCollInstances(HANDLE h, uintptr_t cls, uintptr_t out[], int maxOut) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int found = 0;
    while (p < limit && found < maxOut) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool rw = (mbi.State == MEM_COMMIT) &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) &&
                        !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (rw && size > 0x1000) {
            for (size_t off = 0; off + 0x20 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T g2 = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &g2) || g2 < 0x20) continue;
                for (size_t i = 0; i + 0x20 <= g2; i += 8) {
                    if (*(const uintptr_t*)(buf + i) != cls) continue;
                    out[found++] = (uintptr_t)(base + off + i);
                    if (found >= maxOut) break;
                }
                if (found >= maxOut) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    return found;
}

DWORD WINAPI collIgnoreThread(LPVOID param) {
    const bool enable = ((intptr_t)param) != 0;
    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;

    const uintptr_t cls = (uintptr_t)mod + (uintptr_t)(kMonoEntityHelperTypeInfo - kDumpImageBase);
    if (!g_collFindDone || g_collInstanceCount == 0) {
        g_collInstanceCount = findCollInstances(h, cls, g_collInstances, 8);
        g_collFindDone = true;
        logLine("coll: rescan found %d instance(s)", g_collInstanceCount);
    } else {
        logLine("coll: using %d cached instance(s)", g_collInstanceCount);
    }
    if (g_collInstanceCount == 0) {
        logLine("coll: no instance, abort");
        return 0;
    }

    auto getLayer = (GetLayerFn)((uintptr_t)mod + kRvaGetCollisionLayer);
    auto ignoreLayer = (IgnoreLayerFn)((uintptr_t)mod + kRvaIgnoreLayerCollision);

    for (int k = 0; k < g_collInstanceCount; ++k) {
        int layer = -1;
        layer = getLayer((void*)g_collInstances[k], nullptr);
        logLine("coll: instance %d at %p -> layer=%d", k, (void*)g_collInstances[k], layer);
        if (layer < 0 || layer > 31) continue;
        int ok = 0;
        for (int other = 0; other < 32; ++other) {
            ignoreLayer(layer, other, enable, nullptr);
            ok++;
        }
        logLine("coll: IgnoreLayerCollision(%d, *) = %s for %d layers", layer,
                enable ? "DISABLE" : "ENABLE", ok);
    }
    return 0;
}


// ---------------------------------------------------------------- 关闭全部层间碰撞（暴 noclip
//
// 不需要找组件实例：直接把 32x32 的层碰撞矩阵全部设为"忽略"
// Physics.IgnoreLayerCollision(l1, l2, true) @ RVA 0x19C5DA70
DWORD WINAPI collDisableAllThread(LPVOID param) {
    const bool enable = ((intptr_t)param) != 0;   // param=1 表示"关闭碰撞"
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    auto ignoreLayer = (IgnoreLayerFn)((uintptr_t)mod + kRvaIgnoreLayerCollision);
    logLine("collall: setting ignore=%d for all 32x32 layer pairs", enable ? 1 : 0);
    int calls = 0;
    for (int a = 0; a < 32; ++a) {
        for (int b = a; b < 32; ++b) {
            ignoreLayer(a, b, enable, nullptr);
            calls++;
        }
    }
    logLine("collall: done, %d call(s)", calls);
    installPhysicsNoHitHooks(enable);
    return 0;
}


// ---------------------------------------------------------------- 让物理检测永 没碰 （真·noclip
//
// 角色控制器用的是 layerMask 的独立检测（CheckCapsule / CapsuleCast / SphereCast），
// 不受 IgnoreLayerCollision 影响。把这些函数的入口直接改 返回 false"
//
// x64 调用约定由调用方清栈，所以裸函数 mov al,0; ret 对任何签名都安全
extern "C" __attribute__((naked)) void physicsNoHitStub() {
    __asm__ volatile("xor %eax, %eax\n\tret\n");
}

static const unsigned long long kNoHitRvas[] = {
        // Unity 物理查询（相 拾取/UI 用）
        0x19C5ABD0ull,   // Physics.CheckCapsule
        0x19C5AC20ull,
        0x19C5AC70ull,
        0x19C56A70ull,   // Physics.CapsuleCast
        0x19C56B40ull,
        0x19C56C20ull,
        0x19C56CF0ull,
        0x19C57B50ull,   // Physics.SphereCast
        0x19C57BF0ull,
        0x19C57C90ull,
        0x19C5DCD0ull,   // Physics.ComputePenetration
        0x19C52780ull,   // Physics.Raycast
        0x19C527E0ull,
        0x19C52840ull,
        0x19C52890ull,
        0x19C528E0ull,
        0x19C52930ull,
        0x19C529A0ull,
        0x19C52A60ull,
        // VCHumanoidMove 里接 Vector3 
// bool 方法（位置校验候选）
        0xAC743B0ull,   // FMJHKJIFJEO(Vector3)
        0xAC77610ull,   // EEMCJIMOJIE(Vector3,Vector3)
        0xAC77700ull,   // ACJFCJCNJLF(Vector3,float)
        0xAC7D1C0ull,   // PPNDKNADOEK(Vector3)
        0xAC7DB20ull,   // NDPLPEHMAFG(Vector3)
        0xAC8F820ull,   // GEMKKBHHJAC(Vector3,Vector3)
        0xAC95D60ull,   // LFBEIAMNFCP(Vector3,ref Vector3)
        0xAC9BC60ull,   // FLBDENGBEOL(Vector3,Vector3)
};
static const int kNoHitCount = 27;
static bool g_noHitInstalled[32] = {false};
static bool g_noHitEnabled = false;

// 把一段代码的 2 字节改成 xor eax,eax + ret（不 MinHook 添麻烦）
static bool patchReturnFalse(uintptr_t addr, bool enable, uint8_t original[16]) {
    HANDLE h = GetCurrentProcess();
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)addr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    if (enable) {
        if (original[0] != 0xCC) {
            SIZE_T rd = 0;
            ReadProcessMemory(h, (void*)addr, original, 16, &rd);
        }
        const uint8_t code[3] = {0x31, 0xC0, 0xC3};   // xor eax,eax ; ret
        SIZE_T wr = 0;
        WriteProcessMemory(h, (void*)addr, code, 3, &wr);
        FlushInstructionCache(h, (void*)addr, 3);
    } else if (original[0] != 0xCC) {
        SIZE_T wr = 0;
        WriteProcessMemory(h, (void*)addr, original, 16, &wr);
        FlushInstructionCache(h, (void*)addr, 16);
    }
    VirtualProtect((void*)addr, 16, oldProt, &oldProt);
    return true;
}

static uint8_t g_noHitOriginal[32][16] = {};

void installPhysicsNoHitHooks(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    int ok = 0;
    for (int i = 0; i < kNoHitCount; ++i) {
        const uintptr_t addr = (uintptr_t)mod + (uintptr_t)kNoHitRvas[i];
        if (patchReturnFalse(addr, enable, g_noHitOriginal[i])) {
            ok++;
            if (enable) logLine("nohit: patched %p (rva 0x%llX)", (void*)addr, kNoHitRvas[i]);
        }
    }
    g_noHitEnabled = enable;
    logLine("nohit: %s, %d function(s)", enable ? "ENABLED" : "RESTORED", ok);
}


// ---------------------------------------------------------------- 多类实例扫描（验 TypeInfo 数学 + 找实体）
//
// 7.1 dump 给出 TypeInfo（dump 地址空间，基址 0x50000000000）：
//   MoleMole.BaseActor                      0x500018253b8
//   MoleMole.MonoCollisionHelper            0x50001a392e8
//   MoleMole.MonoWallCollisionHelper        0x50007d61b68
//   MoleMole.MonoEntityLayerCollisionHelper 0x50005454088
struct ClsTarget {
    const char* name;
    unsigned long long typeInfo;
};
static const ClsTarget kClsTargets[] = {
        {"MoleMole.BaseActor", 0x500018253b8ull},
        {"MoleMole.MonoCollisionHelper", 0x50001a392e8ull},
        {"MoleMole.MonoWallCollisionHelper", 0x50007d61b68ull},
        {"MoleMole.MonoEntityLayerCollisionHelper", 0x50005454088ull},
};
static const int kClsTargetCount = 4;

DWORD WINAPI scanMultiClasses(LPVOID) {
    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;

    // 两种候选地址
//    原值（dump 里的 TypeInfo 可能就是进程内的真实绝对地址 x5000 段是映射好的
//    模块换算值（mod + (TypeInfo - 0x50000000000)
    uintptr_t clsAddr[8] = {0};
    uintptr_t clsAddr2[8] = {0};
    for (int i = 0; i < kClsTargetCount; ++i) {
        clsAddr[i] = (uintptr_t)kClsTargets[i].typeInfo;
        clsAddr2[i] = (uintptr_t)mod + (uintptr_t)(kClsTargets[i].typeInfo - kDumpImageBase);
        uintptr_t head[4] = {0};
        SIZE_T got = 0;
        const bool okA = ReadProcessMemory(h, (void*)clsAddr[i], head, sizeof(head), &got) &&
                         got == sizeof(head);
        uintptr_t head2[4] = {0};
        SIZE_T got2 = 0;
        const bool okB = ReadProcessMemory(h, (void*)clsAddr2[i], head2, sizeof(head2), &got2) &&
                         got2 == sizeof(head2);
        logLine("multi: %-42s literal=%p ok=%d head=%p | modrel=%p ok=%d head=%p",
                kClsTargets[i].name, (void*)clsAddr[i], okA ? 1 : 0, (void*)head[0],
                (void*)clsAddr2[i], okB ? 1 : 0, (void*)head2[0]);
    }

    // 一遍扫描，统计每个类的实例
SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int counts[8] = {0};
    uintptr_t firstObj[8] = {0};
    const uintptr_t selfStack = (uintptr_t)&clsAddr;   // 排除自己的栈 
    while (p < limit) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool rw = (mbi.State == MEM_COMMIT) &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) &&
                        !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (rw && size > 0x1000 && size < 0x40000000) {
            for (size_t off = 0; off + 0x20 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T g2 = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &g2) || g2 < 0x20) continue;
                for (size_t i = 0; i + 0x20 <= g2; i += 8) {
                    const uintptr_t v = *(const uintptr_t*)(buf + i);
                    const uintptr_t here = (uintptr_t)(base + off + i);
                    if (here > selfStack - 0x20000 && here < selfStack + 0x20000) continue;
                    for (int k = 0; k < kClsTargetCount; ++k) {
                        if (v != clsAddr[k] && v != clsAddr2[k]) continue;
                        counts[k]++;
                        if (!firstObj[k])
                            firstObj[k] = (uintptr_t)(base + off + i);
                    }
                }
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }

    FILE* f = fopen("D:\\Programs\\gs71-runtime\\multi_classes.txt", "w");
    for (int k = 0; k < kClsTargetCount; ++k) {
        logLine("multi: %-42s instances=%d first=%p", kClsTargets[k].name, counts[k],
                (void*)firstObj[k]);
        if (f) fprintf(f, "%s instances=%d first=0x%llx\n", kClsTargets[k].name, counts[k],
                       (unsigned long long)firstObj[k]);
        // 打印第一个实例的 8  qword（看字段布局
if (firstObj[k] && f) {
            uintptr_t q[8] = {0};
            SIZE_T g3 = 0;
            if (ReadProcessMemory(h, (void*)firstObj[k], q, sizeof(q), &g3) &&
                g3 == sizeof(q)) {
                fprintf(f, "   qwords:");
                for (int j = 0; j < 8; ++j) fprintf(f, " %p", (void*)q[j]);
                fprintf(f, "\n");
            }
        }
    }
    if (f) fclose(f);
    logLine("multi: done -> multi_classes.txt");
    return 0;
}


// ---------------------------------------------------------------- 方案 A：方 RVA  MethodInfo  klass  实例
//
// 逻辑
//   1. 方法绝对地址 = 模块基址 + RVA
//   2. 全内存搜 qword == 该地址  那就 MethodInfo.method 0x00
//   3. MethodInfo.klass  +0x18（标 IL2CPP 布局
//   4. 再搜 qword == klass  对象 klass 字段  实例
struct PlanATarget {
    const char* label;
    unsigned long long rva;
    int fieldOff;      // 找到实例后要读的字段偏移  表示不读
};
static const PlanATarget kPlanA[] = {
        {"MonoEntityLayerCollisionHelper.GetCollisionLayer", 0x7A466E0ull, 0x18},
        {"VCHumanoidMove.FMJHKJIFJEO(Vector3)->bool", 0xAC743B0ull, 0x3BC},
        {"VCHumanoidMove.BJGFJCEDGJF()->bool", 0xAC74310ull, 0x3BC},
};
static const int kPlanACount = 3;

// 用一次性扫描把 "值等于某个目标地址" 的位置收集起
static int collectQwordRefs(uintptr_t needle, uintptr_t out[], int maxOut, uintptr_t selfStack) {
    HANDLE h = GetCurrentProcess();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int n = 0;
    while (p < limit && n < maxOut) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool rw = (mbi.State == MEM_COMMIT) &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                         mbi.Protect == PAGE_WRITECOPY || mbi.Protect == PAGE_EXECUTE_READ) &&
                        !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (rw && size > 0x1000 && size < 0x40000000) {
            for (size_t off = 0; off + 8 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 8) continue;
                for (size_t i = 0; i + 8 <= got; i += 8) {
                    if (*(const uintptr_t*)(buf + i) != needle) continue;
                    const uintptr_t here = (uintptr_t)(base + off + i);
                    if (selfStack && here > selfStack - 0x20000 && here < selfStack + 0x20000)
                        continue;
                    out[n++] = here;
                    if (n >= maxOut) break;
                }
                if (n >= maxOut) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    return n;
}

DWORD WINAPI planAThread(LPVOID) {
    Sleep(2000);
    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    const uintptr_t selfStack = (uintptr_t)&mod;
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\planA.txt", "w");
    if (!f) return 0;
    fprintf(f, "# 方案 A：方 RVA -> MethodInfo -> klass -> 实例\n");

    for (int t = 0; t < kPlanACount; ++t) {
        const uintptr_t methAddr = (uintptr_t)mod + (uintptr_t)kPlanA[t].rva;
        fprintf(f, "\n== %s ==\n   method abs = 0x%llx\n", kPlanA[t].label,
                (unsigned long long)methAddr);
        logLine("planA: %s method=%p", kPlanA[t].label, (void*)methAddr);

        static uintptr_t refs[16];
        const int nrefs = collectQwordRefs(methAddr, refs, 16, selfStack);
        fprintf(f, "   refs=%d\n", nrefs);
        logLine("planA:   refs=%d", nrefs);

        for (int i = 0; i < nrefs && i < 6; ++i) {
            uintptr_t q[6] = {0};
            SIZE_T got = 0;
            if (!ReadProcessMemory(h, (void*)refs[i], q, sizeof(q), &got) || got < sizeof(q))
                continue;
            fprintf(f, "   ref[%d] @0x%llx: [%p] invoker=%p name=%p klass=%p ret=%p\n", i,
                    (unsigned long long)refs[i], (void*)q[0], (void*)q[1], (void*)q[2],
                    (void*)q[3], (void*)q[4]);
            const uintptr_t klass = q[3];
            if (klass < 0x10000 || klass > 0x7fffffffffffULL) continue;
            char nm[64] = {0};
            const bool hasName = resolveName(h, q[2], nm, sizeof(nm));
            fprintf(f, "          klass=0x%llx %s%s\n", (unsigned long long)klass,
                    hasName ? "name=" : "", hasName ? nm : "");

            //  klass 找实
static uintptr_t inst[24];
            const int ninst = collectQwordRefs(klass, inst, 24, selfStack);
            fprintf(f, "          instances=%d\n", ninst);
            logLine("planA:     klass=%p instances=%d", (void*)klass, ninst);
            for (int j = 0; j < ninst && j < 6; ++j) {
                uintptr_t q2[2] = {0};
                SIZE_T g2 = 0;
                if (!ReadProcessMemory(h, (void*)inst[j], q2, sizeof(q2), &g2) || g2 < 16) continue;
                int fieldVal = -1;
                if (kPlanA[t].fieldOff) {
                    SIZE_T g3 = 0;
                    ReadProcessMemory(h, (void*)(inst[j] + kPlanA[t].fieldOff), &fieldVal, 4, &g3);
                }
                fprintf(f, "            inst[%d] @0x%llx  field(+0x%X)=%d\n", j,
                        (unsigned long long)inst[j], kPlanA[t].fieldOff, fieldVal);
            }
        }
    }
    fclose(f);
    logLine("planA: done -> planA.txt");
    return 0;
}


// ---------------------------------------------------------------- 用坐标反查玩家自己的 actor
//
// pos.txt 里放 "x y z"（由外部 /snap 写入）
// 思路：玩 actor 的对象图里一定存着当前坐标
//   1. 对每 BaseActor 实例，扫 ±0x1000 邻域，找等于 (x,y,z) 
// float 三元
//   2. 命中 = 玩家 actor
//   3. 在玩 actor ±0x4000 邻域里找 CollisionHelper 实例指针  拿到玩家自己的碰撞组
static bool posTripleMatch(uint8_t* buf, size_t len, float px, float py, float pz,
                           size_t* outOff) {
    for (size_t i = 0; i + 12 <= len; i += 4) {
        float a, b, c;
        memcpy(&a, buf + i, 4);
        memcpy(&b, buf + i + 4, 4);
        memcpy(&c, buf + i + 8, 4);
        if (!(fabsf(a - px) <= 0.6f)) continue;
        if (!(fabsf(b - py) <= 0.6f)) continue;
        if (!(fabsf(c - pz) <= 0.6f)) continue;
        if (!(fabsf(a) < 1e6f && fabsf(b) < 1e6f && fabsf(c) < 1e6f)) continue;
        *outOff = i;
        return true;
    }
    return false;
}

DWORD WINAPI findPlayerActor(LPVOID) {
    HANDLE h = GetCurrentProcess();
    FILE* pf = fopen("D:\\Programs\\gs71-runtime\\pos.txt", "r");
    if (!pf) {
        logLine("player: pos.txt missing");
        return 0;
    }
    float px = 0, py = 0, pz = 0;
    if (fscanf(pf, "%f %f %f", &px, &py, &pz) != 3) {
        fclose(pf);
        logLine("player: bad pos.txt");
        return 0;
    }
    fclose(pf);
    logLine("player: target pos %.2f %.2f %.2f", px, py, pz);

    // 1) 收集 BaseActor 实例
    static uintptr_t actors[512];
    const int nActors = collectQwordRefs((uintptr_t)0x500018253b8ull, actors, 512, 0);
    logLine("player: %d BaseActor instance(s)", nActors);

    // 2) 每个 actor 邻域找坐
static uint8_t buf[0x2000];
    uintptr_t playerActor = 0;
    int matches = 0;
    for (int i = 0; i < nActors; ++i) {
        const uintptr_t a = actors[i];
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)(a - 0x1000), buf, sizeof(buf), &got)) continue;
        size_t off = 0;
        if (posTripleMatch(buf, got, px, py, pz, &off)) {
            matches++;
            logLine("player: actor %d @%p contains pos at %+d", i, (void*)a,
                    (int)off - 0x1000);
            if (!playerActor) playerActor = a;
        }
    }
    logLine("player: %d actor(s) contain the position", matches);
    if (!playerActor) return 0;

    // 3) 玩家 actor 附近找碰撞组件指
static uintptr_t helpers[16];
    const int nH = collectQwordRefs((uintptr_t)0x50005454088ull, helpers, 16, 0);
    logLine("player: %d CollisionHelper instance(s) total", nH);
    static uint8_t big[0x8000];
    SIZE_T got2 = 0;
    if (ReadProcessMemory(h, (void*)(playerActor - 0x4000), big, sizeof(big), &got2)) {
        for (int j = 0; j < nH; ++j) {
            for (size_t i = 0; i + 8 <= got2; i += 8) {
                uintptr_t v;
                memcpy(&v, big + i, 8);
                if (v != helpers[j]) continue;
                const uintptr_t slot = playerActor - 0x4000 + i;
                logLine("player: helper[%d] @%p referenced from %p (actor%+d)", j,
                        (void*)helpers[j], (void*)slot, (int)(slot - playerActor));
                int typeVal = -1;
                SIZE_T g3 = 0;
                ReadProcessMemory(h, (void*)(helpers[j] + 0x18), &typeVal, 4, &g3);
                logLine("player:   helper[%d] type(+0x18)=%d", j, typeVal);
            }
        }
    }

    // 顺便打印每个 helper  type，供人工比对
    for (int j = 0; j < nH; ++j) {
        int tv = -1;
        SIZE_T g4 = 0;
        ReadProcessMemory(h, (void*)(helpers[j] + 0x18), &tv, 4, &g4);
        logLine("player: all helper[%d] @%p type=%d", j, (void*)helpers[j], tv);
    }
    return 0;
}


// ----------------------------------------------------------------  VCHumanoidMove 实例定位玩家
//
// VCHumanoidMove.TypeInfo = 0x500026f1fe0（dump 原值即绝对地址
// 对每个实例扫 ±0x2000 邻域找玩家坐标三元组；命中者就是玩家自己的移动组件
DWORD WINAPI findPlayerMoveInst(LPVOID) {
    HANDLE h = GetCurrentProcess();
    FILE* pf = fopen("D:\\Programs\\gs71-runtime\\pos.txt", "r");
    if (!pf) {
        logLine("pmove: pos.txt missing");
        return 0;
    }
    float px = 0, py = 0, pz = 0;
    if (fscanf(pf, "%f %f %f", &px, &py, &pz) != 3) {
        fclose(pf);
        return 0;
    }
    fclose(pf);
    logLine("pmove: target %.2f %.2f %.2f", px, py, pz);

    // 各类候选锚
const struct {
        const char* name;
        uintptr_t klass;
        int fieldOff;
    } anchors[] = {
            {"VCHumanoidMove", 0x500026f1fe0ull, 0x3BC},
            {"EntityActor", 0x50001824ed0ull, 0x0},
            {"MonoEntityLayerCollisionHelper", 0x50005454088ull, 0x18},
    };

    static uintptr_t inst[512];
    static uint8_t buf[0x4000];
    for (int a = 0; a < 3; ++a) {
        const int n = collectQwordRefs(anchors[a].klass, inst, 512, 0);
        logLine("pmove: %s -> %d instance(s)", anchors[a].name, n);
        int hits = 0;
        for (int i = 0; i < n; ++i) {
            SIZE_T got = 0;
            if (!ReadProcessMemory(h, (void*)(inst[i] - 0x2000), buf, sizeof(buf), &got)) continue;
            size_t off = 0;
            if (!posTripleMatch(buf, got, px, py, pz, &off)) continue;
            hits++;
            int fv = -1;
            if (anchors[a].fieldOff) {
                SIZE_T g = 0;
                ReadProcessMemory(h, (void*)(inst[i] + anchors[a].fieldOff), &fv, 4, &g);
            }
            logLine("pmove:   MATCH %s[%d] @%p posAt%+d field(+0x%X)=%d", anchors[a].name, i,
                    (void*)inst[i], (int)off - 0x2000, anchors[a].fieldOff, fv);
        }
        logLine("pmove: %s matched %d/%d", anchors[a].name, hits, n);
    }
    return 0;
}


// ---------------------------------------------------------------- 只统计不改变行为：调用计数桩
//
// 对每个候选函数装一个裸桩：
//     inc qword ptr [rip+计数]
//     jmp qword ptr [rip+原函数跳板]
// 完全保留寄存器与参数，只多一次自增。用来判 哪些函数在移动时真被调用"
struct CountStub {
    uint8_t* mem;
    volatile long long* counter;
    void* trampoline;
};

static CountStub g_stubs[16] = {};
static int g_stubCount = 0;

static const unsigned long long kCountRvas[] = {
        0xAC743B0ull,   // FMJHKJIFJEO(Vector3)
        0xAC77610ull,   // EEMCJIMOJIE(Vector3,Vector3)
        0xAC77700ull,   // ACJFCJCNJLF(Vector3,float)
        0xAC7D1C0ull,   // PPNDKNADOEK(Vector3)
        0xAC7DB20ull,   // NDPLPEHMAFG(Vector3)
        0xAC8F820ull,   // GEMKKBHHJAC(Vector3,Vector3)
        0xAC95D60ull,   // LFBEIAMNFCP(Vector3,ref Vector3)
        0xAC9BC60ull,   // FLBDENGBEOL(Vector3,Vector3)
        0xAC74310ull,   // BJGFJCEDGJF()
        0x12E2E570ull,  // Miscs::IsInDivableWaterNonAlloc（水的检测，非墙
        0xDF5FE90ull,   // IFix.__Gen_Wrap_1345(Object, Vector3, float, bool) -> bool
        0xD8E2C60ull,   // IFix.__Gen_Wrap_823(Object, Vector3) -> bool
        0x130AB210ull,  // NOAMHIGAECO::BMJMPKKGIOF(Vector3, bool) -> bool
};
static const int kCountRvaNum = 13;

// 手工构造裸桩（不依 MinHook  trampoline 语义）：
//   +0   48 FF 05 <disp32>      inc qword ptr [rip+disp]   -> 指向 +16 的计数器
//   +7   FF 25 <disp32>         jmp qword ptr [rip+disp]   -> 指向 +24 的原函数
//   +13  90 * 3                 对齐填充
//   +16  counter (8 字节)
//   +24  original (8 字节)
static void* buildStub(uintptr_t target) {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    const uintptr_t base = (uintptr_t)mem;
    // inc qword ptr [rip+disp]
    mem[0] = 0x48;
    mem[1] = 0xFF;
    mem[2] = 0x05;
    int32_t d1 = (int32_t)((base + 16) - (base + 7));
    memcpy(mem + 3, &d1, 4);
    // jmp qword ptr [rip+disp]
    mem[7] = 0xFF;
    mem[8] = 0x25;
    int32_t d2 = (int32_t)((base + 24) - (base + 13));
    memcpy(mem + 9, &d2, 4);
    mem[13] = mem[14] = mem[15] = 0x90;
    *(uint64_t*)(mem + 16) = 0;         // counter
    *(uint64_t*)(mem + 24) = target;    // original（先填原地址，装上钩子后替换为跳板）
    return mem;
}

// 把目标前 5 字节改成 jmp 到桩（保存原 5 字节到桩 tail 里，用于还原
static bool patchJump(uintptr_t target, void* stub) {
    HANDLE h = GetCurrentProcess();
    DWORD oldProt = 0;
    if (!VirtualProtect((void*)target, 16, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    uint8_t* p = (uint8_t*)target;
    const uintptr_t rel = (uintptr_t)stub - (target + 5);
    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
    FlushInstructionCache(h, (void*)target, 5);
    VirtualProtect((void*)target, 16, oldProt, &oldProt);
    return true;
}

void installCounterStubs(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_stubCount > 0) {
            logLine("count: already installed");
            return;
        }
        for (int i = 0; i < kCountRvaNum; ++i) {
            const uintptr_t target = (uintptr_t)mod + (uintptr_t)kCountRvas[i];
            void* stub = buildStub(target);
            if (!stub) continue;
            // 保存 5 字节到桩尾，便于还原
            memcpy((uint8_t*)stub + 32, (void*)target, 5);
            // 跳板：把原函数前 5 字节搬到新内存，再跳 target+5
            uint8_t* tr = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                                PAGE_EXECUTE_READWRITE);
            if (tr) {
                memcpy(tr, (void*)target, 5);
                tr[5] = 0xE9;
                int32_t back = (int32_t)((target + 5) - ((uintptr_t)tr + 10));
                memcpy(tr + 6, &back, 4);
                *(uint64_t*)((uint8_t*)stub + 24) = (uint64_t)tr;
            }
            if (patchJump(target, stub)) {
                g_stubs[g_stubCount].mem = (uint8_t*)stub;
                g_stubs[g_stubCount].counter = (volatile long long*)((uint8_t*)stub + 16);
                g_stubs[g_stubCount].trampoline = tr;
                g_stubCount++;
                logLine("count: stub %d on rva 0x%llX", g_stubCount, kCountRvas[i]);
            }
        }
        logLine("count: %d stub(s) installed", g_stubCount);
    } else {
        for (int i = 0; i < g_stubCount; ++i) {
            const uintptr_t target = (uintptr_t)mod + (uintptr_t)kCountRvas[i];
            HANDLE h = GetCurrentProcess();
            DWORD oldProt = 0;
            if (VirtualProtect((void*)target, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                memcpy((void*)target, g_stubs[i].mem + 32, 5);
                FlushInstructionCache(h, (void*)target, 5);
                VirtualProtect((void*)target, 16, oldProt, &oldProt);
            }
            VirtualFree(g_stubs[i].mem, 0, MEM_RELEASE);
            if (g_stubs[i].trampoline) VirtualFree(g_stubs[i].trampoline, 0, MEM_RELEASE);
        }
        logLine("count: %d stub(s) removed", g_stubCount);
        g_stubCount = 0;
    }
}

void dumpCounters() {
    for (int i = 0; i < g_stubCount; ++i) {
        logLine("count: rva 0x%llX -> %lld call(s)", kCountRvas[i],
                (long long)*g_stubs[i].counter);
    }
}


// ---------------------------------------------------------------- 计数 v2：用 MinHook 保证指令重定位正
//
// v1 崩溃原因：手工只搬前 5 字节做跳板， 5 字节切断了指令，跳回 target+5 会落在指令中间
// v2：交 MinHook 生成 trampoline（它会正确搬完整条指令），裸桩只做两件事
//      inc qword ptr [rip+计数]
//      jmp qword ptr [rip+MinHook 给的 trampoline]
struct CountStubV2 {
    uint8_t* mem;
    volatile long long* counter;
    void* target;
};
static CountStubV2 g_stubs2[16] = {};
static int g_stub2Count = 0;

static void* buildStubV2() {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    const uintptr_t base = (uintptr_t)mem;
    mem[0] = 0x48;   // inc qword ptr [rip+disp32]
    mem[1] = 0xFF;
    mem[2] = 0x05;
    int32_t d1 = (int32_t)((base + 16) - (base + 7));
    memcpy(mem + 3, &d1, 4);
    mem[7] = 0xFF;   // jmp qword ptr [rip+disp32]
    mem[8] = 0x25;
    int32_t d2 = (int32_t)((base + 24) - (base + 13));
    memcpy(mem + 9, &d2, 4);
    mem[13] = mem[14] = mem[15] = 0x90;
    *(uint64_t*)(mem + 16) = 0;
    *(uint64_t*)(mem + 24) = 0;   // 稍后 trampoline
    return mem;
}

void installCounterStubsV2(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_stub2Count > 0) {
            logLine("count2: already installed");
            return;
        }
        for (int i = 0; i < kCountRvaNum; ++i) {
            void* target = (void*)((uintptr_t)mod + (uintptr_t)kCountRvas[i]);
            void* stub = buildStubV2();
            if (!stub) continue;
            void* tramp = nullptr;
            const MH_STATUS st = MH_CreateHook(target, stub, &tramp);
            if (st != MH_OK) {
                logLine("count2: MH_CreateHook failed (%d) on rva 0x%llX", (int)st, kCountRvas[i]);
                VirtualFree(stub, 0, MEM_RELEASE);
                continue;
            }
            *(uint64_t*)((uint8_t*)stub + 24) = (uint64_t)tramp;
            MH_EnableHook(target);
            g_stubs2[g_stub2Count].mem = (uint8_t*)stub;
            g_stubs2[g_stub2Count].counter = (volatile long long*)((uint8_t*)stub + 16);
            g_stubs2[g_stub2Count].target = target;
            g_stub2Count++;
            logLine("count2: stub %d on rva 0x%llX (tramp=%p)", g_stub2Count, kCountRvas[i], tramp);
        }
        logLine("count2: %d stub(s) installed", g_stub2Count);
    } else {
        for (int i = 0; i < g_stub2Count; ++i) {
            MH_DisableHook(g_stubs2[i].target);
            MH_RemoveHook(g_stubs2[i].target);
            VirtualFree(g_stubs2[i].mem, 0, MEM_RELEASE);
        }
        logLine("count2: %d stub(s) removed", g_stub2Count);
        g_stub2Count = 0;
    }
}

void dumpCountersV2() {
    for (int i = 0; i < g_stub2Count; ++i) {
        logLine("count2: rva 0x%llX -> %lld call(s)", kCountRvas[i],
                (long long)*g_stubs2[i].counter);
    }
}


// ---------------------------------------------------------------- 只改 VCHumanoidMove::ACJFCJCNJLF
//
// 计数桩实测：走路时只有这个函数被调用 625 次），其 8 个全 0
// 签名 System.Boolean ACJFCJCNJLF(Vector3 pos, System.Single r)
// 输出参数是它自己栈上的局部变量，对外只返
// bool，所以直接返 false 是安全的
static uint8_t g_soloOrig[16] = {};
static bool g_soloOn = false;

void patchSoloMoveCheck(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    const uintptr_t addr = (uintptr_t)mod + 0xAC77700ull;
    if (enable == g_soloOn) {
        logLine("solo: already %s", enable ? "on" : "off");
        return;
    }
    if (patchReturnFalse(addr, enable, g_soloOrig)) {
        g_soloOn = enable;
        logLine("solo: %s at %p (VCHumanoidMove::ACJFCJCNJLF)", enable ? "PATCHED" : "RESTORED",
                (void*)addr);
    } else {
        logLine("solo: patch failed at %p", (void*)addr);
    }
}


// ---------------------------------------------------------------- 内层查询 x12E2E570
//
// ACJFCJCNJLF 把输出参数初始化 0 之后才调用它（movq $0,0x38(rsp) / movl $0,0x34(rsp)），
// 所以让内层直接返回 false 时，外层读到的输出仍 0 — 语义就是"没碰 
static uint8_t g_solo2Orig[16] = {};
static bool g_solo2On = false;

void patchSolo2Check(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    const uintptr_t addr = (uintptr_t)mod + 0x12E2E570ull;
    if (enable == g_solo2On) {
        logLine("solo2: already %s", enable ? "on" : "off");
        return;
    }
    if (patchReturnFalse(addr, enable, g_solo2Orig)) {
        g_solo2On = enable;
        logLine("solo2: %s at %p (inner query)", enable ? "PATCHED" : "RESTORED", (void*)addr);
    } else {
        logLine("solo2: patch failed at %p", (void*)addr);
    }
}


// ---------------------------------------------------------------- IFix 动态包装器补丁
//
// 原神 IFix 热更新框架，碰撞判定通过 __Gen_Wrap_xxx 动态调用
// 两个候选（都返
// bool）：
//   __Gen_Wrap_1345(Object, Vector3, float, bool)  0xDF5FE90
//   __Gen_Wrap_823 (Object, Vector3)               0xD8E2C60
static uint8_t g_w1345Orig[16] = {};
static uint8_t g_w823Orig[16] = {};
static bool g_w1345On = false;
static bool g_w823On = false;

void patchWrap1345(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    const uintptr_t a = (uintptr_t)mod + 0xDF5FE90ull;
    if (patchReturnFalse(a, enable, g_w1345Orig)) {
        g_w1345On = enable;
        logLine("w1345: %s at %p", enable ? "PATCHED" : "RESTORED", (void*)a);
    }
}

void patchWrap823(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    const uintptr_t a = (uintptr_t)mod + 0xD8E2C60ull;
    if (patchReturnFalse(a, enable, g_w823Orig)) {
        g_w823On = enable;
        logLine("w823: %s at %p", enable ? "PATCHED" : "RESTORED", (void*)a);
    }
}


// ---------------------------------------------------------------- 单点 x130AB210
// NOAMHIGAECO::BMJMPKKGIOF(Vector3, bool) -> bool
// 计数实测：与 ACJFCJCNJLF 1:1（各 1858 次），是真正被每帧调用的判定
static uint8_t g_solo3Orig[16] = {};
static bool g_solo3On = false;

void patchSolo3Check(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    const uintptr_t a = (uintptr_t)mod + 0x130AB210ull;
    if (enable == g_solo3On) {
        logLine("solo3: already %s", enable ? "on" : "off");
        return;
    }
    if (patchReturnFalse(a, enable, g_solo3Orig)) {
        g_solo3On = enable;
        logLine("solo3: %s at %p (BMJMPKKGIOF)", enable ? "PATCHED" : "RESTORED", (void*)a);
    } else {
        logLine("solo3: patch failed at %p", (void*)a);
    }
}


// ----------------------------------------------------------------  addrs.txt 装写断点
// 用法：把要监视的地址写进 addrs.txt（每行一个十六进制），触 bpfile.now
// 用途：找出"每帧写玩家坐 的函数
DWORD WINAPI bpFromFileThread(LPVOID) {
    for (;;) {
        Sleep(1000);
        if (GetFileAttributesA("D:\\Programs\\gs71-runtime\\bpfile.now") == INVALID_FILE_ATTRIBUTES)
            continue;
        DeleteFileA("D:\\Programs\\gs71-runtime\\bpfile.now");

        FILE* f = fopen("D:\\Programs\\gs71-runtime\\addrs.txt", "r");
        if (!f) {
            logLine("bpfile: addrs.txt missing");
            continue;
        }
        uintptr_t a1 = 0, a2 = 0;
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            const uintptr_t v = (uintptr_t)strtoull(line, nullptr, 16);
            if (!v) continue;
            if (!a1) {
                a1 = v;
            } else if (!a2) {
                a2 = v;
                break;
            }
        }
        fclose(f);
        if (!a1) {
            logLine("bpfile: no valid address");
            continue;
        }
        g_bpLogCount.store(0);
        g_bpArmed.store(true);
        const int n = bpApply(a1, a2);
        logLine("bpfile: armed write-BP on %p / %p for 10s (%d threads)", (void*)a1, (void*)a2, n);
        for (int i = 0; i < 100 && g_bpLogCount.load() < 10; ++i) Sleep(100);
        const int cleared = bpApply(0, 0);
        g_bpArmed.store(false);
        logLine("bpfile: cleared (%d threads), %d hit(s)", cleared, g_bpLogCount.load());
    }
    return 0;
}


// ---------------------------------------------------------------- 构造函数捕获实
//
// 原理：构造函数被调用 rcx = this（新对象指针）
// 桩：
//     mov [rip+槽位], rcx        ; 记录一个实
//     jmp [rip+trampoline]       ; 继续执行原构造函
// 完全不影响行为，只是 this 抄下来
struct CtorCapture {
    uint8_t* mem;
    volatile uint64_t* slot;
    void* target;
    const char* name;
};
static CtorCapture g_ctors[8] = {};
static int g_ctorCount = 0;

static const struct {
    const char* name;
    unsigned long long rva;
} kCtorTargets[] = {
        {"MonoEntityLayerCollisionHelper..ctor", 0x7A468A0ull},
        {"MonoCollisionHelper..ctor", 0xFE9E240ull},
        {"MonoWallCollisionHelper..ctor", 0x7A9E230ull},
};
static const int kCtorNum = 3;

static void* buildCtorStub(const char* name) {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    const uintptr_t base = (uintptr_t)mem;
    // mov [rip+disp32], rcx   : 48 89 0D <disp32>
    mem[0] = 0x48;
    mem[1] = 0x89;
    mem[2] = 0x0D;
    int32_t d1 = (int32_t)((base + 32) - (base + 7));
    memcpy(mem + 3, &d1, 4);
    // jmp [rip+disp32]        : FF 25 <disp32>
    mem[7] = 0xFF;
    mem[8] = 0x25;
    int32_t d2 = (int32_t)((base + 40) - (base + 13));
    memcpy(mem + 9, &d2, 4);
    mem[13] = mem[14] = mem[15] = 0x90;
    *(uint64_t*)(mem + 32) = 0;   // 记录的实     *(uint64_t*)(mem + 40) = 0;   // trampoline
    (void)name;
    return mem;
}


void installCtorCapture(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_ctorCount > 0) {
            logLine("ctor: already installed");
            return;
        }
        for (int i = 0; i < kCtorNum; ++i) {
            void* target = (void*)((uintptr_t)mod + (uintptr_t)kCtorTargets[i].rva);
            void* stub = buildCtorStub(kCtorTargets[i].name);
            if (!stub) continue;
            void* tramp = nullptr;
            if (MH_CreateHook(target, stub, &tramp) != MH_OK) {
                VirtualFree(stub, 0, MEM_RELEASE);
                logLine("ctor: hook failed for %s", kCtorTargets[i].name);
                continue;
            }
            *(uint64_t*)((uint8_t*)stub + 40) = (uint64_t)tramp;
            MH_EnableHook(target);
            g_ctors[g_ctorCount].mem = (uint8_t*)stub;
            g_ctors[g_ctorCount].slot = (volatile uint64_t*)((uint8_t*)stub + 32);
            g_ctors[g_ctorCount].target = target;
            g_ctors[g_ctorCount].name = kCtorTargets[i].name;
            g_ctorCount++;
            logLine("ctor: hooked %s (rva 0x%llX)", kCtorTargets[i].name, kCtorTargets[i].rva);
        }
        logLine("ctor: %d hook(s) installed", g_ctorCount);
    } else {
        for (int i = 0; i < g_ctorCount; ++i) {
            MH_DisableHook(g_ctors[i].target);
            MH_RemoveHook(g_ctors[i].target);
            VirtualFree(g_ctors[i].mem, 0, MEM_RELEASE);
        }
        logLine("ctor: %d hook(s) removed", g_ctorCount);
        g_ctorCount = 0;
    }
}

void dumpCtorCaptures() {
    for (int i = 0; i < g_ctorCount; ++i) {
        const uint64_t v = *g_ctors[i].slot;
        logLine("ctor: %-40s last this = %p", g_ctors[i].name, (void*)v);
    }
}


// ---------------------------------------------------------------- 捕获全部 VCHumanoidMove 实例
//
// .ctor()  IL2CPP 签名 (this, MethodInfo*)，所以可以写一个正常的 C 函数 detour
// 拿到每一个新建实例，存进数组；然后用玩家坐标做匹配，找出属于自己的那个 
static void hkCtorVchm(void* self, void* mi) {
    const long n = InterlockedIncrement(&g_vchmInstCount) - 1;
    // 环形缓冲：切场景会不断创建新实例，不环绕就会写到已释放的对象
    if (n >= 0) g_vchmInst[n % 512] = self;
    // 当场读：对象 + 构造函数写入的常量字段 + 飞行字段
    if (n >= 0 && n < 16) {
        const uint64_t* q = (const uint64_t*)self;
        const uint32_t at294 = *(const uint32_t*)((const uint8_t*)self + 0x294);
        const uint32_t at2A4 = *(const uint32_t*)((const uint8_t*)self + 0x2A4);
        const uint8_t at3BC = *(const uint8_t*)((const uint8_t*)self + 0x3BC);
        logLine("vchm: new#%ld self=%p q0=%p q1=%p q2=%p +294=%08X +2A4=%08X +3BC=%02X",
                (long)n, self, (void*)q[0], (void*)q[1], (void*)q[2], at294, at2A4, at3BC);
    }
    if (g_origVchmCtor) g_origVchmCtor(self, mi);
}

static bool g_vchmHooked = false;

void installVchmCtorCapture() {
    if (g_vchmHooked) {
        logLine("vchm: already hooked");
        return;
    }
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    void* target = (void*)((uintptr_t)mod + 0xAC88970ull);
    if (MH_CreateHook(target, (void*)hkCtorVchm, (void**)&g_origVchmCtor) == MH_OK) {
        MH_EnableHook(target);
        g_vchmHooked = true;
        logLine("vchm: ctor hooked at %p", target);
    } else {
        logLine("vchm: hook failed at %p", target);
    }
}

// 对每个捕获的实例，扫 ±0x4000 找玩家坐标，命中的就是玩家自己的组件
void findPlayerVchm() {
    HANDLE h = GetCurrentProcess();
    FILE* pf = fopen("D:\\Programs\\gs71-runtime\\pos.txt", "r");
    if (!pf) {
        logLine("vchm: pos.txt missing");
        return;
    }
    float px = 0, py = 0, pz = 0;
    if (fscanf(pf, "%f %f %f", &px, &py, &pz) != 3) {
        fclose(pf);
        return;
    }
    fclose(pf);
    const long total = g_vchmInstCount;
    logLine("vchm: %ld instance(s) captured, target pos %.2f %.2f %.2f", total, px, py, pz);

    static uint8_t buf[0x2000];
    int hits = 0;
    for (long i = 0; i < total && i < 512; ++i) {
        const uintptr_t a = (uintptr_t)g_vchmInst[i];
        if (!a) continue;
        // 先验证这是真实对象：q0 必须是类指针，q1 必须 monitor（小值）
        uint64_t q0 = 0, q1 = 0;
        SIZE_T gq = 0;
        if (!ReadProcessMemory(h, (void*)a, &q0, 8, &gq) || q0 != 0x500026f1fe0ull) {
            logLine("vchm: inst[%ld] @%p 不是活对 (q0=%p)", i, (void*)a, (void*)q0);
            continue;
        }
        ReadProcessMemory(h, (void*)(a + 8), &q1, 8, &gq);
        logLine("vchm: inst[%ld] @%p 活对 q1=%p", i, (void*)a, (void*)q1);
        // 只在本对 0x000..0x2000 内找坐标
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)a, buf, sizeof(buf), &got)) continue;
        size_t off = 0;
        if (!posTripleMatch(buf, got, px, py, pz, &off)) continue;
        hits++;
        // 读几个关键字
int flyUp = 0, flyDown = 0, forceFly = 0, superSprint = 0;
        SIZE_T g = 0;
        ReadProcessMemory(h, (void*)(a + 0x396), &flyUp, 1, &g);
        ReadProcessMemory(h, (void*)(a + 0x398), &flyDown, 1, &g);
        ReadProcessMemory(h, (void*)(a + 0x3BC), &forceFly, 1, &g);
        ReadProcessMemory(h, (void*)(a + 0x38E), &superSprint, 1, &g);
        logLine("vchm: MATCH inst[%ld] @%p posAt+0x%zX  tryFlyUp=%d tryFlyDown=%d forceAirStateFly=%d superSprint=%d",
                i, (void*)a, off, flyUp, flyDown, forceFly, superSprint);
    }
    logLine("vchm: %d/%ld matched the player position", hits, total);
}


// ---------------------------------------------------------------- 类对象结构遍
// 目的：找 Il2CppClass  static_fields 的真实偏移
// 做法：把类对 0x00..0x200 全部 qword 打出来；凡是指向堆的，再解引用前 4  qword
DWORD WINAPI classWalkThread(LPVOID) {
    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    const uintptr_t cls = (uintptr_t)0x500026f1fe0ull;   // VCHumanoidMove 类对
FILE* f = fopen("D:\\Programs\\gs71-runtime\\classwalk.txt", "w");
    if (!f) return 0;
    fprintf(f, "# VCHumanoidMove class object @0x%llx\n", (unsigned long long)cls);
    fprintf(f, "# module base = 0x%llx\n\n", (unsigned long long)(uintptr_t)mod);

    for (int off = 0; off < 0x200; off += 8) {
        uint64_t v = 0;
        if (!ReadProcessMemory(h, (void*)(cls + off), &v, 8, nullptr)) continue;
        if (!v) continue;
        const char* kind = "";
        if (v >= (uintptr_t)mod && v < (uintptr_t)mod + 0x1C000000ULL) kind = "MODULE";
        else if (v >= 0x50000000000ULL && v < 0x50020000000ULL) kind = "META";
        else if (v > 0x10000 && v < 0x7fffffffffffULL) kind = "HEAP?";
        fprintf(f, "+0x%03X = 0x%016llx  %s\n", off, (unsigned long long)v, kind);
        if (strcmp(kind, "HEAP?") == 0) {
            uint64_t q[4] = {0};
            SIZE_T got = 0;
            if (ReadProcessMemory(h, (void*)v, q, sizeof(q), &got) && got == sizeof(q)) {
                fprintf(f, "        -> [0x%llx] [0x%llx] [0x%llx] [0x%llx]\n",
                        (unsigned long long)q[0], (unsigned long long)q[1],
                        (unsigned long long)q[2], (unsigned long long)q[3]);
            }
        }
    }
    fclose(f);
    logLine("classwalk: done -> classwalk.txt");
    return 0;
}


// ---------------------------------------------------------------- Transform 写入探针
//
// 目的：找 谁在每帧改角色坐 
// 桩（只记录，不改行为）：
//     mov rax, [rsp]          ; 返回地址 = 调用
//     mov [rip+slot], rax     ; 记下
//     inc qword [rip+ctr]     ; 计数
//     jmp [rip+tramp]         ; 继续执行原函
struct XformProbe {
    uint8_t* mem;
    volatile long long* counter;
    volatile uint64_t* caller;
    void* target;
    const char* name;
};
static XformProbe g_xf[8] = {};
static int g_xfCount = 0;

static const struct {
    const char* name;
    unsigned long long rva;
} kXfTargets[] = {
        {"Transform.set_position(managed)", 0x19C00C10ull},
        {"Transform.set_localPosition(managed)", 0x19BFE1D0ull},
        {"Transform.NativeSetPosition", 0x15C4A80ull},
        {"Transform.NativeWritePos", 0x15CADF0ull},
        {"Transform.NativePosNotify", 0x128B750ull},
};
static const int kXfNum = 5;

static void* buildXformStub() {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    const uintptr_t base = (uintptr_t)mem;
    uint8_t* w = mem;
    // mov rax, [rsp]
    w[0] = 0x48; w[1] = 0x8B; w[2] = 0x04; w[3] = 0x24; w += 4;
    // mov [rip+disp32], rax
    w[0] = 0x48; w[1] = 0x89; w[2] = 0x05;
    int32_t d1 = (int32_t)((base + 48) - (uintptr_t)(w + 7));
    memcpy(w + 3, &d1, 4); w += 7;
    // mov [rip+disp32], rcx
    w[0] = 0x48; w[1] = 0x89; w[2] = 0x0D;
    int32_t d5 = (int32_t)((base + 96 + 5 * 8) - (uintptr_t)(w + 7));
    memcpy(w + 3, &d5, 4); w += 7;
    // mov [rip+disp32], rdx
    w[0] = 0x48; w[1] = 0x89; w[2] = 0x15;
    int32_t d6 = (int32_t)((base + 96 + 6 * 8) - (uintptr_t)(w + 7));
    memcpy(w + 3, &d6, 4); w += 7;
    // inc qword ptr [rip+disp32]
    w[0] = 0x48; w[1] = 0xFF; w[2] = 0x05;
    int32_t d2 = (int32_t)((base + 56) - (uintptr_t)(w + 7));
    memcpy(w + 3, &d2, 4); w += 7;
    // jmp qword ptr [rip+disp32]
    w[0] = 0xFF; w[1] = 0x25;
    int32_t d3 = (int32_t)((base + 64) - (uintptr_t)(w + 6));
    memcpy(w + 2, &d3, 4); w += 6;
    *(uint64_t*)(mem + 48) = 0;   // caller
    *(uint64_t*)(mem + 56) = 0;   // counter
    *(uint64_t*)(mem + 64) = 0;   // trampoline
    return mem;
}

void installTransformProbe(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_xfCount > 0) {
            logLine("xf: already installed");
            return;
        }
        for (int i = 0; i < kXfNum; ++i) {
            void* target = (void*)((uintptr_t)mod + (uintptr_t)kXfTargets[i].rva);
            void* stub = buildXformStub();
            if (!stub) continue;
            void* tramp = nullptr;
            if (MH_CreateHook(target, stub, &tramp) != MH_OK) {
                VirtualFree(stub, 0, MEM_RELEASE);
                logLine("xf: hook failed for %s", kXfTargets[i].name);
                continue;
            }
            *(uint64_t*)((uint8_t*)stub + 64) = (uint64_t)tramp;
            MH_EnableHook(target);
            g_xf[g_xfCount].mem = (uint8_t*)stub;
            g_xf[g_xfCount].counter = (volatile long long*)((uint8_t*)stub + 56);
            g_xf[g_xfCount].caller = (volatile uint64_t*)((uint8_t*)stub + 48);
            g_xf[g_xfCount].target = target;
            g_xf[g_xfCount].name = kXfTargets[i].name;
            g_xfCount++;
            logLine("xf: hooked %s", kXfTargets[i].name);
        }
        logLine("xf: %d hook(s) installed", g_xfCount);
    } else {
        for (int i = 0; i < g_xfCount; ++i) {
            MH_DisableHook(g_xf[i].target);
            MH_RemoveHook(g_xf[i].target);
            VirtualFree(g_xf[i].mem, 0, MEM_RELEASE);
        }
        logLine("xf: %d hook(s) removed", g_xfCount);
        g_xfCount = 0;
    }
}

void dumpTransformProbe() {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    for (int i = 0; i < g_xfCount; ++i) {
        const uint64_t caller = *g_xf[i].caller;
        const long long n = *g_xf[i].counter;
        const uint64_t rva = (mod && caller > (uint64_t)mod) ? caller - (uint64_t)mod : 0;
        logLine("xf: %-28s calls=%-8lld lastCaller=%p (rva 0x%llX)", g_xf[i].name, n,
                (void*)caller, (unsigned long long)rva);
    }
}


// ---------------------------------------------------------------- 锁定玩家 transform
//
// 原生 Transform::SetPosition(self, Vector3*) 每帧被调用两万多次（所有实体）
// 这里用「传入坐 == 玩家当前坐标」来识别玩家自己的那 transform
// 识别到之后把它记下来 — 之后就能直接读写它的权威坐标 
volatile uintptr_t g_playerTransform = 0;
static volatile long g_xfMatchCount = 0;
static volatile uint64_t g_xfMatchCaller = 0;
static volatile long g_xfCalls = 0;
static volatile float g_xfMinD2 = 1e18f;
static volatile float g_xfMinPos[3] = {0, 0, 0};
static volatile uintptr_t g_xfMinSelf = 0;
static volatile long g_xfMinIsLocal = 0;

using XfSetPosFn = void (*)(void* self, void* vec);
static XfSetPosFn g_origXfSetPos = nullptr;
static XfSetPosFn g_origXfSetLocal = nullptr;

// 公共匹配逻辑：传入坐标离玩家当前坐标很近  认为这就是玩家的 transform
static void xfMatchPlayer(void* self, void* vec) {
    if (!vec) return;
    const float* v = (const float*)vec;
    const Snapshot snap = bridge::get();
    const float dx = v[0] - snap.px;
    const float dy = v[1] - snap.py;
    const float dz = v[2] - snap.pz;
    const float d2 = dx * dx + dy * dy + dz * dz;
    g_xfCalls++;
    if (d2 < g_xfMinD2) {
        g_xfMinD2 = d2;
        g_xfMinPos[0] = v[0];
        g_xfMinPos[1] = v[1];
        g_xfMinPos[2] = v[2];
        g_xfMinSelf = (uintptr_t)self;
        g_xfMinIsLocal = 1;
    }
    if (d2 < 9.0f) {
        if (!g_playerTransform) {
            g_playerTransform = (uintptr_t)self;
            g_xfMatchCaller = (uint64_t)__builtin_return_address(0);
            logLine("xfmatch: player transform = %p (from local=%d)", self,
                    (int)g_xfMinIsLocal);
        }
        g_xfMatchCount++;
    }
}

static void hkXfSetLocal(void* self, void* vec) {
    xfMatchPlayer(self, vec);
    if (g_origXfSetLocal) g_origXfSetLocal(self, vec);
}

static void hkXfSetPos(void* self, void* vec) {
    xfMatchPlayer(self, vec);
    if (g_origXfSetPos) g_origXfSetPos(self, vec);
}

void installPlayerTransformHook(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    static bool installed = false;
    if (enable) {
        if (installed) {
            logLine("xft: already installed");
            return;
        }
        void* t1 = (void*)((uintptr_t)mod + 0x15C4A80ull);
        if (MH_CreateHook(t1, (void*)hkXfSetPos, (void**)&g_origXfSetPos) == MH_OK) {
            MH_EnableHook(t1);
            logLine("xft: NativeSetPosition hooked at %p", t1);
        } else {
            logLine("xft: hook failed at %p", t1);
        }
        void* t2 = (void*)((uintptr_t)mod + 0x15C4990ull);
        if (MH_CreateHook(t2, (void*)hkXfSetLocal, (void**)&g_origXfSetLocal) == MH_OK) {
            MH_EnableHook(t2);
            logLine("xft: NativeSetLocalPosition hooked at %p", t2);
        } else {
            logLine("xft: hook failed at %p", t2);
        }
        installed = true;
    } else {
        if (!installed) return;
        MH_DisableHook((void*)((uintptr_t)mod + 0x15C4A80ull));
        MH_RemoveHook((void*)((uintptr_t)mod + 0x15C4A80ull));
        installed = false;
        logLine("xft: hook removed");
    }
}

void reportPlayerTransform() {
    const uintptr_t t = g_playerTransform;
    const Snapshot s2 = bridge::get();
    logLine("xft: calls=%ld matches=%ld playerTransform=%p", (long)g_xfCalls,
            (long)g_xfMatchCount, (void*)t);
    logLine("xft: snapshot pos = %.2f %.2f %.2f", s2.px, s2.py, s2.pz);
    logLine("xft: nearest dist=%.2f m at %.2f %.2f %.2f self=%p", (float)sqrtf(g_xfMinD2),
            g_xfMinPos[0], g_xfMinPos[1], g_xfMinPos[2], (void*)g_xfMinSelf);
    if (!t) return;
    float pos[3] = {0, 0, 0};
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), (void*)(t + 0x48), pos, sizeof(pos), &got);
    logLine("xft:   transform+0x48 = %.2f %.2f %.2f", pos[0], pos[1], pos[2]);
}


// ---------------------------------------------------------------- 扫出玩家 Transform 结构
//
// 依据（从原生 Transform::SetPosition 反汇编读出来的）
//   transform + 0x48 = 世界坐标 (float x,y,z)
//   transform + 0x58 = 旋转四元 (float x,y,z,w)
//   transform + 0x68 = 缩放 (float x,y,z)，通常接近 1,1,1
//
// 做法：全内存 等于玩家坐标
// float 三元 ，再验证它前后符 transform 布局
DWORD WINAPI findPlayerTransform(LPVOID) {
    HANDLE h = GetCurrentProcess();
    const Snapshot snap = bridge::get();
    const float px = snap.px, py = snap.py, pz = snap.pz;
    if (px == 0.f && pz == 0.f) {
        logLine("ft: no player position yet");
        return 0;
    }
    logLine("ft: scanning for %.3f %.3f %.3f", px, py, pz);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int found = 0;

    while (p < limit && found < 40) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool rw = (mbi.State == MEM_COMMIT) &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY) &&
                        !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (rw && size > 0x1000 && size < 0x40000000) {
            for (size_t off = 0; off + 0x80 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 0x80) continue;
                for (size_t i = 0x48; i + 0x30 <= got; i += 4) {
                    float a, b, c;
                    memcpy(&a, buf + i, 4);
                    memcpy(&b, buf + i + 4, 4);
                    memcpy(&c, buf + i + 8, 4);
                    if (!(fabsf(a - px) <= 0.05f)) continue;
                    if (!(fabsf(b - py) <= 0.05f)) continue;
                    if (!(fabsf(c - pz) <= 0.05f)) continue;
                    // 这个位置的前 0x48 字节应当 transform 起点
                    // 检 +0x10 (四元   +0x20 (缩放)
                    float q[4], sc[3];
                    memcpy(q, buf + i + 0x10, 16);
                    memcpy(sc, buf + i + 0x20, 12);
                    const float qn = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                    const bool quatOk = qn > 0.7f && qn < 1.3f && fabsf(q[3]) <= 1.05f;
                    const bool scaleOk = fabsf(sc[0] - 1.f) < 0.05f && fabsf(sc[1] - 1.f) < 0.05f &&
                                         fabsf(sc[2] - 1.f) < 0.05f;
                    const uintptr_t posAddr = (uintptr_t)(base + off + i);
                    const uintptr_t transform = posAddr - 0x48;
                    if (quatOk && scaleOk) {
                        found++;
                        logLine("ft: HIT transform=%p posAddr=%p  q=%.2f %.2f %.2f %.2f  sc=%.2f %.2f %.2f",
                                (void*)transform, (void*)posAddr, q[0], q[1], q[2], q[3], sc[0],
                                sc[1], sc[2]);
                    } else if (found < 40) {
                        logLine("ft: cand  posAddr=%p  quatOk=%d scaleOk=%d qn=%.2f sc=%.2f %.2f %.2f",
                                (void*)posAddr, quatOk ? 1 : 0, scaleOk ? 1 : 0, qn, sc[0], sc[1],
                                sc[2]);
                        found++;
                    }
                    if (found >= 40) break;
                }
                if (found >= 40) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    logLine("ft: done, %d candidate(s)", found);
    return 0;
}


// ----------------------------------------------------------------  开 所 Rigidbody 的碰撞检
//
// 来自 Akebi（开源） NoClip 实现
//     Rigidbody_set_detectCollisions(rigidBody, false, nullptr);
// 这一步就 不让物理系统把角色挡在墙 的关键
// 7.1 地址：Rigidbody.set_detectCollisions = 0x19C5E520
//          UnityEngine.Rigidbody 类的 TypeInfo = 0x500014137b0
using RbdSetDetectFn = void (*)(void* self, bool value, void* mi);
static const unsigned long long kRigidbodyTypeInfo = 0x500014137b0ull;

DWORD WINAPI toggleRigidbodyCollisions(LPVOID param) {
    const bool detect = ((intptr_t)param) != 0;   // true = 恢复碰撞, false = 关闭
    HANDLE h = GetCurrentProcess();
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    auto fn = (RbdSetDetectFn)((uintptr_t)mod + 0x19C5E520ull);

    static uintptr_t inst[2048];
    const int n = collectQwordRefs((uintptr_t)kRigidbodyTypeInfo, inst, 2048, 0);
    logLine("rbd: %d candidate(s), detectCollisions=%d", n, detect ? 1 : 0);

    // 严格校验：q0 == 类指针、q1  monitor、m_CachedPtr(+0x10) 是可读非空指
uintptr_t good[64];
    int ng = 0;
    for (int i = 0; i < n && ng < 64; ++i) {
        const uintptr_t a = inst[i];
        uint64_t q0 = 0, q1 = 0, cached = 0;
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)a, &q0, 8, &got) || q0 != kRigidbodyTypeInfo) continue;
        ReadProcessMemory(h, (void*)(a + 8), &q1, 8, &got);
        ReadProcessMemory(h, (void*)(a + 0x10), &cached, 8, &got);
        if (q1 > 0x10000) continue;
        if (cached < 0x10000 || cached > 0x7fffffffffffULL) continue;
        // 原生对象头几个字节应当可
uint64_t probe = 0;
        if (!ReadProcessMemory(h, (void*)cached, &probe, 8, &got)) continue;
        good[ng++] = a;
    }
    logLine("rbd: %d verified object(s)", ng);
    if (ng == 0) return 0;

    // 先只动第一个，确认 ABI 正确、不
fn((void*)good[0], detect, nullptr);
    logLine("rbd: first call ok (object %p)", (void*)good[0]);
    Sleep(200);
    for (int i = 1; i < ng; ++i) {
        fn((void*)good[i], detect, nullptr);
    }
    logLine("rbd: detectCollisions=%d applied to %d object(s)", detect ? 1 : 0, ng);
    return 0;
}

// ---------------------------------------------------------------- 移动状态机（Akebi  NoClip 核心
//
// 7.1  HumanoidMoveFSM 被改名为 GCOBMFODCLK（从 VCHumanoidMove+0x698 的字段类型查到）
// 它有两个 (float) 方法，对 Akebi hook  LateTick / Tick
//   PHLHJDKNFJF  0x128E8AF0
//   DBHNOIIMLDM  0x128E92A0
//
// Akebi 的做法：noclip 开启时直接 return，不执行原函 — 移动/碰撞/重力全部跳过 
static void hkFsmA(void* self, float dt, void* mi) {
    g_fsmCallsA++;
    if (g_fsmSkip) return;          // 跳过原函 = 不碰
if (g_origFsmA) g_origFsmA(self, dt, mi);
}
static void hkFsmB(void* self, float dt, void* mi) {
    g_fsmCallsB++;
    if (g_fsmSkip) return;
    if (g_origFsmB) g_origFsmB(self, dt, mi);
}

void installFsmHooks(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_fsmHooked) {
            logLine("fsm: already hooked");
            return;
        }
        void* a = (void*)((uintptr_t)mod + 0x128E8AF0ull);
        void* b = (void*)((uintptr_t)mod + 0x128E92A0ull);
        if (MH_CreateHook(a, (void*)hkFsmA, (void**)&g_origFsmA) == MH_OK) {
            MH_EnableHook(a);
            logLine("fsm: hooked PHLHJDKNFJF at %p", a);
        } else {
            logLine("fsm: hook A failed");
        }
        if (MH_CreateHook(b, (void*)hkFsmB, (void**)&g_origFsmB) == MH_OK) {
            MH_EnableHook(b);
            logLine("fsm: hooked DBHNOIIMLDM at %p", b);
        } else {
            logLine("fsm: hook B failed");
        }
        g_fsmHooked = true;
    } else {
        if (!g_fsmHooked) return;
        MH_DisableHook((void*)((uintptr_t)mod + 0x128E8AF0ull));
        MH_RemoveHook((void*)((uintptr_t)mod + 0x128E8AF0ull));
        MH_DisableHook((void*)((uintptr_t)mod + 0x128E92A0ull));
        MH_RemoveHook((void*)((uintptr_t)mod + 0x128E92A0ull));
        g_fsmHooked = false;
        logLine("fsm: hooks removed");
    }
}

void reportFsm() {
    logLine("fsm: A(PHLHJDKNFJF)=%ld calls  B(DBHNOIIMLDM)=%ld calls  skip=%d",
            (long)g_fsmCallsA, (long)g_fsmCallsB, g_fsmSkip ? 1 : 0);
}


// ---------------------------------------------------------------- 对指定地址 set_detectCollisions
// addrs.txt 里每行一个地址（十六进制），对这些对象调用 Rigidbody.set_detectCollisions(value)
DWORD WINAPI rbdFromFileThread(LPVOID param) {
    const bool detect = ((intptr_t)param) != 0;
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    auto fn = (RbdSetDetectFn)((uintptr_t)mod + 0x19C5E520ull);

    FILE* f = fopen("D:\\Programs\\gs71-runtime\\addrs.txt", "r");
    if (!f) {
        logLine("rbdf: addrs.txt missing");
        return 0;
    }
    char line[64];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        const uintptr_t a = (uintptr_t)strtoull(line, nullptr, 16);
        if (!a) continue;
        uint64_t q0 = 0;
        SIZE_T got = 0;
        ReadProcessMemory(GetCurrentProcess(), (void*)a, &q0, 8, &got);
        logLine("rbdf: call on %p (q0=%p)", (void*)a, (void*)q0);
        fn((void*)a, detect, nullptr);
        n++;
        Sleep(150);
    }
    fclose(f);
    logLine("rbdf: detectCollisions=%d on %d object(s)", detect ? 1 : 0, n);
    return 0;
}


// ---------------------------------------------------------------- 捕获 FSM 实例，拿角色 Transform
//
// GCOBMFODCLK  HumanoidMoveFSM 
//   .ctor(MoleMole.VCHumanoidMove)      0x12905880
//   字段 +0x90 UnityEngine.Transform    <- 角色 transform
// Transform 布局（从原生 setter 反汇编确认） +0x48 = 世界坐标
using FsmCtorFn = void (*)(void* self, void* move, void* mi);
static FsmCtorFn g_origFsmCtor = nullptr;
static void* g_fsmInst[64] = {};
static volatile long g_fsmInstCount = 0;
static bool g_fsmCtorHooked = false;

static void hkFsmCtor(void* self, void* move, void* mi) {
    const long n = InterlockedIncrement(&g_fsmInstCount) - 1;
    if (n >= 0 && n < 64) g_fsmInst[n] = self;
    if (g_origFsmCtor) g_origFsmCtor(self, move, mi);
}

void installFsmCtorCapture(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_fsmCtorHooked) {
            logLine("fsmc: already hooked");
            return;
        }
        void* t = (void*)((uintptr_t)mod + 0x12905880ull);
        if (MH_CreateHook(t, (void*)hkFsmCtor, (void**)&g_origFsmCtor) == MH_OK) {
            MH_EnableHook(t);
            g_fsmCtorHooked = true;
            logLine("fsmc: FSM ctor hooked at %p", t);
        } else {
            logLine("fsmc: hook failed at %p", t);
        }
    } else {
        if (!g_fsmCtorHooked) return;
        MH_DisableHook((void*)((uintptr_t)mod + 0x12905880ull));
        MH_RemoveHook((void*)((uintptr_t)mod + 0x12905880ull));
        g_fsmCtorHooked = false;
        logLine("fsmc: hook removed");
    }
}

// 对每个捕获的 FSM 实例，读 +0x90  Transform，再 transform+0x48 的坐
void reportFsmTransforms() {
    HANDLE h = GetCurrentProcess();
    const Snapshot snap = bridge::get();
    const long n = g_fsmInstCount;
    logLine("fsmc: %ld FSM instance(s), player pos %.2f %.2f %.2f", n, snap.px, snap.py, snap.pz);
    static uint8_t buf[0x1000];
    int hits = 0;
    for (long i = 0; i < n && i < 64; ++i) {
        const uintptr_t fsm = (uintptr_t)g_fsmInst[i];
        if (!fsm) continue;
        uint64_t tr = 0;
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)(fsm + 0x90), &tr, 8, &got) || !tr) continue;
        float pos[3] = {0, 0, 0};
        if (!ReadProcessMemory(h, (void*)(tr + 0x48), pos, sizeof(pos), &got)) continue;
        const float dx = pos[0] - snap.px, dy = pos[1] - snap.py, dz = pos[2] - snap.pz;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz);
        logLine("fsmc: [%ld] fsm=%p transform=%p pos=%.2f %.2f %.2f  dist=%.2f", i, (void*)fsm,
                (void*)tr, pos[0], pos[1], pos[2], d);
        if (d < 3.0f) hits++;
    }
    logLine("fsmc: %d near player", hits);
}


// ----------------------------------------------------------------  get_position 反推坐标偏移
//
// 钩住 UnityEngine.Transform::get_position x19C00BD0）
// 每次调用时， this 指针，在它周 ±0x100  等于玩家坐标
// float 三元 
// 命中处相对于 this 的偏移，就是真正的坐标字段偏移（运行时亲口告诉我们）
static volatile long g_gpCalls = 0;
static volatile int g_gpBestOff = -9999;
static volatile uintptr_t g_gpBestThis = 0;
static bool g_gpHooked = false;

using GetPosFn = void (*)(void* retBuf, void* self, void* mi);
static GetPosFn g_origGetPos = nullptr;

// 说明：Vector3 返回值在 IL2CPP x64 上通过调用方提供的缓冲区传回
// 我们不改返回值，只观 this 附近的内存
static void hkGetPosition(void* retBuf, void* self, void* mi) {
    if (g_origGetPos) g_origGetPos(retBuf, self, mi);
    g_gpCalls++;
    if (!self || g_gpCalls > 200000) return;
    const Snapshot snap = bridge::get();
    if (snap.px == 0.f && snap.pz == 0.f) return;
    // 已经找到过就不重复扫，省性能
    if (g_gpBestOff != -9999 && (g_gpCalls % 2000) != 0) return;

    uint8_t buf[0x200];
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)self - 0x80), buf, sizeof(buf),
                           &got) || got < 0x120)
        return;
    float best = 1e9f;
    int bestOff = -9999;
    for (int i = 0; i + 12 <= (int)got; i += 4) {
        float a, b, c;
        memcpy(&a, buf + i, 4);
        memcpy(&b, buf + i + 4, 4);
        memcpy(&c, buf + i + 8, 4);
        const float dx = a - snap.px, dy = b - snap.py, dz = c - snap.pz;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < best) {
            best = d;
            bestOff = i - 0x80;
        }
    }
    if (best < 0.5f && bestOff != g_gpBestOff) {
        g_gpBestOff = bestOff;
        g_gpBestThis = (uintptr_t)self;
        logLine("getpos: transform=%p  坐标偏移=+0x%X  (dist=%.2f)", self, bestOff, best);
    }
}

void installGetPositionHook(bool enable) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    if (enable) {
        if (g_gpHooked) {
            logLine("getpos: already hooked");
            return;
        }
        g_gpCalls = 0;
        g_gpBestOff = -9999;
        void* t = (void*)((uintptr_t)mod + 0x19C00BD0ull);
        if (MH_CreateHook(t, (void*)hkGetPosition, (void**)&g_origGetPos) == MH_OK) {
            MH_EnableHook(t);
            g_gpHooked = true;
            logLine("getpos: Transform.get_position hooked at %p", t);
        } else {
            logLine("getpos: hook failed at %p", t);
        }
    } else {
        if (!g_gpHooked) return;
        MH_DisableHook((void*)((uintptr_t)mod + 0x19C00BD0ull));
        MH_RemoveHook((void*)((uintptr_t)mod + 0x19C00BD0ull));
        g_gpHooked = false;
        logLine("getpos: hook removed");
    }
}

void reportGetPosition() {
    const Snapshot snap = bridge::get();
    logLine("getpos: calls=%ld  bestOff=%d  transform=%p  playerPos=%.2f %.2f %.2f",
            (long)g_gpCalls, g_gpBestOff, (void*)g_gpBestThis, snap.px, snap.py, snap.pz);
}


// ---------------------------------------------------------------- 每帧把刚体速度清零（防掉落
//
// Akebi：Rigidbody_set_velocity(rigidBody, zero)
// 7.1：UnityEngine.Rigidbody::set_velocity = 0x19C5E130
// Vector3 参数 IL2CPP 惯例用指针传递（rdx = &vec）
using RbdSetVelFn = void (*)(void* self, void* vec, void* mi);

DWORD WINAPI zeroVelocityThread(LPVOID) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    auto fn = (RbdSetVelFn)((uintptr_t)mod + 0x19C5E130ull);
    float zero[3] = {0.f, 0.f, 0.f};

    //  addrs.txt 读刚体指
uintptr_t rbs[32];
    int n = 0;
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\addrs.txt", "r");
    if (f) {
        char line[64];
        while (n < 32 && fgets(line, sizeof(line), f)) {
            const uintptr_t a = (uintptr_t)strtoull(line, nullptr, 16);
            if (a) rbs[n++] = a;
        }
        fclose(f);
    }
    logLine("vel0: %d rigidbody target(s), loop start", n);
    if (n == 0) return 0;

    int calls = 0;
    while (g_vel0On) {
        for (int i = 0; i < n; ++i) {
            fn((void*)rbs[i], zero, nullptr);
            calls++;
        }
        Sleep(16);
    }
    logLine("vel0: loop stopped (%d calls)", calls);
    return 0;
}


// ---------------------------------------------------------------- FSM 字段扫描 / 写入（软穿墙
//
// GCOBMFODCLK（HumanoidMoveFSM）里有一 Int32 字段，其中一个很可能就是
// Akebi 用的 _layerMaskScene。做法：
//   fsmfields.now   打印所 Int32 候选字段的值（每个 FSM 实例
//   fsmmask.txt     每行 "偏移  （十六进制偏移、十进制值）
//   fsmmask.now     按文件把这些值写进每 FSM 实例
static const int kFsmIntOffs[] = {0x190, 0x1dc, 0x1ec, 0x208, 0x20c, 0x22c, 0x24c, 0x264, 0x2ac};
static const int kFsmIntNum = 9;

DWORD WINAPI fsmFieldsThread(LPVOID) {
    HANDLE h = GetCurrentProcess();
    const long n = g_fsmInstCount;
    logLine("fsmf: %ld FSM instance(s)", n);
    for (long i = 0; i < n && i < 8; ++i) {
        const uintptr_t fsm = (uintptr_t)g_fsmInst[i];
        if (!fsm) continue;
        char buf[512] = {0};
        int len = 0;
        for (int k = 0; k < kFsmIntNum; ++k) {
            int32_t v = 0;
            if (ReadProcessMemory(h, (void*)(fsm + kFsmIntOffs[k]), &v, 4, nullptr)) {
                len += snprintf(buf + len, sizeof(buf) - len, "%s+0x%X=%d", k ? " " : "",
                                kFsmIntOffs[k], v);
            }
        }
        logLine("fsmf: [%ld] %p : %s", i, (void*)fsm, buf);
    }
    return 0;
}

DWORD WINAPI fsmMaskThread(LPVOID) {
    HANDLE h = GetCurrentProcess();
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\fsmmask.txt", "r");
    if (!f) {
        logLine("fsmm: fsmmask.txt missing");
        return 0;
    }
    int offs[16] = {0};
    int vals[16] = {0};
    int cnt = 0;
    char line[64];
    while (cnt < 16 && fgets(line, sizeof(line), f)) {
        unsigned off = 0;
        int val = 0;
        if (sscanf(line, "%x %d", &off, &val) == 2) {
            offs[cnt] = (int)off;
            vals[cnt] = val;
            cnt++;
        }
    }
    fclose(f);
    if (cnt == 0) {
        logLine("fsmm: no entries");
        return 0;
    }
    const long n = g_fsmInstCount;
    logLine("fsmm: applying %d field(s) to %ld FSM(s)", cnt, n);
    int ok = 0;
    for (long i = 0; i < n && i < 64; ++i) {
        const uintptr_t fsm = (uintptr_t)g_fsmInst[i];
        if (!fsm) continue;
        for (int k = 0; k < cnt; ++k) {
            int32_t v = vals[k];
            if (WriteProcessMemory(h, (void*)(fsm + offs[k]), &v, 4, nullptr)) ok++;
        }
    }
    logLine("fsmm: %d write(s) done", ok);
    return 0;
}

// 原子 noclip：一次触发内按正确顺序做完，中间不留空档
DWORD WINAPI noclipAtomicThread(LPVOID) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return 0;
    logLine("nc: atomic start");

    //  先把速度清零的循环打开（防止后面掉下去
if (!g_vel0On) {
        g_vel0On = true;
        CreateThread(nullptr, 0, zeroVelocityThread, nullptr, 0, nullptr);
        logLine("nc: vel0 ON");
    }
    Sleep(120);

    //  关碰
auto setDetect = (RbdSetDetectFn)((uintptr_t)mod + 0x19C5E520ull);
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\addrs.txt", "r");
    if (f) {
        char line[64];
        int n = 0;
        while (fgets(line, sizeof(line), f)) {
            const uintptr_t a = (uintptr_t)strtoull(line, nullptr, 16);
            if (!a) continue;
            setDetect((void*)a, false, nullptr);
            n++;
        }
        fclose(f);
        logLine("nc: collisions off on %d body(ies)", n);
    }
    Sleep(120);

    //  最后才跳过状态机
    g_fsmSkip = true;
    logLine("nc: FSM skip ON (atomic done)");
    return 0;
}


// ---------------------------------------------------------------- FSM 全字段扫描（找碰撞层掩码
//  FSM 实例 0x000~0x400 的每 int32，打印所 像位掩码"的值（1..0x1000  2 的幂，或 0）
DWORD WINAPI fsmMaskScanThread(LPVOID) {
    HANDLE h = GetCurrentProcess();
    const long n = g_fsmInstCount;
    logLine("fsmscan: %ld FSM instance(s)", n);
    for (long i = 0; i < n && i < 4; ++i) {
        const uintptr_t fsm = (uintptr_t)g_fsmInst[i];
        if (!fsm) continue;
        char buf[1024] = {0};
        int len = 0;
        for (int off = 0; off < 0x400; off += 4) {
            int32_t v = 0;
            if (!ReadProcessMemory(h, (void*)(fsm + off), &v, 4, nullptr)) continue;
            // 只报 0..0x1000 范围内的值（位掩 层号
if (v < 0 || v > 0x1000) continue;
            if (v == 0) continue;
            if (len < (int)sizeof(buf) - 32) {
                len += snprintf(buf + len, sizeof(buf) - len, "+0x%X=%d ", off, v);
            }
        }
        logLine("fsmscan: [%ld] %p : %s", i, (void*)fsm, buf);
    }
    return 0;
}


// ---------------------------------------------------------------- 反查 Transform 内的坐标偏移
//
// FSM 实例（GCOBMFODCLK）在 +0x90 处有 UnityEngine.Transform
// 对这 transform  0x000~0x400 逐个 4 字节偏移扫，找哪里的
// float 三元组等于玩家坐标
// 命中偏移 = 真实坐标字段偏移
DWORD WINAPI findTransformOffsetThread(LPVOID) {
    HANDLE h = GetCurrentProcess();
    const Snapshot snap = bridge::get();
    if (snap.px == 0.f && snap.pz == 0.f) {
        logLine("tfind: no player pos");
        return 0;
    }
    logLine("tfind: player %.2f %.2f %.2f, %ld FSM(s)", snap.px, snap.py, snap.pz,
            (long)g_fsmInstCount);

    static uint8_t buf[0x400];
    int reported = 0;
    for (long i = 0; i < g_fsmInstCount && i < 8; ++i) {
        const uintptr_t fsm = (uintptr_t)g_fsmInst[i];
        if (!fsm) continue;
        uint64_t tr = 0;
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (void*)(fsm + 0x90), &tr, 8, &got) || !tr) continue;
        if (!ReadProcessMemory(h, (void*)tr, buf, sizeof(buf), &got) || got < 16) continue;
        for (int off = 0; off + 12 <= (int)got; off += 4) {
            float a, b, c;
            memcpy(&a, buf + off, 4);
            memcpy(&b, buf + off + 4, 4);
            memcpy(&c, buf + off + 8, 4);
            if (!(fabsf(a - snap.px) <= 0.2f)) continue;
            if (!(fabsf(b - snap.py) <= 0.5f)) continue;
            if (!(fabsf(c - snap.pz) <= 0.2f)) continue;
            logLine("tfind: [%ld] fsm=%p transform=%p  坐标偏移 = +0x%X  (%.2f %.2f %.2f)", i,
                    (void*)fsm, (void*)tr, off, a, b, c);
            reported++;
            break;
        }
        // 顺便打印 transform  0x80 的浮点，便于人工核对
        char fb[512] = {0};
        int len = 0;
        for (int off = 0; off < 0x80 && len < (int)sizeof(fb) - 24; off += 16) {
            float q[4];
            memcpy(q, buf + off, 16);
            len += snprintf(fb + len, sizeof(fb) - len, "f%02X:%.2f %.2f %.2f %.2f | ", off, q[0],
                            q[1], q[2], q[3]);
        }
        logLine("tfind: [%ld] %s", i, fb);
    }
    logLine("tfind: %d transform(s) matched", reported);
    return 0;
}


// ---------------------------------------------------------------- 合成按键（让程序自己 W
//
// key_w.now           按住 W 2 
// key_space.now       按一下空
// 用途：全自动实验台里，不需要人肉操作角
static void tapScancode(WORD sc, DWORD holdMs) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan = sc;
    in[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(holdMs);
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wScan = sc;
    in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &in[1], sizeof(INPUT));
}

DWORD WINAPI holdKeyThread(LPVOID param) {
    const intptr_t which = (intptr_t)param;
    if (which == 1) {
        logLine("key: hold W 2000ms");
        tapScancode(0x11, 2000);   // W
    } else if (which == 2) {
        logLine("key: tap Space");
        tapScancode(0x39, 80);     // Space
    } else if (which == 3) {
        logLine("key: hold Shift 600ms");
        tapScancode(0x2A, 600);    // Left Shift
    }
    logLine("key: done");
    return 0;
}


// ---------------------------------------------------------------- 反作弊函数调用者追
//
// Mmoron.dll+0x13A750 是反作弊校验位置的函数（入口处检 NaN/Inf）
// 在它入口装桩，记 [rsp] = 调用者返回地址  反推游戏 谁在读坐 
static uint8_t* g_mmStub = nullptr;
static volatile uint64_t* g_mmCaller = nullptr;
static volatile long long* g_mmCount = nullptr;
static bool g_mmHooked = false;

void installMmoronProbe(bool enable) {
    if (enable) {
        if (g_mmHooked) {
            logLine("mm: already hooked");
            return;
        }
        HMODULE m = GetModuleHandleA("Mmoron.dll");
        if (!m) {
            logLine("mm: Mmoron.dll not loaded");
            return;
        }
        void* target = (void*)((uintptr_t)m + 0x13A750ull);
        uint8_t* stub = (uint8_t*)buildXformStub();
        if (!stub) return;
        void* tramp = nullptr;
        if (MH_CreateHook(target, stub, &tramp) != MH_OK) {
            VirtualFree(stub, 0, MEM_RELEASE);
            logLine("mm: hook failed at %p", target);
            return;
        }
        *(uint64_t*)(stub + 64) = (uint64_t)tramp;
        MH_EnableHook(target);
        g_mmStub = stub;
        g_mmCaller = (volatile uint64_t*)(stub + 48);
        g_mmCount = (volatile long long*)(stub + 56);
        g_mmHooked = true;
        logLine("mm: Mmoron+0x13A750 hooked at %p (Mmoron base=%p)", target, (void*)m);
    } else {
        HMODULE m = GetModuleHandleA("Mmoron.dll");
        if (!m || !g_mmHooked) return;
        MH_DisableHook((void*)((uintptr_t)m + 0x13A750ull));
        MH_RemoveHook((void*)((uintptr_t)m + 0x13A750ull));
        VirtualFree(g_mmStub, 0, MEM_RELEASE);
        g_mmHooked = false;
        logLine("mm: hook removed");
    }
}

void reportMmoron() {
    if (!g_mmHooked) {
        logLine("mm: not hooked");
        return;
    }
    const uint64_t caller = *g_mmCaller;
    const long long n = *g_mmCount;
    HMODULE exe = GetModuleHandleA(nullptr);
    uint64_t rva = 0;
    if (exe && caller > (uint64_t)exe) rva = caller - (uint64_t)exe;
    logLine("mm: calls=%lld  lastCaller=%p  (GenshinImpact.exe+0x%llX)", n, (void*)caller,
            (unsigned long long)rva);
}


// ---------------------------------------------------------------- 多槽位调用栈捕获
// 桩记 rsp+0/8/16/24/32 处的返回地址，报告时逐个归属到模 + RVA
static uint8_t* g_spStub = nullptr;
static volatile long long* g_spCount = nullptr;
static bool g_spHooked = false;

static void* buildStackStub() {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;
    const uintptr_t base = (uintptr_t)mem;
    uint8_t* w = mem;
    for (int i = 0; i < 5; ++i) {
        // mov rax, [rsp + i*8]
        w[0] = 0x48; w[1] = 0x8B; w[2] = 0x44; w[3] = 0x24; w[4] = (uint8_t)(i * 8);
        // mov [rip+disp32], rax
        w[5] = 0x48; w[6] = 0x89; w[7] = 0x05;
        int32_t d = (int32_t)((base + 96 + i * 8) - (uintptr_t)(w + 12));
        memcpy(w + 8, &d, 4);
        w += 12;
    }
    // inc qword ptr [rip+disp32]
    w[0] = 0x48; w[1] = 0xFF; w[2] = 0x05;
    int32_t dc = (int32_t)((base + 168) - (uintptr_t)(w + 7));
    memcpy(w + 3, &dc, 4);
    w += 7;
    // jmp qword ptr [rip+disp32]
    w[0] = 0xFF; w[1] = 0x25;
    int32_t dj = (int32_t)((base + 176) - (uintptr_t)(w + 6));
    memcpy(w + 2, &dj, 4);
    // 数据区：72..111 = 5  caller 槽；112 = counter 20 = trampoline
    return mem;
}

void installStackProbe(bool enable) {
    if (enable) {
        if (g_spHooked) {
            logLine("sp: already hooked");
            return;
        }
        HMODULE m = GetModuleHandleA("Mmoron.dll");
        if (!m) {
            logLine("sp: Mmoron not loaded");
            return;
        }
        void* target = (void*)((uintptr_t)m + 0x13A750ull);
        void* stub = buildStackStub();
        if (!stub) return;
        void* tramp = nullptr;
        if (MH_CreateHook(target, stub, &tramp) != MH_OK) {
            VirtualFree(stub, 0, MEM_RELEASE);
            logLine("sp: hook failed");
            return;
        }
        *(uint64_t*)((uint8_t*)stub + 176) = (uint64_t)tramp;
        MH_EnableHook(target);
        g_spStub = (uint8_t*)stub;
        g_spCount = (volatile long long*)((uint8_t*)stub + 168);
        g_spHooked = true;
        logLine("sp: hooked Mmoron+0x13A750, capturing 5 stack slots");
    } else {
        HMODULE m = GetModuleHandleA("Mmoron.dll");
        if (!m || !g_spHooked) return;
        MH_DisableHook((void*)((uintptr_t)m + 0x13A750ull));
        MH_RemoveHook((void*)((uintptr_t)m + 0x13A750ull));
        VirtualFree(g_spStub, 0, MEM_RELEASE);
        g_spHooked = false;
        logLine("sp: removed");
    }
}

static void describeAddr(const char* tag, uint64_t a) {
    HMODULE mods[256];
    DWORD need = 0;
    HANDLE h = GetCurrentProcess();
    HMODULE exe = GetModuleHandleA(nullptr);
    HMODULE mm = GetModuleHandleA("Mmoron.dll");
    const char* name = "?";
    uint64_t base = 0;
    if (mm && a >= (uint64_t)mm && a < (uint64_t)mm + 0x400000) {
        name = "Mmoron.dll";
        base = (uint64_t)mm;
    } else if (exe && a >= (uint64_t)exe && a < (uint64_t)exe + 0x1C000000ULL) {
        name = "GenshinImpact.exe";
        base = (uint64_t)exe;
    } else {
        // 逐个模块
if (EnumProcessModules(h, mods, sizeof(mods), &need)) {
            const int n = need / sizeof(HMODULE);
            for (int i = 0; i < n; ++i) {
                MODULEINFO mi{};
                if (GetModuleInformation(h, mods[i], &mi, sizeof(mi)) && a >= (uint64_t)mi.lpBaseOfDll &&
                    a < (uint64_t)mi.lpBaseOfDll + mi.SizeOfImage) {
                    char nm[MAX_PATH] = {0};
                    GetModuleBaseNameA(h, mods[i], nm, MAX_PATH);
                    name = nm;
                    base = (uint64_t)mi.lpBaseOfDll;
                    break;
                }
            }
        }
    }
    logLine("sp: %-4s %p  %s+0x%llX", tag, (void*)a, name, (unsigned long long)(base ? a - base : 0));
}

void reportStackProbe() {
    if (!g_spHooked) {
        logLine("sp: not hooked");
        return;
    }
    logLine("sp: calls=%lld", (long long)*g_spCount);
    for (int i = 0; i < 7; ++i) {
        const uint64_t a = *(volatile uint64_t*)(g_spStub + 96 + i * 8);
        char tag[8];
        snprintf(tag, sizeof(tag), "#%d", i);
        describeAddr(tag, a);
    }
}


// ----------------------------------------------------------------  BaseEntity.Position=0x38c 反查实体
//
// 7.0 偏移表给 BaseEntity.Position = 0x38c（与 7.1  VCHumanoidMove 布局一致，可信）
// 做法：全内存 "等于玩家坐标
// float 三元 ，然后检 addr-0x38c 处是不是一个托管对象头
//       （q0 落在 0x50000000000  = 类指针）。这样的命中几乎一定是玩家 BaseEntity
DWORD WINAPI findEntityByPosThread(LPVOID) {
    HANDLE h = GetCurrentProcess();
    const Snapshot snap = bridge::get();
    if (snap.px == 0.f && snap.pz == 0.f) {
        logLine("ent: no player pos");
        return 0;
    }
    logLine("ent: player %.2f %.2f %.2f, 反查 +0x38c 实体", snap.px, snap.py, snap.pz);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* limit = (uint8_t*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static uint8_t buf[1 << 20];
    int hits = 0;

    while (p < limit && hits < 20) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        const bool rw = (mbi.State == MEM_COMMIT) &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
                         mbi.Protect == PAGE_WRITECOPY) &&
                        !(mbi.Protect & PAGE_GUARD);
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;
        if (rw && size > 0x1000 && size < 0x40000000) {
            for (size_t off = 0; off + 0x20 <= size; off += sizeof(buf)) {
                size_t chunk = size - off;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                SIZE_T got = 0;
                if (!ReadProcessMemory(h, base + off, buf, chunk, &got) || got < 0x20) continue;
                for (size_t i = 0x38c; i + 12 <= got; i += 4) {
                    float a, b, c;
                    memcpy(&a, buf + i, 4);
                    memcpy(&b, buf + i + 4, 4);
                    memcpy(&c, buf + i + 8, 4);
                    if (!(fabsf(a - snap.px) <= 0.05f)) continue;
                    if (!(fabsf(b - snap.py) <= 0.05f)) continue;
                    if (!(fabsf(c - snap.pz) <= 0.05f)) continue;
                    const uintptr_t posAddr = (uintptr_t)(base + off + i);
                    const uintptr_t entity = posAddr - 0x38c;
                    uint64_t q0 = 0, q1 = 0;
                    SIZE_T g2 = 0;
                    if (!ReadProcessMemory(h, (void*)entity, &q0, 8, &g2)) continue;
                    ReadProcessMemory(h, (void*)(entity + 8), &q1, 8, &g2);
                    const bool classOk = q0 >= 0x50000000000ULL && q0 < 0x50040000000ULL;
                    if (!classOk) continue;
                    hits++;
                    logLine("ent: HIT entity=%p  posAddr=%p  klass=%p  monitor=%p  pos=%.2f %.2f %.2f",
                            (void*)entity, (void*)posAddr, (void*)q0, (void*)q1, a, b, c);
                    if (hits >= 20) break;
                }
                if (hits >= 20) break;
            }
        }
        uint8_t* next = base + size;
        if (next <= p) break;
        p = next;
    }
    logLine("ent: done, %d candidate entity(ies)", hits);
    return 0;
}


// ---------------------------------------------------------------- 调用游戏自身的角色接
//
// 7.1 dump 提供的地址
//   MoleMole.ActorUtils::GetAvatarPos()                    0x13AAF060  -> Vector3
//   MoleMole.ActorUtils::AvatarMoveTo(Vector3,f,b,f)       0x13AA8850
//   MoleMole.ActorUtils::GetCameraPos()                    0x13AB7B80  -> Vector3
//
// IL2CPP x64 返回 12 字节结构体时，调用方 rcx 传返回缓冲区指针，rdx  MethodInfo*
using GetVec3Fn = void (*)(void* retBuf, void* mi);
using MoveToFn = void (*)(void* posPtr, float len, void* changeState, void* isWalk, void* mi);

static void callGetAvatarPos_impl() {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    auto fn = (GetVec3Fn)((uintptr_t)mod + 0x13AAF060ull);
    float out[4] = {-999, -999, -999, -999};
    fn(out, nullptr);
    const Snapshot snap = bridge::get();
    const float dx = out[0] - snap.px, dy = out[1] - snap.py, dz = out[2] - snap.pz;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    logLine("api: GetAvatarPos -> %.2f %.2f %.2f   (服务 %.2f %.2f %.2f,  %.2f m)", out[0],
            out[1], out[2], snap.px, snap.py, snap.pz, d);
}

static void callGetCameraPos_impl() {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    auto fn = (GetVec3Fn)((uintptr_t)mod + 0x13AB7B80ull);
    float out[4] = {-999, -999, -999, -999};
    fn(out, nullptr);
    logLine("api: GetCameraPos -> %.2f %.2f %.2f", out[0], out[1], out[2]);
}

// 用游戏自己的 AvatarMoveTo 把角色送到指定
static void callAvatarMoveTo(float x, float y, float z, float len) {
    uint8_t* mod = (uint8_t*)GetModuleHandleA(nullptr);
    if (!mod) return;
    auto fn = (MoveToFn)((uintptr_t)mod + 0x13AA8850ull);
    float pos[3] = {x, y, z};
    fn(pos, len, (void*)0, (void*)0, nullptr);
    logLine("api: AvatarMoveTo(%.2f %.2f %.2f, len=%.1f) called", x, y, z, len);
}

DWORD WINAPI apiProbeThread(LPVOID param) {
    const int which = (int)(intptr_t)param;
    if (which == 0) {
        callGetAvatarPos_impl();
        callGetCameraPos_impl();
    } else if (which == 1) {
        const Snapshot snap = bridge::get();
        callAvatarMoveTo(snap.px + 5.f, snap.py, snap.pz, 5.f);
    }
    return 0;
}

void autoProbeTypesStart() { CreateThread(nullptr, 0, autoProbeTypesThread, nullptr, 0, nullptr); }

void Gs71CbFreeToggle(bool on) { cbFreeToggle(on); }
bool Gs71CbFreeEnabled() { return cbFreeEnabled(); }
void Gs71CbFreeNudge(int a, float d) { cbFreeNudge(a, d); }
float Gs71CbFreeGet(int i) { return cbFreeGet(i); }
unsigned long long Gs71CbFreeHits() { return cbFreeHits(); }
unsigned long long Gs71CbFreePatched() { return cbFreePatched(); }

unsigned long long Gs71CbMapStat(int which) { return cbMapStat(which); }

// ---------------------------------------------------------------- 第二批对外接口
// 实现在匿名 namespace 里（内部链接），这里做一层全局包装给 menu.cpp 用。
bool Gs71Project(float x, float y, float z, float* sx, float* sy, float vw, float vh) {
    return projImpl(x, y, z, sx, sy, vw, vh);
}

// 跳过过场：hook CriwareMediaPlayer::Update (0x12DA0D90)，播放中就调 Skip (0x12DA06F0)
using MediaUpdateFn = void (*)(void* self, void* mi);
static MediaUpdateFn g_origMediaUpdate = nullptr;
static void hkMediaUpdate(void* self, void* mi) {
    if (g_cutsceneSkip && self) {
        using SkipFn = void (*)(void* self, void* mi);
        ((SkipFn)(overlayMod() + 0x12DA06F0ull))(self, nullptr);
    }
    if (g_origMediaUpdate) g_origMediaUpdate(self, mi);
}

void installCutsceneSkip(bool enable) {
    const uintptr_t mod = overlayMod();
    if (!mod) return;
    void* t = (void*)(mod + 0x12DA0D90ull);
    if (enable) {
        if (MH_CreateHook(t, (void*)hkMediaUpdate, (void**)&g_origMediaUpdate) == MH_OK) {
            MH_EnableHook(t);
            logLine("media: CriwareMediaPlayer.Update hooked at %p (skip enabled)", t);
        } else {
            logLine("media: hook Update failed");
        }
    } else {
        MH_DisableHook(t);
        MH_RemoveHook(t);
        g_origMediaUpdate = nullptr;
        logLine("media: hook removed");
    }
}

void Gs71CutsceneSkip(bool on) {
    g_cutsceneSkip = on ? 1 : 0;
    installCutsceneSkip(on ? true : false);
}
bool Gs71CutsceneSkipGet() { return g_cutsceneSkip != 0; }

bool Gs71Bearing(float x, float y, float z, float* outDeg, float* outDist) { return bearingImpl(x, y, z, outDeg, outDist); }
void Gs71ElemView(bool on) { g_elBtnVal = on ? 1 : 0; g_elBtnReq = 1; logLine("elbtn: request %d", on ? 1 : 0); }
bool Gs71ElemViewGet() { return g_elBtnVal != 0; }
int Gs71FlyModeGet() { return (int)g_flyMode; }
void Gs71Log(const char* m) { logLine("hk: %s", m); }
bool Gs71Focused() { return gs71InputFocused(); }
void Gs71FlyMode(int m) { g_flyMode = (m >= 0 && m <= 9) ? m : 0; logLine("fly: mode -> %d", (int)g_flyMode); }
void Gs71FlySet(bool on) { flySetImpl(on); }
bool Gs71FlyGet() { return flyGetImpl(); }
void Gs71FlySpeedSet(float v) { flySpeedSetImpl(v); }
float Gs71FlySpeedGet() { return flySpeedGetImpl(); }
void Gs71HideUi(bool on) { hideUiImpl(on); }
bool Gs71HideUiGet() { return hideUiGetImpl(); }

