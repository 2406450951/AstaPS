// menu.cpp - the in-game menu. Akebi-style tab layout, all state routed to the server plugin.
#include "menu.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bridge.h"

namespace {

constexpr const char* kHotkeys[] = {"INSERT", "F1", "F2", "F3", "HOME", "END"};

enum Tab { TAB_PLAYER = 0, TAB_WORLD, TAB_VISUAL, TAB_TELEPORT, TAB_GM, TAB_SETTINGS, TAB_COUNT };
const char* kTabNames[TAB_COUNT] = {"玩家", "世界", "视觉", "传送", "GM", "设置"};
const char* kTabIcons[TAB_COUNT] = {"P", "W", "V", "T", "G", "S"};

bool g_open = false;  // INSERT / F1 opens it; starting closed keeps the login UI clickable
int g_tab = TAB_PLAYER;
int g_hotkey = 0;

menu::ClientConfig g_cfg;
CheatState g_st;         // local mirror, pushed to the server on change
bool g_stInit = false;

// teleport / spawn editors
float g_tp[3] = {0, 0, 0};
int g_sceneId = 3;
int g_spawnId = 21010101;
int g_spawnLvl = 90;
int g_spawnCount = 1;
int g_giveId = 202;
int g_giveCount = 100000;
char g_bookmark[64] = "home";
int g_weatherId = 1;
float g_timeH = 12.f;
float g_timeM = 0.f;

void push(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bridge::set(buf);
}

void action(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bridge::cmd(buf);
}

void syncFromServer(const Snapshot& s) {
    if (!g_stInit && s.valid) {
        g_st = s.st;
        g_tp[0] = s.px;
        g_tp[1] = s.py;
        g_tp[2] = s.pz;
        g_stInit = true;
    }
}

// ---------------------------------------------------------------- widgets

void sectionHeader(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.55f, 0.95f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// key == nullptr means the switch is client-side only (HUD options), nothing goes to the server.
bool toggleRow(const char* label, bool* value, const char* key = nullptr) {
    bool changed = ImGui::Checkbox(label, value);
    if (changed && key) push("%s=%d", key, *value ? 1 : 0);
    return changed;
}

bool sliderRowF(const char* label, float* value, float lo, float hi, const char* fmt,
                const char* key) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::SliderFloat(("##" + std::string(key)).c_str(), value, lo, hi, fmt);
    if (changed) push("%s=%.3f", key, *value);
    return changed;
}

void actionButton(const char* label, const char* fmt, ...) {
    if (ImGui::Button(label, ImVec2(-1, 0))) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        action("%s", buf);
        action("c=x");  // no-op keeps the queue moving when fmt was a full query
    }
}

void actionQuery(const char* label, const char* query) {
    if (ImGui::Button(label, ImVec2(-1, 0))) {
        // query is passed straight through: "c=killall" etc.
        bridge::cmd(query);
    }
}

// ---------------------------------------------------------------- tabs

void tabPlayer(const Snapshot& s) {
    sectionHeader("生存");
    toggleRow("无敌 (God Mode)", &g_st.god, "god");
    toggleRow("无摔落伤害 (No Fall)", &g_st.fall, "fall");
    toggleRow("无限体力 (Infinite Stamina)", &g_st.stamina, "stamina");
    toggleRow("自动回血 (Auto Heal)", &g_st.autoheal, "autoheal");

    ImGui::Spacing();
    sectionHeader("战斗");
    sliderRowF("伤害倍率", &g_st.mult, 1.f, 1000.f, "x%.1f", "mult");
    ImGui::TextDisabled("1 = 原版，100 = 基本秒杀，1000 = 见谁秒谁");

    ImGui::Spacing();
    sectionHeader("状态");
    ImGui::Text("UID      %d", s.uid);
    ImGui::Text("昵称     %s", s.nick.empty() ? "-" : s.nick.c_str());
    ImGui::Text("场景     %d", s.scene);
    ImGui::Text("坐标     %.1f, %.1f, %.1f", s.px, s.py, s.pz);
    if (s.maxHp > 0) {
        char hpText[64];
        snprintf(hpText, sizeof(hpText), "%.0f / %.0f", s.hp, s.maxHp);
        ImGui::Text("生命     %s", hpText);
        ImGui::ProgressBar(s.maxHp > 0 ? s.hp / s.maxHp : 0.f, ImVec2(-1, 6), "");
    }
}

void tabWorld(const Snapshot& s) {
    (void)s;
    sectionHeader("拾取");
    toggleRow("自动拾取 (Auto Loot)", &g_st.loot, "loot");
    sliderRowF("拾取半径", &g_st.lootR, 2.f, 120.f, "%.0f m", "lootr");
    actionQuery("立刻捡一圈", "c=lootnow");
    ImGui::Spacing();
    sectionHeader("怪物吸附 / 大招");
    if (ImGui::Checkbox("怪物吸附 (Mob Vacuum)", &g_cfg.mobVac))
        push("mobvac=%d", g_cfg.mobVac ? 1 : 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("##mobvarr", &g_cfg.mobVacR, 5.f, 80.f, "%.0f m") && ImGui::IsItemDeactivatedAfterEdit())
        push("mobvarr=%.0f", g_cfg.mobVacR);
    actionQuery("吸附一次 (Pull Once)", "c=mobvacnow");
    if (ImGui::Checkbox("无限大招 (Infinite Burst)", &g_cfg.burst))
        push("burst=%d", g_cfg.burst ? 1 : 0);
    if (ImGui::Checkbox("技能可用 (Skill Ready)", &g_cfg.skillReady))
        push("skillready=%d", g_cfg.skillReady ? 1 : 0);

    ImGui::Spacing();
    sectionHeader("地图标点传送");
    actionQuery("传送到最远标点 (Map Pin)", "c=mapgo");
    ImGui::TextDisabled("在大地图上扎一根针，按这个按钮过去");


    ImGui::Spacing();
    sectionHeader("杀意光环");
    toggleRow("开启 (Kill Aura)", &g_st.aura, "aura");
    sliderRowF("半径", &g_st.auraR, 2.f, 100.f, "%.0f m", "aurar");
    sliderRowF("每跳伤害", &g_st.auraDmg, 1000.f, 200000.f, "%.0f", "auradmg");
    actionQuery("清场 (Kill All)", "c=killall");
    actionQuery("补满血 (Heal)", "c=heal");

    ImGui::Spacing();
    sectionHeader("时间 / 天气");
    toggleRow("冻结时间", &g_st.freeze, "freeze");
    ImGui::TextUnformatted("时间");
    ImGui::SetNextItemWidth(-1);
    g_timeH = (float)g_st.timeH;
    if (ImGui::SliderFloat("##hour", &g_timeH, 0.f, 23.f, "%.0f 时")) {
        g_st.timeH = (int)g_timeH;
        push("timeh=%d", g_st.timeH);
        action("c=time&h=%d&m=%d", g_st.timeH, g_st.timeM);
    }
    if (ImGui::Button("晴", ImVec2(70, 0))) { action("c=weather&id=1&area=1"); g_st.weather = 1; }
    ImGui::SameLine();
    if (ImGui::Button("夜/阴", ImVec2(70, 0))) { action("c=weather&id=2&area=1"); g_st.weather = 2; }
    ImGui::SameLine();
    if (ImGui::Button("雨", ImVec2(70, 0))) { action("c=weather&id=3&area=1"); g_st.weather = 3; }

    ImGui::Spacing();
    sectionHeader("刷怪 / 给物品");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("怪物 ID", &g_spawnId, 0, 0);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("等级", &g_spawnLvl, 0, 0);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("数量", &g_spawnCount, 0, 0);
    if (ImGui::Button("在身前刷怪", ImVec2(-1, 0))) {
        action("c=spawn&id=%d&lvl=%d&n=%d", g_spawnId, g_spawnLvl, g_spawnCount);
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("物品 ID", &g_giveId, 0, 0);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("数量", &g_giveCount, 0, 0);
    if (ImGui::Button("给我 (Give)", ImVec2(-1, 0))) {
        action("c=give&id=%d&n=%d", g_giveId, g_giveCount);
    }
    ImGui::TextDisabled("摩拉 202 / 原石 201 / 树脂 106");
}

void tabVisual(const Snapshot& s) {
    sectionHeader("ESP");
    toggleRow("实体数据流 (ESP data)", &g_st.esp, "esp");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##espdist", &g_cfg.espMaxDist, 20.f, 300.f, "范围 %.0f m");
    toggleRow("屏幕雷达环", &g_cfg.entityHud);
    toggleRow("附近实体列表", &g_cfg.nearList);

    ImGui::Spacing();
    sectionHeader("HUD");
    toggleRow("水印 (Watermark)", &g_cfg.watermark);
    toggleRow("坐标显示", &g_cfg.coords);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##hudscale", &g_cfg.hudScale, 0.6f, 2.0f, "HUD 缩放 %.2f");

    ImGui::Spacing();
    sectionHeader("计数");
    int monsters = 0, gadgets = 0, items = 0;
    for (const auto& e : s.ents) {
        if (e.kind == 'M') ++monsters;
        else if (e.kind == 'G') ++gadgets;
        else if (e.kind == 'I') ++items;
    }
    ImGui::Text("怪物 %d   机关 %d   掉落 %d", monsters, gadgets, items);
    ImGui::TextDisabled("数据来自服务端实体流，不需要读客户端内存");
}

void tabTeleport(const Snapshot& s) {
    sectionHeader("坐标传送");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat3("##tp", g_tp, 1.0f, -10000.f, 10000.f, "%.1f");
    if (ImGui::Button("传送到坐标", ImVec2(-1, 0))) {
        action("c=tp&x=%.2f&y=%.2f&z=%.2f", g_tp[0], g_tp[1], g_tp[2]);
    }
    if (ImGui::Button("用当前位置填充", ImVec2(-1, 0))) {
        g_tp[0] = s.px; g_tp[1] = s.py; g_tp[2] = s.pz;
    }

    ImGui::Spacing();
    sectionHeader("快捷");
    actionQuery("传送到最近宝箱", "c=chest");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("场景 ID", &g_sceneId, 0, 0);
    if (ImGui::Button("切换场景", ImVec2(-1, 0))) {
        action("c=scene&id=%d", g_sceneId);
    }

    ImGui::Spacing();
    sectionHeader("书签");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##bm", g_bookmark, sizeof(g_bookmark));
    if (ImGui::Button("保存当前点位", ImVec2(-1, 0))) {
        action("c=bookmark&name=%s", g_bookmark);
    }
    if (ImGui::Button("传送到书签", ImVec2(-1, 0))) {
        action("c=go&name=%s", g_bookmark);
    }
}

void tabGM(const Snapshot& s) {
    sectionHeader("地图 / 传送点");
    actionQuery("解锁全部地图点", "c=raw&text=unlock");
    actionQuery("解锁（含任务保护点）", "c=raw&text=unlock --force");
    actionQuery("解锁全部传送点", "c=raw&text=waypoints");

    ImGui::Spacing();
    sectionHeader("背包 / 资源");
    actionQuery("清空背包（全部）", "c=raw&text=clear all");
    actionQuery("清空武器", "c=raw&text=clear weapons");
    actionQuery("清空圣遗物", "c=raw&text=clear artifacts");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("摩拉数量", &g_giveCount, 100000, 1000000);
    if (ImGui::Button("刷摩拉（202）", ImVec2(-1, 0))) {
        action("c=give&id=202&n=%d", g_giveCount);
    }
    if (ImGui::Button("刷原石（201）", ImVec2(-1, 0))) {
        action("c=give&id=201&n=%d", g_giveCount);
    }

    ImGui::Spacing();
    sectionHeader("副本 / 场景");
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("副本 ID", &g_sceneId, 1, 100);
    if (ImGui::Button("传送到副本", ImVec2(-1, 0))) {
        action("c=raw&text=dungeon %d", g_sceneId);
    }

    ImGui::Spacing();
    sectionHeader("说明");
    ImGui::TextDisabled("这些按钮走服务端 GM 命令，权限已是 *（全开）");
    ImGui::Text("最近动作：%s", s.lastAction.empty() ? "-" : s.lastAction.c_str());
}

void tabSettings(const Snapshot& s) {
    sectionHeader("菜单");
    ImGui::Text("呼出按键");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##hotkey", &g_hotkey, kHotkeys, IM_ARRAYSIZE(kHotkeys));
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##accent_r", &g_cfg.accent[0], 0.f, 1.f, "红 %.2f");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##accent_g", &g_cfg.accent[1], 0.f, 1.f, "绿 %.2f");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##accent_b", &g_cfg.accent[2], 0.f, 1.f, "蓝 %.2f");

    ImGui::Spacing();
    sectionHeader("连接");
    ImGui::Text("服务端   %s", bridge::connected() ? "已连接 (127.0.0.1:8890)" : "未连接");
    ImGui::Text("延迟     %lld ms", bridge::ageMs());
    ImGui::Text("最近动作 %s", s.lastAction.empty() ? "-" : s.lastAction.c_str());
    if (ImGui::Button("重新应用全部开关", ImVec2(-1, 0))) {
        push("god=%d&fall=%d&loot=%d&stamina=%d&autoheal=%d&aura=%d&esp=%d&freeze=%d",
             g_st.god, g_st.fall, g_st.loot, g_st.stamina, g_st.autoheal, g_st.aura, g_st.esp,
             g_st.freeze);
    }

    ImGui::Spacing();
    sectionHeader("说明");
    ImGui::TextWrapped("菜单由游戏内 DLL 绘制；开关通过 127.0.0.1:8890 交给服务端插件执行，"
                       "伤害 / 掉落 / 时间都以服务端为准，所以单人私服里表现和 Akebi 一致。");
}

void applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    const float r = g_cfg.accent[0], g = g_cfg.accent[1], b = g_cfg.accent[2];

    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.055f, 0.070f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.075f, 0.092f, 0.85f);
    c[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.26f, 0.60f);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(r * 0.4f, g * 0.4f, b * 0.4f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(r * 0.55f, g * 0.55f, b * 0.55f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(r, g, b, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(r, g, b, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(r, g, b, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(r + 0.1f, g + 0.1f, b + 0.1f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(r * 0.45f, g * 0.45f, b * 0.45f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(r * 0.65f, g * 0.65f, b * 0.65f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(r, g, b, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.28f, 0.60f);
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);

    style.WindowRounding = 6.f;
    style.ChildRounding = 5.f;
    style.FrameRounding = 4.f;
    style.GrabRounding = 4.f;
    style.PopupRounding = 4.f;
    style.ItemSpacing = ImVec2(8, 7);
    style.FramePadding = ImVec2(8, 5);
}

}  // namespace

namespace menu {

ClientConfig& config() { return g_cfg; }
bool isOpen() { return g_open; }
void setOpen(bool open) { g_open = open; }
void toggle() { g_open = !g_open; }
const char* hotkeyName() { return kHotkeys[g_hotkey]; }

// ---------------------------------------------------------------- config persistence
//
// The toggles are remembered between sessions in `gs71_menu.cfg` next to this DLL. The very first
// run writes an all-off file, so a fresh install never starts with cheats enabled; every later run
// loads the file and applies it once the server connection is up.
static void cfgPath(char* out, size_t cap) {
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&cfgPath, &mod);
    char p[MAX_PATH] = {0};
    GetModuleFileNameA(mod, p, MAX_PATH);
    char* slash = strrchr(p, '\\');
    if (slash) *(slash + 1) = 0; else p[0] = 0;
    snprintf(out, cap, "%sgs71_menu.cfg", p);
}

static void saveConfig() {
    char path[MAX_PATH];
    cfgPath(path, sizeof(path));
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# GS71 menu config - written automatically\n");
    fprintf(f, "watermark=%d\n", g_cfg.watermark ? 1 : 0);
    fprintf(f, "coords=%d\n", g_cfg.coords ? 1 : 0);
    fprintf(f, "entityHud=%d\n", g_cfg.entityHud ? 1 : 0);
    fprintf(f, "nearList=%d\n", g_cfg.nearList ? 1 : 0);
    fprintf(f, "glow=%d\n", g_cfg.glow ? 1 : 0);
    fprintf(f, "hudScale=%.3f\n", g_cfg.hudScale);
    fprintf(f, "espMaxDist=%.1f\n", g_cfg.espMaxDist);
    fprintf(f, "theme=%d\n", g_cfg.theme);
    fprintf(f, "mobvac=%d\n", g_cfg.mobVac ? 1 : 0);
    fprintf(f, "mobvarr=%.1f\n", g_cfg.mobVacR);
    fprintf(f, "burst=%d\n", g_cfg.burst ? 1 : 0);
    fprintf(f, "skillready=%d\n", g_cfg.skillReady ? 1 : 0);
    fprintf(f, "accent=%.3f %.3f %.3f\n", g_cfg.accent[0], g_cfg.accent[1], g_cfg.accent[2]);
    fprintf(f, "god=%d\n", g_st.god ? 1 : 0);
    fprintf(f, "fall=%d\n", g_st.fall ? 1 : 0);
    fprintf(f, "loot=%d\n", g_st.loot ? 1 : 0);
    fprintf(f, "stamina=%d\n", g_st.stamina ? 1 : 0);
    fprintf(f, "autoheal=%d\n", g_st.autoheal ? 1 : 0);
    fprintf(f, "aura=%d\n", g_st.aura ? 1 : 0);
    fprintf(f, "freeze=%d\n", g_st.freeze ? 1 : 0);
    fprintf(f, "esp=%d\n", g_st.esp ? 1 : 0);
    fprintf(f, "mult=%.2f\n", g_st.mult);
    fprintf(f, "aurar=%.1f\n", g_st.auraR);
    fprintf(f, "auradmg=%.1f\n", g_st.auraDmg);
    fprintf(f, "lootr=%.1f\n", g_st.lootR);
    fprintf(f, "timeh=%d\n", g_st.timeH);
    fprintf(f, "timem=%d\n", g_st.timeM);
    fprintf(f, "weather=%d\n", g_st.weather);
    fclose(f);
}

// Returns true when a config file existed (false = first run, defaults already all-off).
static bool loadConfig() {
    char path[MAX_PATH];
    cfgPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) { saveConfig(); return false; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = line;
        const char* v = eq + 1;
        auto isOn = [] (const char* s) { return s[0] == '1' || s[0] == 't' || s[0] == 'T'; };
        if (!strcmp(k, "watermark")) g_cfg.watermark = isOn(v);
        else if (!strcmp(k, "coords")) g_cfg.coords = isOn(v);
        else if (!strcmp(k, "entityHud")) g_cfg.entityHud = isOn(v);
        else if (!strcmp(k, "nearList")) g_cfg.nearList = isOn(v);
        else if (!strcmp(k, "glow")) g_cfg.glow = isOn(v);
        else if (!strcmp(k, "hudScale")) g_cfg.hudScale = (float)atof(v);
        else if (!strcmp(k, "espMaxDist")) g_cfg.espMaxDist = (float)atof(v);
        else if (!strcmp(k, "theme")) g_cfg.theme = atoi(v);
        else if (!strcmp(k, "mobvac")) g_cfg.mobVac = isOn(v);
        else if (!strcmp(k, "mobvarr")) g_cfg.mobVacR = (float)atof(v);
        else if (!strcmp(k, "burst")) g_cfg.burst = isOn(v);
        else if (!strcmp(k, "skillready")) g_cfg.skillReady = isOn(v);
        else if (!strcmp(k, "accent")) sscanf(v, "%f %f %f", &g_cfg.accent[0], &g_cfg.accent[1], &g_cfg.accent[2]);
        else if (!strcmp(k, "god")) g_st.god = isOn(v);
        else if (!strcmp(k, "fall")) g_st.fall = isOn(v);
        else if (!strcmp(k, "loot")) g_st.loot = isOn(v);
        else if (!strcmp(k, "stamina")) g_st.stamina = isOn(v);
        else if (!strcmp(k, "autoheal")) g_st.autoheal = isOn(v);
        else if (!strcmp(k, "aura")) g_st.aura = isOn(v);
        else if (!strcmp(k, "freeze")) g_st.freeze = isOn(v);
        else if (!strcmp(k, "esp")) g_st.esp = isOn(v);
        else if (!strcmp(k, "mult")) g_st.mult = (float)atof(v);
        else if (!strcmp(k, "aurar")) g_st.auraR = (float)atof(v);
        else if (!strcmp(k, "auradmg")) g_st.auraDmg = (float)atof(v);
        else if (!strcmp(k, "lootr")) g_st.lootR = (float)atof(v);
        else if (!strcmp(k, "timeh")) g_st.timeH = atoi(v);
        else if (!strcmp(k, "timem")) g_st.timeM = atoi(v);
        else if (!strcmp(k, "weather")) g_st.weather = atoi(v);
    }
    fclose(f);
    return true;
}

// Push the whole state to the server (used right after loading the config).
static void pushState() {
    char q[512];
    snprintf(q, sizeof(q),
             "god=%d&fall=%d&loot=%d&stamina=%d&autoheal=%d&mult=%.2f&aura=%d&aurar=%.1f"
             "&auradmg=%.1f&lootr=%.1f&freeze=%d&timeh=%d&timem=%d&weather=%d&esp=%d",
             g_st.god ? 1 : 0, g_st.fall ? 1 : 0, g_st.loot ? 1 : 0, g_st.stamina ? 1 : 0,
             g_st.autoheal ? 1 : 0, g_st.mult, g_st.aura ? 1 : 0, g_st.auraR, g_st.auraDmg,
             g_st.lootR, g_st.freeze ? 1 : 0, g_st.timeH, g_st.timeM, g_st.weather,
             g_st.esp ? 1 : 0);
        bridge::set(q);
}

void draw() {
    // Load the saved config once, then push it to the server as soon as the link answers.
    // A fresh install writes an all-off file, so nothing is enabled on the first launch.
    static int cfgState = 0;   // 0 = not loaded, 1 = loaded, 2 = applied
    static CheatState lastSaved{};
    static ClientConfig lastCfg{};
    if (cfgState == 0) {
        const bool hadFile = loadConfig();
        if (!hadFile) {
            // First run: the file was just created with every toggle off. Push those defaults so a
            // stale state left on the server cannot leak into the session or back into the file.
            memset(&g_st, 0, sizeof(g_st));
            g_st.mult = 1.f;
            g_st.auraR = 12.f;
            g_st.auraDmg = 30000.f;
            g_st.lootR = 25.f;
            g_st.timeH = 12;
            g_st.weather = -1;
            g_st.esp = true;
        }
        lastSaved = g_st;
        lastCfg = g_cfg;
        cfgState = 1;
    }
    if (cfgState == 1 && bridge::connected()) {
        pushState();
        bridge::set("mobvac=" + std::string(g_cfg.mobVac ? "1" : "0"));
        push("mobvarr=%.0f", g_cfg.mobVacR);
        bridge::set("burst=" + std::string(g_cfg.burst ? "1" : "0"));
        bridge::set("skillready=" + std::string(g_cfg.skillReady ? "1" : "0"));
        cfgState = 2;
    }
    // "menuopen.on" next to the game exe forces the menu open at launch; the normal path is the
    // INSERT / F1 hotkey. Used by the automated screenshots.
    static int forceOpen = -1;
    if (forceOpen < 0) {
        char dir[MAX_PATH];
        GetModuleFileNameA(nullptr, dir, MAX_PATH);
        char* slash = strrchr(dir, '\\');
        forceOpen = 0;
        if (slash) {
            *slash = 0;
            char flag[MAX_PATH];
            snprintf(flag, sizeof(flag), "%s\\menuopen.on", dir);
            if (GetFileAttributesA(flag) != INVALID_FILE_ATTRIBUTES) {
                forceOpen = 1;
                g_open = true;
            }
        }
    }

    Snapshot s = bridge::get();
    syncFromServer(s);
    applyTheme();

    const int vk[] = {VK_INSERT, VK_F1, VK_F2, VK_F3, VK_HOME, VK_END};
    (void)vk;

    // Persist whenever something actually changed (cheap: compares against the last saved copy).
    if (memcmp(&g_st, &lastSaved, sizeof(CheatState)) != 0 ||
        memcmp(&g_cfg, &lastCfg, sizeof(ClientConfig)) != 0) {
        saveConfig();
        lastSaved = g_st;
        lastCfg = g_cfg;
    }

    if (!g_open) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(960, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f - 480, io.DisplaySize.y * 0.5f - 320),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.97f);

    ImGui::Begin("GS71  ·  原神 7.1 私服菜单", &g_open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    // ---- sidebar -----------------------------------------------------------
    ImGui::BeginChild("##side", ImVec2(178, -1), true);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(g_cfg.accent[0], g_cfg.accent[1], g_cfg.accent[2], 1.f));
    ImGui::Text("GS71");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("private server");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < TAB_COUNT; ++i) {
        bool selected = (g_tab == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                   ImVec4(g_cfg.accent[0], g_cfg.accent[1], g_cfg.accent[2], 0.85f));
        }
        if (ImGui::Button(kTabNames[i], ImVec2(-1, 40))) g_tab = i;
        if (selected) ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImVec4 dot = bridge::connected() ? ImVec4(0.35f, 0.85f, 0.45f, 1.f) : ImVec4(0.85f, 0.35f, 0.35f, 1.f);
    ImGui::TextColored(dot, bridge::connected() ? "● 已连接" : "● 未连接");
    ImGui::TextDisabled("%.0f FPS", io.Framerate);
    ImGui::TextDisabled("%s 关闭", hotkeyName());
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- content -----------------------------------------------------------
    ImGui::BeginChild("##content", ImVec2(-1, -1), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    switch (g_tab) {
        case TAB_PLAYER: tabPlayer(s); break;
        case TAB_WORLD: tabWorld(s); break;
        case TAB_VISUAL: tabVisual(s); break;
        case TAB_TELEPORT: tabTeleport(s); break;
        case TAB_GM: tabGM(s); break;
        case TAB_SETTINGS: tabSettings(s); break;
        default: break;
    }
    ImGui::EndChild();

    ImGui::End();
}

void drawHud() {
    Snapshot s = bridge::get();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = g_cfg.hudScale;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // ---- watermark ---------------------------------------------------------
    if (g_cfg.watermark) {
        char line[160];
        snprintf(line, sizeof(line), "GS71  ·  原神 7.1  ·  %s  ·  %.0f FPS",
                 bridge::connected() ? "online" : "offline", io.Framerate);
        ImVec2 sz = ImGui::CalcTextSize(line);
        ImVec2 p(18, 16);
        dl->AddRectFilled(ImVec2(p.x - 8, p.y - 6),
                          ImVec2(p.x + sz.x * scale + 10, p.y + sz.y * scale + 8),
                          IM_COL32(12, 12, 18, 170), 4.f);
        dl->AddText(
            ImGui::GetFont(), ImGui::GetFontSize() * scale, p,
            IM_COL32(int(160 * g_cfg.accent[0] + 90), int(160 * g_cfg.accent[1] + 90),
                     int(160 * g_cfg.accent[2] + 90), 255),
            line);
    }

    if (!s.online) return;

    // ---- coordinates -------------------------------------------------------
    if (g_cfg.coords) {
        char line[200];
        snprintf(line, sizeof(line), "%.1f , %.1f , %.1f    场景 %d    HP %.0f/%.0f",
                 s.px, s.py, s.pz, s.scene, s.hp, s.maxHp);
        dl->AddText(
            ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(20, 44),
            IM_COL32(230, 230, 240, 220), line);
    }

    // ---- compass ring ------------------------------------------------------
    if (g_cfg.entityHud && !s.ents.empty()) {
        const float cx = io.DisplaySize.x * 0.5f;
        const float cy = io.DisplaySize.y - 120.f;
        const float radius = 74.f;
        ImDrawList* fg = ImGui::GetForegroundDrawList();

        fg->AddCircle(ImVec2(cx, cy), radius, IM_COL32(30, 30, 40, 140), 64, 2.f);
        fg->AddCircle(ImVec2(cx, cy), 1.5f, IM_COL32(200, 200, 220, 160), 8, 3.f);

        int drawn = 0;
        for (const auto& e : s.ents) {
            if (drawn >= 60) break;
            if (e.dist > g_cfg.espMaxDist) continue;

            float dx = e.x - s.px;
            float dz = e.z - s.pz;
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 0.5f) continue;

            // dx/dz is world space; map to the ring with +Z up on screen.
            float nx = dx / len;
            float nz = dz / len;
            float px = cx + nx * radius;
            float py = cy - nz * radius;

            ImU32 col;
            float r = 4.f;
            switch (e.kind) {
                case 'M': col = IM_COL32(255, 90, 90, 235); r = 4.5f; break;
                case 'G': col = IM_COL32(255, 205, 90, 220); r = 4.0f; break;
                case 'I': col = IM_COL32(110, 230, 140, 220); r = 3.0f; break;
                default: col = IM_COL32(150, 160, 200, 180); r = 2.5f; break;
            }
            fg->AddCircleFilled(ImVec2(px, py), r, col, 12);
            if (e.kind == 'M' && e.dist < 40.f) {
                char dist[32];
                snprintf(dist, sizeof(dist), "%.0f", e.dist);
                fg->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f * scale,
                            ImVec2(px + 6, py - 6), IM_COL32(255, 200, 200, 200), dist);
            }
            ++drawn;
        }
    }

    // ---- nearby entity list ------------------------------------------------
    if (g_cfg.nearList && !s.ents.empty()) {
        std::vector<EntInfo> sorted = s.ents;
        std::sort(sorted.begin(), sorted.end(),
                  [](const EntInfo& a, const EntInfo& b) { return a.dist < b.dist; });

        int shown = 0;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, 60), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(240, 320), ImGuiCond_Always);
        ImGui::Begin("##near", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("附近实体");
        ImGui::Separator();
        for (const auto& e : sorted) {
            if (shown++ >= 14) break;
            const char* kind = e.kind == 'M' ? "怪物" : e.kind == 'G' ? "机关" : e.kind == 'I' ? "掉落" : "其他";
            ImU32 col = e.kind == 'M' ? IM_COL32(255, 130, 130, 255)
                                      : e.kind == 'G' ? IM_COL32(255, 215, 120, 255)
                                                      : IM_COL32(140, 235, 160, 255);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", kind);
            ImGui::SameLine(60);
            ImGui::Text("%.0fm", e.dist);
            ImGui::SameLine(110);
            ImGui::TextDisabled("#%d", e.typeId);
        }
        ImGui::End();
    }
}

}  // namespace menu
