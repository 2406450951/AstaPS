// bridge.h - HTTP link between the in-game overlay and the Grasscutter CheatMenu plugin.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct EntInfo {
    char kind = 'E';
    int id = 0;
    int typeId = 0;
    float x = 0, y = 0, z = 0;
    float hp = 0, maxHp = 0;
    int level = 0;
    float dist = 0;
};

struct CheatState {
    bool god = false;
    bool fall = false;
    bool loot = false;
    bool stamina = false;
    bool autoheal = false;
    bool aura = false;
    bool freeze = false;
    bool esp = false;
    float mult = 1.f;
    float auraR = 12.f;
    float auraDmg = 30000.f;
    float lootR = 25.f;
    int timeH = 12;
    int timeM = 0;
    int weather = -1;
};

struct Snapshot {
    bool valid = false;
    bool online = false;
    int players = 0;
    int uid = 0;
    std::string nick;
    int scene = 0;
    float px = 0, py = 0, pz = 0;
    float rx = 0, ry = 0, rz = 0;
    float hp = 0, maxHp = 0;
    int worldHours = 0;
    std::vector<EntInfo> ents;
    CheatState st;
    std::string lastAction;
    long long serverUptimeMs = 0;
};

namespace bridge {

void start();
void stop();

// Thread-safe copy of the most recent snapshot.
Snapshot get();

// Fire-and-forget helpers. `query` is a form-encoded string like "god=1&mult=5".
void set(const std::string& query);
void cmd(const std::string& query);

// True when the last poll answered.
bool connected();
// Milliseconds since the last successful poll.
long long ageMs();

}  // namespace bridge
