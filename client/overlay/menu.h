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
};
ClientConfig& config();

}  // namespace menu
