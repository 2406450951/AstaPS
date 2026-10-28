// menu.h - Akebi-style in-game menu drawn over the game by ImGui.
#pragma once

#include <string>

namespace menu {

// Draws the menu. Call between ImGui::NewFrame() and ImGui::Render().
void draw();

// Draws the always-on HUD (watermark / nearby entities), independent of the menu.
void drawHud();

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
