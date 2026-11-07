// menu.h - Akebi-style in-game menu drawn over the game by ImGui.
#pragma once

#include <string>

namespace menu {

// Draws the menu. Call between ImGui::NewFrame() and ImGui::Render().
void draw();

// Draws the always-on HUD (watermark / nearby entities), independent of the menu.
void drawHud();

// å¿«æ·é®å¤çï¼æ¯å¸§è°ç¨ï¼å·²åç¦ç¹é¨æ§ï¼
void tickHotkeys();

bool isOpen();
void setOpen(bool open);
void toggle();

// Hotkey label, e.g. "INSERT".
const char* hotkeyName();

// Free camera / speed toggles live client-side; keep them here so the settings tab can edit them.
struct ClientConfig {
    bool watermark = true;
    bool coords = true;
    bool entityHud = true;
    bool nearList = false;
    float hudScale = 1.0f;
    float espMaxDist = 120.0f;
    bool glow = true;
    float accent[3] = {0.62f, 0.45f, 0.98f};
    int theme = 0;
    // 世界投影 ESP（客户端侧：服务端实体流坐标 + 相机投影，不读内存）
    bool espWorld = true;
    int espStyle = 2;      // 0=点 1=方框 2=方框+连线+血条
    bool espMobs = true;   // ESP 只显示怪物
    bool espGadgets = true;// 机关/宝箱类
    bool espItems = true;  // 掉落物
    bool chestArrow = true;// 宝箱指引箭头
    // 新增：服务端侧的三个开关，存进 gs71_menu.cfg
    bool mobVac = false;
    float mobVacR = 20.0f;
    bool burst = false;
    bool skillReady = false;

    // 每项作弊的快捷键（0 = 未绑定）
    int hkGod = 0, hkFall = 0, hkLoot = 0, hkStamina = 0, hkAutoheal = 0;
    int hkAura = 0, hkFreeze = 0, hkEsp = 0, hkMobVac = 0, hkBurst = 0;
    int hkSkill = 0, hkSpeed = 0;
};
ClientConfig& config();

}  // namespace menu
