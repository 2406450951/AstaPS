// menu.cpp - the in-game menu. Akebi-style tab layout, all state routed to the server plugin.
#include "menu.h"

void Gs71CamProbeStart();
void Gs71CamProbeCompare();
void Gs71CamProbeDump();
void Gs71CamAnchor();
void Gs71CamPointerScan();
void Gs71CamDumpHolders();
void Gs71CamBpProbe();
void Gs71CamFreeToggle(bool on);
bool Gs71CamFreeEnabled();
int Gs71CamFreeWrites();
float Gs71CamFreeGet(int i);
void Gs71CamFreeNudge(int axis, float d);
void Gs71CamFreeHookToggle(bool on);
bool Gs71CamFreeHookEnabled();
float Gs71CamFreeHookGet(int i);
void Gs71CamFreeHookNudge(int a, float d);
void Gs71AutoTalkToggle(bool on);
bool Gs71AutoTalkEnabled();
int Gs71AutoTalkCount();
bool gs71InputFocused();
void Gs71Log(const char* m);
bool Gs71Focused();
int Gs71FlyModeGet();
void Gs71ElemView(bool on);
bool Gs71ElemViewGet();
void Gs71CutsceneSkip(bool on);
bool Gs71CutsceneSkipGet();
void Gs71HideUi(bool on);
bool Gs71HideUiGet();
void Gs71FlyMode(int m);
void Gs71DumpMetadata();
void Gs71CbFreeToggle(bool on);
// kamera marker
bool Gs71CbFreeEnabled();
void Gs71CbFreeNudge(int a, float d);
float Gs71CbFreeGet(int i);
unsigned long long Gs71CbFreeHits();
unsigned long long Gs71CbFreePatched();
unsigned long long Gs71CbMapStat(int which);
unsigned long long Gs71CamHookHits();
unsigned long long Gs71CamSkipCount();
// ---- 第二批（字符串 / 隐藏UI / 世界投影 ESP / 原生飞行）----
bool Gs71Project(float x, float y, float z, float* sx, float* sy, float vw, float vh);
void Gs71FlySet(bool on);
bool Gs71FlyGet();
void Gs71FlySpeedSet(float v);
float Gs71FlySpeedGet();
void Gs71HideUi(bool on);
void Gs71FlyMode(int m);
void Gs71CutsceneSkip(bool on);
void Gs71ElemView(bool on);
bool Gs71ElemViewGet();
bool Gs71Bearing(float x, float y, float z, float* outDeg, float* outDist);
bool Gs71CutsceneSkipGet();
bool Gs71HideUiGet();

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

constexpr const char* kHotkeys[] = {"INSERT", "DELETE"};

enum Tab { TAB_PLAYER = 0, TAB_WORLD, TAB_VISUAL, TAB_TELEPORT, TAB_GM, TAB_SETTINGS, TAB_COUNT };
const char* kTabNames[TAB_COUNT] = {"玩家", "世界", "视觉", "传送", "其他", "设置"};
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
bool g_speedOn = false;
int g_hits = 1;
float g_gravVal = -2.0f;
float g_ncStep = 5.0f;   // 单次穿墙步长
bool g_noclip = false;
float g_noclipSpeed = 4.0f;
bool g_autoTp = false;   // 扎针即刻传送
bool g_autoInt = true;   // 自动交互
bool g_mapT = false;     // T 键传送到地图标点
bool g_mapTDown = false;
float g_speedRatio = 1.5f;
int g_avatarId = 10000002;
int g_avatarLvl = 90;
int g_propId = 100;
float g_propVal = 1.f;
int g_questId = 20001;
int g_dungeonId = 2001;
int g_uid = 1;
char g_msg[120] = "";
char g_itemCustom[16] = "";

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


// ---------------------------------------------------------------- 作弊快捷键
//
// 表里每行：显示名、绑定到哪个 VK 字段、切换哪个开关、下发给服务端时用哪个字段名。
struct CheatHotkey {
    const char* name;
    int* vk;
    bool* value;
    const char* field;
};

CheatHotkey g_hotkeys_cheat[] = {
    {"无敌", &g_cfg.hkGod, &g_st.god, "god"},
    {"无摔落", &g_cfg.hkFall, &g_st.fall, "fall"},
    {"自动拾取", &g_cfg.hkLoot, &g_st.loot, "loot"},
    {"无限体力", &g_cfg.hkStamina, &g_st.stamina, "stamina"},
    {"自动回血", &g_cfg.hkAutoheal, &g_st.autoheal, "autoheal"},
    {"杀意光环", &g_cfg.hkAura, &g_st.aura, "aura"},
    {"冻结时间", &g_cfg.hkFreeze, &g_st.freeze, "freeze"},
    {"实体数据流", &g_cfg.hkEsp, &g_st.esp, "esp"},
    {"怪物吸附", &g_cfg.hkMobVac, &g_cfg.mobVac, "mobvac"},
    {"无限大招", &g_cfg.hkBurst, &g_cfg.burst, "burst"},
    {"技能可用", &g_cfg.hkSkill, &g_cfg.skillReady, "skillready"},
};
const int g_hotkeyCount = (int)(sizeof(g_hotkeys_cheat) / sizeof(g_hotkeys_cheat[0]));

const char* vkName(int vk) {
    static char buf[24];
    if (vk == 0) return "未绑定";
    if (vk >= VK_F1 && vk <= VK_F12) {
        static char f[8];
        snprintf(f, sizeof(f), "F%d", vk - VK_F1 + 1);
        return f;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        buf[0] = (char)vk;
        buf[1] = 0;
        return buf;
    }
    if (vk == VK_INSERT) return "Insert";
    if (vk == VK_DELETE) return "Delete";
    if (vk == VK_HOME) return "Home";
    if (vk == VK_END) return "End";
    if (vk == VK_PRIOR) return "PageUp";
    if (vk == VK_NEXT) return "PageDown";
    if (vk == VK_NUMPAD0 + (vk - VK_NUMPAD0) && vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        static char n[8];
        snprintf(n, sizeof(n), "Num%d", vk - VK_NUMPAD0);
        return n;
    }
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

void pollCheatHotkeys() {
    for (int i = 0; i < g_hotkeyCount; ++i) {
        int vk = *g_hotkeys_cheat[i].vk;
        if (vk == 0) continue;
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        static bool was[32] = {false};
        if (down && !was[i]) {
            *g_hotkeys_cheat[i].value = !*g_hotkeys_cheat[i].value;
            bool on = *g_hotkeys_cheat[i].value;
            push("%s=%d", g_hotkeys_cheat[i].field, on ? 1 : 0);
            if (!strcmp(g_hotkeys_cheat[i].field, "mobvac")) {
                // 这两个是本地开关，服务端字段名不同，单独补一条
                push("mobvac=%d", on ? 1 : 0);
            }
        }
        was[i] = down;
    }
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 8.f;
    const float w = ImGui::GetContentRegionAvail().x;
    const float r = g_cfg.accent[0], g = g_cfg.accent[1], b = g_cfg.accent[2];
    dl->AddRectFilled(ImVec2(p.x, p.y + 1.f), ImVec2(p.x + 3.f, p.y + h - 1.f),
                      ImGui::GetColorU32(ImVec4(r, g, b, 1.f)), 2.f);
    dl->AddRectFilled(ImVec2(p.x + 3.f, p.y), ImVec2(p.x + w, p.y + h),
                      ImGui::GetColorU32(ImVec4(r, g, b, 0.09f)), 3.f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 12.f, p.y + 4.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.86f, 1.0f, 1.f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 6.f));
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

    ImGui::Spacing();
    sectionHeader("剧情加速（自动对话）");
    {
        static bool talk = false;
        if (ImGui::Checkbox("自动敲空格推进对话", &talk)) Gs71AutoTalkToggle(talk);
        talk = Gs71AutoTalkEnabled();
        ImGui::SameLine();
        ImGui::TextDisabled("已敲 %d 次", Gs71AutoTalkCount());
    }

    sectionHeader("移动速度（原生移速药）");
    if (ImGui::Checkbox("开启 +30%（数据内置值）", &g_speedOn)) {
        if (g_speedOn) action("c=speed&r=1.3");
        else action("c=speed&on=0");
    }
    ImGui::TextDisabled("走游戏原生 buff（170201 移速增加药品），客户端只认数据里的 0.3；");

    ImGui::Spacing();
    sectionHeader("飞行（客户端，实验）");
    {
        static float flySpeed = 6.f;
        bool fly = Gs71FlyGet();
        if (ImGui::Checkbox("自由飞行", &fly)) Gs71FlySet(fly);
        if (fly) {
            flySpeed = Gs71FlySpeedGet();
            ImGui::SetNextItemWidth(200);
            if (ImGui::SliderFloat("速度", &flySpeed, 1.f, 40.f, "%.1f m/s")) Gs71FlySpeedSet(flySpeed);
            ImGui::TextDisabled("空格=上升  Ctrl=下降  方向跟着镜头   V=开关");
        } else {
            ImGui::TextDisabled("走游戏自己的飞行状态（forceAirStateFly）");
        }
    }

    ImGui::Spacing();
    sectionHeader("多段伤害（一刀打 N 次）");
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt("段数", &g_hits, 1, 5)) {
        if (g_hits < 1) g_hits = 1;
        if (g_hits > 30) g_hits = 30;
        push("hits=%d", g_hits);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("和上面的倍率可叠加：10 倍 x 5 段 = 50 次");

    ImGui::TextDisabled("自定义倍数需要客户端接受改值，暂时做不到");

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
    sectionHeader("穿墙（无视碰撞移动）");
    if (ImGui::Button("穿墙前进一步", ImVec2(160, 0))) action("c=ncstep&d=%.1f", g_ncStep);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat("步长", &g_ncStep, 1.f, 30.f, "%.0f m");
    if (ImGui::Checkbox("连续穿墙（会白屏，不推荐）", &g_noclip)) push("noclip=%d", g_noclip ? 1 : 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("速度倍数", &g_noclipSpeed, 1.f, 15.f, "%.1fx")) {
        push("noclipspeed=%.1f", g_noclipSpeed);
    }
    ImGui::TextDisabled("开着它走路，会穿过墙/山/地板；关掉就停在当前位置");

    sectionHeader("拾取");
    toggleRow("自动拾取 (Auto Loot)", &g_st.loot, "loot");
    if (ImGui::Button("传送到最近宝箱", ImVec2(160, 0))) action("c=chesttp");
    ImGui::SameLine();
    if (ImGui::Checkbox("自动交互（宝箱/机关）", &g_autoInt)) push("autoint=%d", g_autoInt ? 1 : 0);
    ImGui::SameLine();
    if (ImGui::Button("立即交互一次", ImVec2(120, 0))) action("c=interactnow");
    ImGui::TextDisabled("和拾取同一个半径；开箱走游戏自己的交互流程，奖励和任务进度正常结算");
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
    if (ImGui::Checkbox("扎针即刻传送（大地图点哪传哪）", &g_autoTp)) push("autotp=%d", g_autoTp ? 1 : 0);
    ImGui::TextDisabled("开大地图 → 在目标位置扎一根针 → 不用按任何键，角色直接被拉过去");
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
    sectionHeader("世界投影 ESP（客户端）");
    ImGui::Checkbox("启用", &g_cfg.espWorld);
    ImGui::SameLine();
    ImGui::TextDisabled("坐标来自服务端，投影由客户端算");
    if (g_cfg.espWorld) {
        int style = g_cfg.espStyle;
        if (ImGui::RadioButton("仅点", style == 0)) g_cfg.espStyle = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("方框", style == 1)) g_cfg.espStyle = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("方框+连线+血条", style == 2)) g_cfg.espStyle = 2;
        ImGui::Checkbox("怪物", &g_cfg.espMobs);
        ImGui::SameLine();
        ImGui::Checkbox("机关/宝箱", &g_cfg.espGadgets);
        ImGui::SameLine();
        ImGui::Checkbox("掉落物", &g_cfg.espItems);
    }

    ImGui::Spacing();
    sectionHeader("界面");
    {
        static bool hideUi = false;
        if (ImGui::Checkbox("隐藏游戏 UI（截图/录像）", &hideUi)) Gs71HideUi(hideUi);
        ImGui::SameLine();
        ImGui::TextDisabled("同 hideui_on.now");
    }

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

    sectionHeader("自由相机（游戏原生）");
    if (ImGui::Button("领取留影机", ImVec2(140, 0))) {
    }
    ImGui::SameLine();
    ImGui::TextDisabled("装备后按小道具键 → 人物定住，镜头自由移动");

    sectionHeader("跨场景传送列表");
    if (ImGui::Button("列出所有已解锁传送点", ImVec2(220, 0))) action("c=allanchors");
    ImGui::SameLine();
    ImGui::TextDisabled("结果在下方回调里（act 行）");

    sectionHeader("安全点");
    if (ImGui::Button("回到蒙德城", ImVec2(200, 0))) action("c=tp&x=2236&y=212&z=-915");
    ImGui::SameLine();
    if (ImGui::Button("回神像", ImVec2(120, 0)))
        action("c=tp&x=2747&y=194&z=-1719");
    ImGui::SameLine();
    if (ImGui::Button("列出已解锁锚点", ImVec2(170, 0))) action("c=anchors");
    ImGui::SameLine();
    if (ImGui::Button("回到锚点 0", ImVec2(120, 0))) action("c=goanchor&i=0");
    ImGui::TextDisabled("卡在虚空/未知区域时点左边那个；锚点编号看「列出」的回调");


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
    if (ImGui::Button("解锁全部地图点", ImVec2(200, 0))) action("c=command&text=unlock all");
    ImGui::SameLine();
    if (ImGui::Button("解锁（含任务保护点）", ImVec2(210, 0)))
        action("c=command&text=unlock all --force");
    ImGui::SameLine();
    if (ImGui::Button("解锁全部传送点", ImVec2(170, 0))) action("c=command&text=waypoints");

    ImGui::Spacing();
    sectionHeader("物品");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("物品 ID", &g_giveId, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("数量", &g_giveCount, 1000, 100000);
    if (ImGui::Button("give 发放", ImVec2(120, 0)))
        action("c=command&text=give %d x%d", g_giveId, g_giveCount);
    ImGui::SameLine();
    if (ImGui::Button("原石 201", ImVec2(95, 0)))
        action("c=command&text=give 201 x%d", g_giveCount);
    ImGui::SameLine();
    if (ImGui::Button("摩拉 202", ImVec2(95, 0)))
        action("c=command&text=give 202 x%d", g_giveCount);
    ImGui::SameLine();
    if (ImGui::Button("纠缠之缘 223", ImVec2(120, 0)))
        action("c=command&text=give 223 x%d", g_giveCount);

    if (ImGui::Button("清空背包", ImVec2(120, 0))) action("c=command&text=clear all");
    ImGui::SameLine();
    if (ImGui::Button("清空武器", ImVec2(120, 0))) action("c=command&text=clear weapons");
    ImGui::SameLine();
    if (ImGui::Button("清空圣遗物", ImVec2(120, 0))) action("c=command&text=clear artifacts");
    ImGui::SameLine();
    if (ImGui::Button("重置商店限购", ImVec2(130, 0))) action("c=command&text=resetShopLimit");

    ImGui::Spacing();
    sectionHeader("角色");
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("角色 ID", &g_avatarId, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("等级", &g_avatarLvl, 1, 10);
    if (ImGui::Button("添加角色", ImVec2(110, 0)))
        action("c=command&text=avatar add %d lv%d", g_avatarId, g_avatarLvl);
    ImGui::SameLine();
    if (ImGui::Button("替换出场角色", ImVec2(140, 0)))
        action("c=command&text=avatar set %d", g_avatarId);
    ImGui::SameLine();
    if (ImGui::Button("加入队伍", ImVec2(110, 0)))
        action("c=command&text=team add %d", g_avatarId);
    ImGui::SameLine();
    if (ImGui::Button("满血", ImVec2(80, 0))) action("c=command&text=restore");

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("属性 ID", &g_propId, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputFloat("值", &g_propVal, 0.1f, 1.f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("prop 设置属性", ImVec2(140, 0)))
        action("c=command&text=prop %d %.2f", g_propId, g_propVal);
    ImGui::SameLine();
    if (ImGui::Button("切换元素", ImVec2(110, 0))) action("c=command&text=switchelement 1");

    ImGui::Spacing();
    sectionHeader("世界 / 刷怪 / 传送");
    ImGui::SetNextItemWidth(150);
    ImGui::InputInt("怪物 ID", &g_spawnId, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("等级##m", &g_spawnLvl, 1, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("数量##m", &g_spawnCount, 1, 10);
    if (ImGui::Button("spawn 刷怪", ImVec2(130, 0)))
        action("c=command&text=spawn %d %d %d", g_spawnId, g_spawnLvl, g_spawnCount);
    ImGui::SameLine();
    if (ImGui::Button("kill all 清怪", ImVec2(130, 0))) action("c=command&text=kill all");
    ImGui::SameLine();
    if (ImGui::Button("回七天神像", ImVec2(120, 0))) action("c=command&text=statue");

    ImGui::SetNextItemWidth(130);
    ImGui::InputInt("副本 ID", &g_dungeonId, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("进入副本", ImVec2(110, 0)))
        action("c=command&text=dungeon %d", g_dungeonId);
    ImGui::SameLine();
    if (ImGui::Button("晴##w", ImVec2(50, 0))) action("c=weather&id=1&area=1");
    ImGui::SameLine();
    if (ImGui::Button("阴##w", ImVec2(50, 0))) action("c=weather&id=2&area=1");
    ImGui::SameLine();
    if (ImGui::Button("雨##w", ImVec2(50, 0))) action("c=weather&id=3&area=1");
    ImGui::SameLine();
    if (ImGui::Button("雷##w", ImVec2(50, 0))) action("c=weather&id=4&area=1");
    ImGui::SameLine();
    if (ImGui::Button("雪##w", ImVec2(50, 0))) action("c=weather&id=5&area=1");

    ImGui::Spacing();
    sectionHeader("进度");
    ImGui::SetNextItemWidth(130);
    ImGui::InputInt("任务 ID", &g_questId, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("quest 完成", ImVec2(110, 0)))
        action("c=command&text=quest %d finish", g_questId);
    ImGui::SameLine();
    if (ImGui::Button("成就全领", ImVec2(110, 0))) action("c=command&text=achievement");
    ImGui::SameLine();
    if (ImGui::Button("每日任务", ImVec2(110, 0))) action("c=command&text=dailytask");

    ImGui::Spacing();
    sectionHeader("管理 / 聊天");
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("消息", g_msg, sizeof(g_msg));
    if (ImGui::Button("全服公告", ImVec2(110, 0)))
        action("c=command&text=announce %s", g_msg);
    ImGui::SameLine();
    if (ImGui::Button("说话", ImVec2(90, 0))) action("c=command&text=say %s", g_msg);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("UID", &g_uid, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("踢下线", ImVec2(90, 0))) action("c=command&text=kick %d", g_uid);
    ImGui::SameLine();
    if (ImGui::Button("封禁", ImVec2(80, 0))) action("c=command&text=ban %d", g_uid);
    ImGui::SameLine();
    if (ImGui::Button("解封", ImVec2(80, 0))) action("c=command&text=unban %d", g_uid);
    if (ImGui::Button("热重载", ImVec2(110, 0))) action("c=command&text=reload");

    ImGui::Spacing();
    sectionHeader("其余命令（点一下直接执行）");
    const char* others[] = {"barrier", "coop", "cutscene", "debug", "entity",   "group",
                            "info",    "language", "lookup", "name", "namecard", "npc",
                            "permission", "player", "position", "proto", "setSceneTag",
                            "sgv",     "sound", "support", "teapot", "trackmat", "trial",
                            "windy",   "dps",  "help", "account", "battlepass", "openstate"};
    for (int i = 0; i < (int)(sizeof(others) / sizeof(others[0])); ++i) {
        if (ImGui::Button(others[i], ImVec2(118, 0))) action("c=command&text=%s", others[i]);
        if ((i + 1) % 4 != 0) ImGui::SameLine();
    }
    (void)s;
}

void tabSettings(const Snapshot& s) {
    static int binding = -1;   // 正在等按键的那一行
    sectionHeader("作弊快捷键（点“绑定”后按任意键，Esc 取消）");
    for (int i = 0; i < g_hotkeyCount; ++i) {
        ImGui::Text("%s", g_hotkeys_cheat[i].name);
        ImGui::SameLine(150);
        ImGui::TextDisabled("%s", vkName(*g_hotkeys_cheat[i].vk));
        ImGui::SameLine(250);
        char idb[32];
        snprintf(idb, sizeof(idb), "绑定##%d", i);
        if (binding == i) {
            if (ImGui::Button("按键中...##cancel", ImVec2(120, 0))) binding = -1;
        } else if (ImGui::Button(idb, ImVec2(120, 0))) {
            binding = i;
        }
        ImGui::SameLine();
        char idc[32];
        snprintf(idc, sizeof(idc), "清除##%d", i);
        if (ImGui::Button(idc, ImVec2(80, 0))) {
            *g_hotkeys_cheat[i].vk = 0;
            if (binding == i) binding = -1;
        }
        if (binding == i) {
            for (int vk = 1; vk < 256; ++vk) {
                if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
                if ((GetAsyncKeyState(vk) & 0x8000) == 0) continue;
                if (vk == VK_ESCAPE) {
                    binding = -1;
                } else {
                    *g_hotkeys_cheat[i].vk = vk;
                    binding = -1;
                }
                break;
            }
        }
        if ((i + 1) % 1 == 0) ImGui::Separator();
    }

    sectionHeader("调试（开发者）");
    if (ImGui::Button("导出 IL2CPP 元数据", ImVec2(200, 0))) Gs71DumpMetadata();
    ImGui::SameLine();
    ImGui::TextDisabled("从内存抓解密后的元数据，给 Il2CppDumper 用");
    ImGui::TextDisabled("输出: D:\\Programs\\gs71-runtime\\il2cppdumper\\out\\metadata_dumped.dat");

    sectionHeader("界面");

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
    sectionHeader("快捷键（仅游戏窗口在前台时生效）");
    {
        static const char* rows[][2] = {
            {"F1", "NoClip 真·自由移动"}, {"F2", "速度覆写飞行"}, {"F3", "元素视野"},
            {"F4", "ESP 透视"},           {"F5", "自动奔跑"},     {"F6", "跳过过场"},
            {"F7", "隐藏 UI"},            {"F8", "无敌"},         {"F9", "杀戮光环"},
            {"F10", "时间冻结"},          {"F11", "自动拾取"},    {"F12", "无限体力"},
            {"[ / ]", "飞行速度 -/+"},    {"INSERT", "开关菜单"}, {"V", "NoClip 快捷开关"},
        };
        for (const auto& r : rows) {
            ImGui::TextColored(ImVec4(0.74f, 0.62f, 1.0f, 1.f), "%-7s", r[0]);
            ImGui::SameLine(92);
            ImGui::TextUnformatted(r[1]);
        }
        ImGui::TextDisabled("切到后台/别的窗口后，这些键不再触发");
    }

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
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.23f, 0.29f, 1.00f);
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

    style.WindowRounding = 10.f;
    style.ChildRounding = 8.f;
    style.FrameRounding = 6.f;
    style.GrabRounding = 6.f;
    style.PopupRounding = 6.f;
    style.FrameBorderSize = 1.f;
    style.WindowBorderSize = 1.f;
    style.ItemSpacing = ImVec2(10, 8);
    style.FramePadding = ImVec2(10, 6);
    style.WindowPadding = ImVec2(14, 12);
    style.ScrollbarSize = 12.f;
}

}  // namespace

namespace menu {

// ---------------------------------------------------------------- hotkeys
//
// å¨é¨å¿«æ·é®é½ç» gs71InputFocused() é¨æ§ï¼æ¸¸æçªå£ä¸å¨åå°æ¶ä¸å¾ä¸çæã
struct HotkeyDef {
    int vk;
    const char* key;
    const char* what;
};

static const HotkeyDef kHotkeyTable[] = {
    {VK_F1, "F1", "NoClip çÂ·èªç±ç§»å¨"},
    {VK_F2, "F2", "éåº¦è¦åé£è¡"},
    {VK_F3, "F3", "åç´ è§é"},
    {VK_F4, "F4", "ESP éè§"},
    {VK_F5, "F5", "èªå¨å¥è·"},
    {VK_F6, "F6", "è·³è¿è¿åº"},
    {VK_F7, "F7", "éè UI"},
    {VK_F8, "F8", "æ æ God Mode"},
    {VK_F9, "F9", "ææ®åç¯"},
    {VK_F10, "F10", "æ¶é´å»ç»"},
    {VK_F11, "F11", "èªå¨æ¾å"},
    {VK_F12, "F12", "æ éä½å"},
};
static const int kHotkeyCount = (int)(sizeof(kHotkeyTable) / sizeof(kHotkeyTable[0]));
static bool g_hkPrev[16] = {false};
static bool g_hkSkipCut = false;
static bool g_hkHideUi = false;

void tickHotkeys() {
    if (!gs71InputFocused()) {
        for (int i = 0; i < 16; ++i) g_hkPrev[i] = false;
        return;
    }
    for (int i = 0; i < kHotkeyCount && i < 16; ++i) {
        const bool down = (::GetAsyncKeyState(kHotkeyTable[i].vk) & 0x8000) != 0;
        if (down && !g_hkPrev[i]) {
            switch (i) {
                case 0: Gs71FlyMode(Gs71FlyModeGet() == 9 ? 0 : 9); Gs71Log("F1 noclip"); break;
                case 1: Gs71FlySet(!Gs71FlyGet()); break;
                case 2: Gs71ElemView(!Gs71ElemViewGet()); break;
                case 3: g_cfg.espWorld = !g_cfg.espWorld; Gs71Log(g_cfg.espWorld ? "F4 esp on" : "F4 esp off"); break;
                case 4: Gs71FlyMode(Gs71FlyModeGet() == 3 ? 0 : 3); break;
                case 5: g_hkSkipCut = !g_hkSkipCut; Gs71CutsceneSkip(g_hkSkipCut); break;
                case 6: g_hkHideUi = !g_hkHideUi; Gs71HideUi(g_hkHideUi); break;
                case 7: g_st.god = !g_st.god; push("god=%d", g_st.god ? 1 : 0); Gs71Log(g_st.god ? "F8 god on" : "F8 god off"); break;
                case 8: g_st.aura = !g_st.aura; push("aura=%d", g_st.aura ? 1 : 0); break;
                case 9: g_st.freeze = !g_st.freeze; push("freeze=%d", g_st.freeze ? 1 : 0); break;
                case 10: g_st.loot = !g_st.loot; push("loot=%d", g_st.loot ? 1 : 0); break;
                case 11: g_st.stamina = !g_st.stamina; push("stamina=%d", g_st.stamina ? 1 : 0); break;
                default: break;
            }
        }
        g_hkPrev[i] = down;
    }
}

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
    fprintf(f, "espWorld=%d\n", g_cfg.espWorld ? 1 : 0);
    fprintf(f, "espStyle=%d\n", g_cfg.espStyle);
    fprintf(f, "espMobs=%d\n", g_cfg.espMobs ? 1 : 0);
    fprintf(f, "espGadgets=%d\n", g_cfg.espGadgets ? 1 : 0);
    fprintf(f, "espItems=%d\n", g_cfg.espItems ? 1 : 0);
    fprintf(f, "chestArrow=%d\n", g_cfg.chestArrow ? 1 : 0);
    fprintf(f, "theme=%d\n", g_cfg.theme);
    fprintf(f, "hkgod=%d\n", g_cfg.hkGod);
    fprintf(f, "hkfall=%d\n", g_cfg.hkFall);
    fprintf(f, "hkloot=%d\n", g_cfg.hkLoot);
    fprintf(f, "hkstamina=%d\n", g_cfg.hkStamina);
    fprintf(f, "hkautoheal=%d\n", g_cfg.hkAutoheal);
    fprintf(f, "hkaura=%d\n", g_cfg.hkAura);
    fprintf(f, "hkfreeze=%d\n", g_cfg.hkFreeze);
    fprintf(f, "hkesp=%d\n", g_cfg.hkEsp);
    fprintf(f, "hkmobvac=%d\n", g_cfg.hkMobVac);
    fprintf(f, "hkburst=%d\n", g_cfg.hkBurst);
    fprintf(f, "hkskill=%d\n", g_cfg.hkSkill);
    fprintf(f, "hkspeed=%d\n", g_cfg.hkSpeed);
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
        else if (!strcmp(k, "espWorld")) g_cfg.espWorld = isOn(v);
        else if (!strcmp(k, "espStyle")) g_cfg.espStyle = atoi(v);
        else if (!strcmp(k, "espMobs")) g_cfg.espMobs = isOn(v);
        else if (!strcmp(k, "espGadgets")) g_cfg.espGadgets = isOn(v);
        else if (!strcmp(k, "espItems")) g_cfg.espItems = isOn(v);
        else if (!strcmp(k, "chestArrow")) g_cfg.chestArrow = isOn(v);
        else if (!strcmp(k, "theme")) g_cfg.theme = atoi(v);
        else if (!strcmp(k, "hkgod")) g_cfg.hkGod = atoi(v);
        else if (!strcmp(k, "hkfall")) g_cfg.hkFall = atoi(v);
        else if (!strcmp(k, "hkloot")) g_cfg.hkLoot = atoi(v);
        else if (!strcmp(k, "hkstamina")) g_cfg.hkStamina = atoi(v);
        else if (!strcmp(k, "hkautoheal")) g_cfg.hkAutoheal = atoi(v);
        else if (!strcmp(k, "hkaura")) g_cfg.hkAura = atoi(v);
        else if (!strcmp(k, "hkfreeze")) g_cfg.hkFreeze = atoi(v);
        else if (!strcmp(k, "hkresp")) g_cfg.hkEsp = atoi(v);
        else if (!strcmp(k, "hkesp")) g_cfg.hkEsp = atoi(v);
        else if (!strcmp(k, "hkmobvac")) g_cfg.hkMobVac = atoi(v);
        else if (!strcmp(k, "hkburst")) g_cfg.hkBurst = atoi(v);
        else if (!strcmp(k, "hkskill")) g_cfg.hkSkill = atoi(v);
        else if (!strcmp(k, "hkspeed")) g_cfg.hkSpeed = atoi(v);
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

// ================================================================ Akebi 移植菜单
// 按 Akebi 的 5 个分组（Player / World / Visuals / Teleport / Misc）重排，
// 每个条目后面标注移植状态：✅ 已接入  ○ 未移植（附 Akebi 源码位置）

static void rowTodo(const char* label, const char* hint) {
    ImGui::TextDisabled("  ○ %s", label);
    if (hint && *hint) {
        ImGui::SameLine();
        ImGui::TextDisabled("— %s", hint);
    }
}

// 往 D:\Programs\gs71-runtime\cmd.now 写一行 key=value（overlay 2 秒内消费）
static void SetFileFlag(const char* line) {
    FILE* f = fopen("D:\\Programs\\gs71-runtime\\cmd.now", "w");
    if (!f) return;
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
}

// 创建 xxx.now 触发文件
static void TouchNow(const char* name) {
    char p[MAX_PATH];
    snprintf(p, sizeof(p), "D:\\Programs\\gs71-runtime\\%s", name);
    FILE* f = fopen(p, "wb");
    if (f) fclose(f);
}

static void akPlayer(const Snapshot& s) {
    (void)s;
    sectionHeader("Player · 玩家");
    toggleRow("无敌 God Mode", &g_st.god, "god");
    toggleRow("无限体力 Infinite Stamina", &g_st.stamina, "stamina");
    toggleRow("无摔落 Fall Control", &g_st.fall, "fall");
    toggleRow("自动回血 Auto Heal", &g_st.autoheal, "autoheal");

    ImGui::Spacing();
    sectionHeader("No Clip · 自由移动");
    {
        static bool noclip = false;
        if (ImGui::Checkbox("真·自由移动 NoClip（写位置，V 键）", &noclip)) Gs71FlyMode(noclip ? 9 : 0);
        ImGui::SameLine();
        ImGui::TextDisabled("kinematic + MovePositionDelta，穿墙");
        bool fly = Gs71FlyGet();
        if (ImGui::Checkbox("速度覆写飞行（旧方案）", &fly)) Gs71FlySet(fly);
        if (fly) {
            float sp = Gs71FlySpeedGet();
            ImGui::SetNextItemWidth(240);
            if (ImGui::SliderFloat("速度##flysp", &sp, 1.f, 40.f, "%.1f m/s")) Gs71FlySpeedSet(sp);
            ImGui::TextDisabled("WASD 移动 · 空格上升 · Ctrl 慢降 · [ ] 调速");
        }
    }

    ImGui::Spacing();
    sectionHeader("战斗辅助");
    sliderRowF("伤害倍率", &g_st.mult, 1.f, 1000.f, "x%.1f", "mult");
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt("多段伤害段数", &g_hits, 1, 5)) {
        if (g_hits < 1) g_hits = 1;
        if (g_hits > 30) g_hits = 30;
        push("hits=%d", g_hits);
    }
    toggleRow("无限大招 Infinite Burst", &g_cfg.burst);
    toggleRow("技能可用 Skill Ready", &g_cfg.skillReady);
    rowTodo("无 CD NoCD", "Akebi 钩 DataItem::HandleNormalProp");
    rowTodo("快速射击 RapidFire", "Akebi 钩 avatar 攻速");
    {
        static bool autoRun = false;
        if (ImGui::Checkbox("自动奔跑 Auto Run", &autoRun)) Gs71FlyMode(autoRun ? 3 : 0);
        ImGui::SameLine();
        ImGui::TextDisabled("沿镜头方向持续前进");
    }
}

static void akWorld(const Snapshot& s) {
    (void)s;
    sectionHeader("拾取 / 吸附");
    toggleRow("自动拾取 Auto Loot", &g_st.loot, "loot");
    toggleRow("吸怪 Mob Vacuum", &g_cfg.mobVac);
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("吸怪半径", &g_cfg.mobVacR, 5.f, 80.f, "%.0f m"))
        push("mobvarr=%.0f", g_cfg.mobVacR);
    sliderRowF("拾取半径", &g_st.lootR, 5.f, 120.f, "%.0f m", "lootr");
    rowTodo("吸物 VacuumLoot", "Akebi 遍历掉落物连点");

    ImGui::Spacing();
    sectionHeader("敌人");
    toggleRow("冻结时间 Time Freeze", &g_st.freeze, "freeze");
    rowTodo("冻结敌人 Freeze Enemies", "服务端没有，需客户端实体枚举");
    rowTodo("笨蛋敌人 Dumb Enemies", "Akebi 钩 VCMonsterAIController::TryDoSkill");
    toggleRow("击杀光环 Kill Aura", &g_st.aura, "aura");
    sliderRowF("光环半径", &g_st.auraR, 3.f, 40.f, "%.0f m", "aurar");

    ImGui::Spacing();
    sectionHeader("自动解谜 / 交互");
    ImGui::Checkbox("自动交互（宝箱/机关）", &g_autoInt);
    if (ImGui::Button("触发一次广域交互", ImVec2(180, 0))) action("c=interactnow");
    ImGui::SameLine();
    ImGui::TextDisabled("服务端 onInteract · 3 秒冷却");
    {
        static bool elView = false;
        if (ImGui::Checkbox("元素视野 Elemental Sight", &elView)) Gs71ElemView(elView);
        ImGui::SameLine();
        ImGui::TextDisabled("走游戏自己的 UI 开关（Toggle.isOn）");
    }
    rowTodo("自动仙灵 AutoSeelie / 自动挑战 AutoChallenge", "需要客户端实体枚举");

    ImGui::Spacing();
    sectionHeader("环境");
    {
        static float gs = 1.f;
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("游戏速度", &gs, 0.2f, 5.f, "%.2fx")) {
            char q[64];
            snprintf(q, sizeof(q), "timescale=%.2f", gs);
            SetFileFlag(q);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("写 cmd.now 即时生效");
    }
    ImGui::TextDisabled("天气/时间走服务端：");
    ImGui::SameLine();
    if (ImGui::Button("晴")) action("c=weather&id=1&area=1");
    ImGui::SameLine();
    if (ImGui::Button("雨")) action("c=weather&id=3&area=1");
    ImGui::SameLine();
    if (ImGui::Button("雷")) action("c=weather&id=4&area=1");
    {
        static bool skipCut = false;
        if (ImGui::Checkbox("跳过过场动画 Skip Cutscenes", &skipCut)) Gs71CutsceneSkip(skipCut);
        static float dt = 1.f;
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("对话加速", &dt, 1.f, 5.f, "%.1fx")) {
            char q[64];
            snprintf(q, sizeof(q), "timescale=%.2f", dt);
            SetFileFlag(q);
        }
    }
    rowTodo("跳过对话 DialogSkip（自动选对话）", "Akebi 钩 InLevelCutScenePageContext");
}

static void akVisuals(const Snapshot& s) {
    sectionHeader("ESP");
    toggleRow("实体数据流 (ESP data)", &g_st.esp, "esp");
    ImGui::Checkbox("世界投影 ESP", &g_cfg.espWorld);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##espdist2", &g_cfg.espMaxDist, 20.f, 300.f, "显示范围 %.0f m");
    if (g_cfg.espWorld) {
        int style = g_cfg.espStyle;
        if (ImGui::RadioButton("点", style == 0)) g_cfg.espStyle = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("方框", style == 1)) g_cfg.espStyle = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("方框+连线+血条", style == 2)) g_cfg.espStyle = 2;
        ImGui::Checkbox("怪物", &g_cfg.espMobs);
        ImGui::SameLine();
        ImGui::Checkbox("机关/宝箱", &g_cfg.espGadgets);
        ImGui::SameLine();
        ImGui::Checkbox("掉落", &g_cfg.espItems);
    }
    toggleRow("屏幕雷达环", &g_cfg.entityHud);
    toggleRow("附近实体列表", &g_cfg.nearList);
    rowTodo("宝箱指示 ShowChestIndicator", "Akebi 引导箭头");

    ImGui::Spacing();
    sectionHeader("画面");
    {
        static bool hideUi = false;
        if (ImGui::Checkbox("隐藏游戏 UI", &hideUi)) Gs71HideUi(hideUi);
        ImGui::SameLine();
        ImGui::TextDisabled("Canvas.enabled");
    }
    if (ImGui::Button("去雾 / 恢复", ImVec2(150, 0))) SetFileFlag("fog=1");
    ImGui::SameLine();
    if (ImGui::Button("视野 45", ImVec2(90, 0))) SetFileFlag("fov=45");
    ImGui::SameLine();
    if (ImGui::Button("视野 90", ImVec2(90, 0))) SetFileFlag("fov=90");
    ImGui::SameLine();
    if (ImGui::Button("恢复 60", ImVec2(90, 0))) SetFileFlag("fov=60");
    {
        static float fov = 60.f;
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderFloat("相机视野 FOV", &fov, 20.f, 120.f, "%.0f°")) {
            char q[64];
            snprintf(q, sizeof(q), "fov=%.0f", fov);
            SetFileFlag(q);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("等效相机缩放");
    }
    rowTodo("相机缩放 CameraZoom（改 distance 字段）", "缺 SCameraModuleInitialize");
    rowTodo("自由相机 FreeCamera", "之前实验过，未成");
    rowTodo("帕蒙跟随 / 立绘替换 / 贴图替换", "Akebi visuals 组其余项");

    ImGui::Spacing();
    sectionHeader("HUD");
    toggleRow("水印 Watermark", &g_cfg.watermark);
    toggleRow("坐标显示", &g_cfg.coords);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##hudscale2", &g_cfg.hudScale, 0.6f, 2.0f, "HUD 缩放 %.2f");
}

static void akTeleport(const Snapshot& s) {
    sectionHeader("传送 · Teleport");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat3("##tp2", g_tp, 1.0f, -10000.f, 10000.f, "%.1f");
    if (ImGui::Button("传送到坐标", ImVec2(-1, 0)))
        action("c=tp&x=%.2f&y=%.2f&z=%.2f", g_tp[0], g_tp[1], g_tp[2]);
    if (ImGui::Button("用当前位置填充", ImVec2(-1, 0))) {
        g_tp[0] = s.px;
        g_tp[1] = s.py;
        g_tp[2] = s.pz;
    }
    ImGui::TextDisabled("注意：服务端传送到远处可能触发『区域以后再来探索』");

    ImGui::Spacing();
    actionQuery("传送到最近宝箱 ChestTeleport", "c=chesttp");
    actionQuery("按 id 段 → 最近宝箱", "c=gadgettp&idmin=70200000&idmax=70300000");
    actionQuery("按 id 段 → 最近采集物", "c=gadgettp&idmin=70500000&idmax=70600000");
    actionQuery("按 id 段 → 最近机关", "c=gadgettp&idmin=70360000&idmax=70370000");
    ImGui::TextDisabled("远距离传送会掉进未加载区域，掉了用「回到蒙德」救回来");
    actionQuery("列出已解锁锚点", "c=anchors");
    if (ImGui::Button("回到锚点 0", ImVec2(150, 0))) action("c=goanchor&i=0");
    ImGui::SameLine();
    if (ImGui::Button("回到神像", ImVec2(120, 0))) action("c=command&text=statue");
    ImGui::SameLine();
    if (ImGui::Button("回到蒙德", ImVec2(120, 0))) action("c=tp&x=2236&y=212&z=-915");
    rowTodo("神瞳传送 OculiTeleport", "Akebi 按神瞳点位列表传送");
    rowTodo("点哪传哪 TransmitRequest", "钩子已挂好，未抓到结构");

    ImGui::Spacing();
    sectionHeader("书签 / 场景");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##bm2", g_bookmark, sizeof(g_bookmark));
    if (ImGui::Button("保存当前点位", ImVec2(-1, 0))) action("c=bookmark&name=%s", g_bookmark);
    if (ImGui::Button("传送到书签", ImVec2(-1, 0))) action("c=go&name=%s", g_bookmark);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("场景 ID", &g_sceneId, 0, 0);
    if (ImGui::Button("切换场景", ImVec2(-1, 0))) action("c=scene&id=%d", g_sceneId);
}

static void akMisc(const Snapshot& s) {
    (void)s;
    sectionHeader("GM 命令");
    if (ImGui::Button("解锁全部地图点", ImVec2(190, 0))) action("c=command&text=unlock all");
    ImGui::SameLine();
    if (ImGui::Button("解锁传送点", ImVec2(150, 0))) action("c=command&text=waypoints");
    ImGui::SetNextItemWidth(110);
    ImGui::InputInt("物品 ID", &g_giveId, 0, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("数量", &g_giveCount, 1000, 100000);
    ImGui::SameLine();
    if (ImGui::Button("give", ImVec2(80, 0)))
        action("c=command&text=give %d x%d", g_giveId, g_giveCount);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("怪物 ID", &g_spawnId, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("spawn 刷怪", ImVec2(120, 0)))
        action("c=command&text=spawn %d", g_spawnId);
    ImGui::SameLine();
    if (ImGui::Button("kill all", ImVec2(110, 0))) action("c=command&text=kill all");

    ImGui::Spacing();
    sectionHeader("诊断 / 探针");
    if (ImGui::Button("导出 IL2CPP 元数据", ImVec2(200, 0))) Gs71DumpMetadata();
    if (ImGui::Button("UI 候选名探测", ImVec2(200, 0))) TouchNow("hideui_probe.now");
    ImGui::SameLine();
    if (ImGui::Button("Lua 实例探针", ImVec2(160, 0))) TouchNow("lua_probe.now");
    ImGui::SameLine();
    ImGui::TextDisabled("见 lua_probe.now / tx_probe.now");

    ImGui::Spacing();
    sectionHeader("状态");
    ImGui::Text("UID %d   场景 %d", s.uid, s.scene);
    ImGui::Text("坐标 %.1f, %.1f, %.1f", s.px, s.py, s.pz);
    if (s.maxHp > 0) {
        char hpText[64];
        snprintf(hpText, sizeof(hpText), "%.0f / %.0f", s.hp, s.maxHp);
        ImGui::Text("生命 %s", hpText);
        ImGui::ProgressBar(s.maxHp > 0 ? s.hp / s.maxHp : 0.f, ImVec2(-1, 6), "");
    }
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

    pollCheatHotkeys();


    // T 键：传送到地图上扎的针（边沿触发）
    if (g_mapT) {
        const bool tdown = (GetAsyncKeyState('T') & 0x8000) != 0;
        if (tdown && !g_mapTDown) action("c=mapgo");
        g_mapTDown = tdown;
    }

    // tkey report：把 T 键的按下状态报给服务端
    // 服务端逻辑：按住 T 时在地图上扎针 → 直接传送过去，并把针删掉（地图保持干净）
    {
        static bool lastTState = false;
        const bool tHeld = (GetAsyncKeyState('T') & 0x8000) != 0;
        if (tHeld != lastTState) {
            lastTState = tHeld;
            bridge::set(tHeld ? "tkey=1" : "tkey=0");
        }
    }

    Snapshot s = bridge::get();
    syncFromServer(s);
    applyTheme();

    const int vk[] = {VK_INSERT, VK_DELETE};
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
        case TAB_PLAYER: akPlayer(s); break;
        case TAB_WORLD: akWorld(s); break;
        case TAB_VISUAL: akVisuals(s); break;
        case TAB_TELEPORT: akTeleport(s); break;
        case TAB_GM: akMisc(s); break;
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

    // ---- 世界投影 ESP -------------------------------------------------------
    // 坐标来自服务端实体流，投影用客户端相机的 forward + FOV 自己算（不调 WorldToScreenPoint，
    // 那个函数的 Vector3 传参 ABI 没验证过，猜错会崩）。
    static int lastProjFails = -1;
    static int lastProjDrawn = -1;
    if (g_cfg.espWorld && !s.ents.empty()) {
        const float vw = io.DisplaySize.x;
        const float vh = io.DisplaySize.y;
        int drawn = 0, failed = 0;

        // 先投影、按距离排序，只画最近的 40 个：服务端会下发上百个实体，全画只会糊屏
        struct ProjEnt { const EntInfo* e; float sx, sy; char kind; };
        static std::vector<ProjEnt> hits;
        hits.clear();
        for (const auto& e : s.ents) {
            if (e.dist > g_cfg.espMaxDist) continue;
            if (e.dist < 2.0f) continue;   // 自己，别画

            // 宝箱：Grasscutter 里宝箱的 gadgetId 在 70200000~70300000
            char kind = e.kind;
            if (kind == 'G' && e.typeId >= 70200000 && e.typeId < 70300000) kind = 'C';

            if (kind == 'M' && !g_cfg.espMobs) continue;
            if ((kind == 'G' || kind == 'C') && !g_cfg.espGadgets) continue;
            if (kind == 'I' && !g_cfg.espItems) continue;
            float sx = 0.f, sy = 0.f;
            if (!Gs71Project(e.x, e.y, e.z, &sx, &sy, vw, vh)) { ++failed; continue; }
            if (sx < -200.f || sy < -200.f || sx > vw + 200.f || sy > vh + 200.f) { ++failed; continue; }
            hits.push_back(ProjEnt{&e, sx, sy, kind});
        }
        std::sort(hits.begin(), hits.end(), [](const ProjEnt& a, const ProjEnt& b) {
            return a.e->dist < b.e->dist;
        });

        for (const auto& h : hits) {
            if (drawn >= 40) break;
            const EntInfo& e = *h.e;
            const float sx = h.sx, sy = h.sy;
            const char kind = h.kind;

            ImU32 col;
            switch (kind) {
                case 'M': col = IM_COL32(255, 90, 90, 235); break;
                case 'C': col = IM_COL32(255, 215, 90, 245); break;
                case 'O': col = IM_COL32(190, 130, 255, 245); break;
                case 'K': col = IM_COL32(120, 200, 255, 235); break;
                case 'G': col = IM_COL32(255, 205, 90, 230); break;
                case 'I': col = IM_COL32(110, 230, 140, 225); break;
                default:  col = IM_COL32(160, 170, 210, 210); break;
            }

            if (g_cfg.espStyle == 0) {
                ImGui::GetForegroundDrawList()->AddCircleFilled(ImVec2(sx, sy), 3.5f * scale, col, 12);
                ++drawn;
                continue;
            }

            float bh = 2400.f / (e.dist > 1.f ? e.dist : 1.f);
            if (bh > 220.f) bh = 220.f;
            if (bh < 14.f) bh = 14.f;
            const float w = bh * 0.45f;
            // 实体坐标是脚底：方框从脚往头上长
            const float top = sy - bh * 0.92f;
            const float bottom = sy + bh * 0.08f;
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddRect(ImVec2(sx - w * 0.5f, top), ImVec2(sx + w * 0.5f, bottom), col, 3.f, 0, 1.6f);

            if (g_cfg.espStyle == 2) {
                // 连线只给怪物：机关/掉落太多，全画会糊成一片
                if (kind == 'M')
                    fg->AddLine(ImVec2(vw * 0.5f, vh), ImVec2(sx, bottom),
                                IM_COL32(255, 120, 120, 90), 1.f);
                // 血条
                if (e.maxHp > 0.f) {
                    const float hp = e.hp / e.maxHp;
                    const float barX = sx - w * 0.5f - 6.f;
                    fg->AddRectFilled(ImVec2(barX, top), ImVec2(barX + 3.f, bottom),
                                      IM_COL32(20, 20, 26, 190), 1.f);
                    fg->AddRectFilled(ImVec2(barX, bottom - (bottom - top) * hp),
                                      ImVec2(barX + 3.f, bottom),
                                      IM_COL32(90, 235, 120, 235), 1.f);
                }
            }

            char label[64];
            const char* kindTxt = kind == 'M' ? "怪" : kind == 'O' ? "神瞳"
                                : kind == 'C' ? "宝箱"
                                : kind == 'K' ? "机关" : kind == 'I' ? "采集"
                                : kind == 'G' ? "机" : "?";
            if (kind == 'M' && e.dist < 90.f) snprintf(label, sizeof(label), "%s %d  %.0fm", kindTxt, e.level, e.dist);
            else snprintf(label, sizeof(label), "%s  %.0fm", kindTxt, e.dist);
            fg->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f * scale,
                        ImVec2(sx - w * 0.5f, top - 15.f * scale), col, label);
            ++drawn;
        }
        if (failed != lastProjFails || drawn != lastProjDrawn) {
            lastProjFails = failed;
            lastProjDrawn = drawn;
        }
        // 左下角一行小字：投影命中的实体数（排障用）
        {
            char line[96];
            snprintf(line, sizeof(line), "ESP %d 投影 / %d 跳过", drawn, failed);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f * scale,
                        ImVec2(20, io.DisplaySize.y - 26), IM_COL32(150, 155, 180, 190), line);
        }
    }

    // ---- ?????? -------------------------------------------------------
    if (g_cfg.chestArrow && !s.ents.empty()) {
        const EntInfo* best = nullptr;
        float bestD = 1e9f;
        for (const auto& e : s.ents) {
            char kind = e.kind;
            if (kind == 'G' && e.typeId >= 70200000 && e.typeId < 70300000) kind = 'C';
            if (kind != 'C') continue;
            if (e.dist < bestD) { bestD = e.dist; best = &e; }
        }
        if (best) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            float sx = 0.f, sy = 0.f;
            const float vw = io.DisplaySize.x, vh = io.DisplaySize.y;
            if (Gs71Project(best->x, best->y, best->z, &sx, &sy, vw, vh) &&
                sx > 0 && sx < vw && sy > 0 && sy < vh) {
                fg->AddCircle(ImVec2(sx, sy - 14.f * scale), 7.f * scale, IM_COL32(255, 215, 90, 240), 12, 2.f);
                char t[48];
                snprintf(t, sizeof(t), "?? %.0fm", best->dist);
                fg->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f * scale,
                            ImVec2(sx + 10.f, sy - 30.f * scale), IM_COL32(255, 225, 130, 240), t);
            } else {
                float deg = 0.f, dist = 0.f;
                if (Gs71Bearing(best->x, best->y, best->z, &deg, &dist)) {
                    const float cx = vw * 0.5f;
                    const float cy = vh - 96.f;
                    const float rad = deg * 3.14159265f / 180.f;
                    const float ax = cx + sinf(rad) * 46.f;
                    const float ay = cy - cosf(rad) * 46.f;
                    fg->AddTriangleFilled(ImVec2(ax, ay - 9.f), ImVec2(ax - 7.f, ay + 7.f),
                                          ImVec2(ax + 7.f, ay + 7.f), IM_COL32(255, 215, 90, 235));
                    char t[48];
                    snprintf(t, sizeof(t), "?? %.0fm", dist);
                    fg->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f * scale,
                                ImVec2(cx - 26.f, cy + 12.f), IM_COL32(255, 225, 130, 220), t);
                }
            }
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
