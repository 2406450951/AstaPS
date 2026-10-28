// bridge.cpp - tiny WinHTTP client for the local CheatMenu API (127.0.0.1:8890).
#include "bridge.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <deque>
#include <utility>

#pragma comment(lib, "winhttp.lib")

namespace {

std::mutex g_mu;
Snapshot g_snap;
std::atomic<bool> g_run{false};
std::atomic<bool> g_connected{false};
std::atomic<long long> g_lastOk{0};
std::mutex g_cmdMu;
// Pending outgoing calls. Sending used to spawn one detached thread per click; those threads kept
// running while the module unloaded, which is a classic crash-at-exit source. The poll thread now
// drains this queue instead.
std::deque<std::pair<std::string, std::string>> g_pending;

constexpr wchar_t kHost[] = L"127.0.0.1";
constexpr INTERNET_PORT kPort = 8890;

bool httpRequest(const wchar_t* verb, const wchar_t* path, const std::string& body,
                 std::string& out) {
    HINTERNET session = WinHttpOpen(L"Gs71Menu/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, kHost, kPort, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, verb, path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (req) {
            DWORD timeout = 1500;
            WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            WinHttpSetOption(req, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

            const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                  : L"Content-Type: text/plain";
            BOOL sent = WinHttpSendRequest(
                req, headers, headers ? (DWORD)-1 : 0,
                body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                (DWORD)body.size(), (DWORD)body.size(), 0);

            if (sent && WinHttpReceiveResponse(req, nullptr)) {
                out.clear();
                char buf[8192];
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    DWORD read = 0;
                    DWORD want = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
                    if (!WinHttpReadData(req, buf, want, &read) || read == 0) break;
                    out.append(buf, read);
                    if (out.size() > (1 << 20)) break;
                }
                ok = true;
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

// "pos 1.2 3.4 5.6" -> three floats
int readFloats(const char* p, float* dst, int max) {
    int n = 0;
    while (n < max && p && *p) {
        char* end = nullptr;
        float v = strtof(p, &end);
        if (end == p) break;
        dst[n++] = v;
        p = end;
        while (*p == ' ') ++p;
    }
    return n;
}

bool parseBool(const std::string& s) { return s == "1" || s == "true" || s == "on"; }

Snapshot parse(const std::string& text) {
    Snapshot s;
    s.valid = true;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const char* p = line.c_str();
        if (line.rfind("up ", 0) == 0) {
            s.serverUptimeMs = atoll(p + 3);
        } else if (line.rfind("online ", 0) == 0) {
            s.players = atoi(p + 7);
            s.online = s.players > 0;
        } else if (line.rfind("uid ", 0) == 0) {
            s.uid = atoi(p + 4);
        } else if (line.rfind("nick ", 0) == 0) {
            s.nick = p + 5;
            for (auto& c : s.nick)
                if (c == '_') c = ' ';
        } else if (line.rfind("scene ", 0) == 0) {
            s.scene = atoi(p + 6);
        } else if (line.rfind("pos ", 0) == 0) {
            float f[3] = {0, 0, 0};
            readFloats(p + 4, f, 3);
            s.px = f[0]; s.py = f[1]; s.pz = f[2];
        } else if (line.rfind("rot ", 0) == 0) {
            float f[3] = {0, 0, 0};
            readFloats(p + 4, f, 3);
            s.rx = f[0]; s.ry = f[1]; s.rz = f[2];
        } else if (line.rfind("hp ", 0) == 0) {
            float f[2] = {0, 0};
            readFloats(p + 3, f, 2);
            s.hp = f[0]; s.maxHp = f[1];
        } else if (line.rfind("world ", 0) == 0) {
            s.worldHours = atoi(p + 6);
        } else if (line.rfind("ents ", 0) == 0) {
            int hint = atoi(p + 5);
            if (hint > 0 && hint < 8192) s.ents.reserve((size_t)hint);   // never trust the count blindly
        } else if (line.rfind("e ", 0) == 0) {
            EntInfo e;
            char kind = 'E';
            int id = 0, tid = 0;
            float x = 0, y = 0, z = 0, hp = 0, mhp = 0, dist = 0;
            int lvl = 0;
            // NOTE: sscanf_s requires an explicit buffer size for %c; without it the CRT reads a
            // garbage size from the varargs and trashes the heap. Plain sscanf has no such rule.
            if (sscanf(line.c_str(), "e %1c %d %d %f %f %f %f %f %d %f", &kind, &id, &tid,
                       &x, &y, &z, &hp, &mhp, &lvl, &dist) >= 10) {
                e.kind = kind; e.id = id; e.typeId = tid;
                e.x = x; e.y = y; e.z = z; e.hp = hp; e.maxHp = mhp;
                e.level = lvl; e.dist = dist;
                s.ents.push_back(e);
            }
        } else if (line.rfind("st ", 0) == 0) {
            std::istringstream kv(line.substr(3));
            std::string tok;
            while (kv >> tok) {
                auto eq = tok.find('=');
                if (eq == std::string::npos) continue;
                std::string k = tok.substr(0, eq);
                std::string v = tok.substr(eq + 1);
                if (k == "god") s.st.god = parseBool(v);
                else if (k == "fall") s.st.fall = parseBool(v);
                else if (k == "loot") s.st.loot = parseBool(v);
                else if (k == "stamina") s.st.stamina = parseBool(v);
                else if (k == "autoheal") s.st.autoheal = parseBool(v);
                else if (k == "aura") s.st.aura = parseBool(v);
                else if (k == "freeze") s.st.freeze = parseBool(v);
                else if (k == "esp") s.st.esp = parseBool(v);
                else if (k == "mult") s.st.mult = (float)atof(v.c_str());
                else if (k == "aurar") s.st.auraR = (float)atof(v.c_str());
                else if (k == "auradmg") s.st.auraDmg = (float)atof(v.c_str());
                else if (k == "lootr") s.st.lootR = (float)atof(v.c_str());
                else if (k == "timeh") s.st.timeH = atoi(v.c_str());
                else if (k == "timem") s.st.timeM = atoi(v.c_str());
                else if (k == "weather") s.st.weather = atoi(v.c_str());
            }
        } else if (line.rfind("act ", 0) == 0) {
            s.lastAction = line.substr(4);
        }
    }
    return s;
}

void sendLoop(std::string verb_and_path, std::string query);  // defined below

void pollLoop() {
    while (g_run.load()) {
        std::string body;
        if (httpRequest(L"GET", L"/snap", std::string(), body) && !body.empty()) {
            Snapshot s = parse(body);
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_snap = std::move(s);
            }
            g_connected.store(true);
            g_lastOk.store((long long)GetTickCount64());
        } else {
            g_connected.store(false);
        }

        // drain queued /set and /cmd calls on this thread
        for (;;) {
            std::pair<std::string, std::string> item;
            {
                std::lock_guard<std::mutex> lock(g_cmdMu);
                if (g_pending.empty()) break;
                item = std::move(g_pending.front());
                g_pending.pop_front();
            }
            sendLoop(item.first, item.second);
        }

        for (int i = 0; i < 8 && g_run.load(); ++i) Sleep(12);
    }
}

void sendLoop(std::string verb_and_path, std::string query) {
    std::wstring wpath(verb_and_path.begin(), verb_and_path.end());
    std::string out;
    httpRequest(L"POST", wpath.c_str(), query, out);
}

std::thread g_thread;

}  // namespace

namespace bridge {

void start() {
    if (g_run.exchange(true)) return;
    g_thread = std::thread(pollLoop);
}

void stop() {
    if (!g_run.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
}

Snapshot get() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_snap;
}

void set(const std::string& query) {
    std::lock_guard<std::mutex> lock(g_cmdMu);
    g_pending.emplace_back("/set", query);
}

void cmd(const std::string& query) {
    std::lock_guard<std::mutex> lock(g_cmdMu);
    g_pending.emplace_back("/cmd", query);
}

bool connected() { return g_connected.load(); }

long long ageMs() {
    long long last = g_lastOk.load();
    if (last == 0) return -1;
    return (long long)GetTickCount64() - last;
}

}  // namespace bridge
