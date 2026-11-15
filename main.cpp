#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cstdio>

// ----------------------------------------------------------------------------
// КОНСТАНТЫ И ТИПЫ ОРУЖИЯ / СКИНОВ / ДОСТИЖЕНИЙ
// ----------------------------------------------------------------------------
enum WeaponID { WPN_USP = 0, WPN_MP9, WPN_AK47, WPN_M4A1S, WPN_AWP, WPN_COUNT };
enum GrenadeID { GREN_HE = 0, GREN_FLASH, GREN_SMOKE };

struct WeaponSpec {
    const char* name;
    int price;
    int killAward;
    float damage;
    float headshotMult;
    float armorPen;
    float fireRate; // Выстрелов в секунду
    float recoilVertical;
    float recoilHorizontal;
    float moveSpread;
    float baseSpread;
    int magSize;
    int reserveMax;
    float reloadTime;
    float moveSpeedMult;
    bool canWallbang;
    bool isScoped;
    int soundType;
};

static const WeaponSpec WEAPONS[WPN_COUNT] = {
    {"USP-S Tactical", 200, 300, 34.0f, 3.5f, 0.55f, 6.5f, 1.1f, 0.35f, 0.035f, 0.004f, 12, 48, 1.8f, 1.00f, false, false, 2},
    {"MP9 Submachine", 1250, 600, 26.0f, 2.8f, 0.60f, 13.5f, 0.9f, 0.65f, 0.025f, 0.012f, 30, 120, 2.0f, 1.04f, false, false, 0},
    {"AK-47 Kinetic", 2700, 300, 36.0f, 4.0f, 0.78f, 9.8f, 2.1f, 0.85f, 0.085f, 0.003f, 30, 90, 2.4f, 0.92f, true, false, 1},
    {"M4A1-S Phantom", 2900, 300, 33.0f, 3.4f, 0.72f, 10.5f, 1.3f, 0.45f, 0.065f, 0.002f, 25, 75, 2.2f, 0.94f, true, false, 2},
    {"AWP Magnum", 4750, 100, 118.0f,4.0f, 0.97f, 0.8f, 6.0f, 0.20f, 0.140f, 0.000f, 5, 30, 3.2f, 0.78f, true, true, 3}
};

struct CareerProfile {
    int totalKills = 0;
    int headshotKills = 0;
    int trickKills = 0; // Wallbang + Ricochet
    int flawlessRounds = 0;
    int clutchWins = 0;
    int awpKills = 0;
    int grenadeKills = 0;
    int selectedSkin = 0;
};

struct SkinInfo {
    const char* name;
    const char* desc;
    Color bodyCol;
    Color accentCol;
    Color trimCol;
};

static const SkinInfo SKINS[6] = {
    {"Standard Issue", "Default Military Polymer & Steel", {38, 42, 48, 255}, {75, 80, 90, 255}, {25, 28, 32, 255}},
    {"Crimson Carbon", "Unlock: 15 Headshot Kills", {24, 24, 28, 255}, {210, 25, 45, 255}, {90, 15, 25, 255}},
    {"Blue Gem Hardened", "Unlock: 5 Wallbang or Ricochet Kills", {45, 130, 235, 255}, {235, 185, 65, 255}, {125, 85, 210, 255}},
    {"Emerald Doppler", "Unlock: 3 Flawless Rounds (100 HP Win)", {12, 55, 38, 255}, {20, 225, 115, 255}, {8, 25, 20, 255}},
    {"Solar Hyper-Fade", "Unlock: Win a 1v2 or 1v3 Clutch Round", {145, 35, 195, 255}, {255, 60, 130, 255}, {255, 195, 25, 255}},
    {"Abyssal Singularity", "Unlock: 40 Kills + 10 AWP + 3 HE Kills", {18, 12, 34, 255}, {0, 245, 255, 255}, {190, 40, 255, 255}}
};

// ----------------------------------------------------------------------------
// ФИЗИЧЕСКИЕ ОБЪЕКТЫ И МИР
// ----------------------------------------------------------------------------
struct MapBox {
    Vector3 pos;
    Vector3 size;
    Color color;
    bool destructible;
    float hp;
};

struct DebrisParticle {
    Vector3 pos;
    Vector3 vel;
    Vector3 rot;
    Vector3 rotVel;
    float size;
    Color color;
    float life;
    bool isShell;
};

struct BulletTracer {
    Vector3 start;
    Vector3 end;
    Color color;
    float life;
};

struct ActiveGrenade {
    Vector3 pos;
    Vector3 vel;
    int type; // 0=HE, 1=Flash, 2=Smoke
    float timer;
    bool exploded;
    float smokeDuration;
    int ownerTeam; // 0=Player/Ally, 1=Enemy
};

struct KillFeedEntry {
    std::string killer;
    std::string victim;
    std::string weapon;
    bool headshot;
    bool wallbang;
    float timer;
    int killerTeam;
};

// ----------------------------------------------------------------------------
// ИГРОК И УМНЫЕ БОТЫ (FSM)
// ----------------------------------------------------------------------------
enum BotState { BOT_PATROL = 0, BOT_INVESTIGATE, BOT_ENGAGE, BOT_RETREAT };

struct Agent {
    std::string name;
    int team; // 0 = Alpha (Player Team), 1 = Omega (Enemy Bots)
    bool isPlayer;
    bool alive;
    Vector3 pos; // Позиция ног (y = 0 на полу)
    Vector3 vel;
    float yaw; // Радианы
    float pitch; // Радианы
    float hp;
    float armor;
    bool hasHelmet;
    bool crouching;

    WeaponID weapon;
    int magAmmo;
    int reserveAmmo;
    float fireCooldown;
    float reloadTimer;
    int recoilShots;
    float recoilResetTimer;

    // Инвентарь гранат
    int heCount;
    int flashCount;
    int smokeCount;

    // Параметры ИИ бота
    BotState state;
    Vector3 targetWaypoint;
    Vector3 lastKnownEnemyPos;
    float alertTimer;
    float strafeDir;
    float strafeTimer;
    float blindTimer;
    float burstTimer;
    float reactionDelay;
};

// ----------------------------------------------------------------------------
// ПРОЦЕДУРНЫЙ ЗВУКОВОЙ СИНТЕЗАТОР (ГЕНЕРАЦИЯ WAV В ОПЕРАТИВНОЙ ПАМЯТИ)
// ----------------------------------------------------------------------------
Sound GenerateProceduralSound(int type) {
    int sampleRate = 44100;
    float duration = (type == 3) ? 0.65f : (type == 4 ? 0.60f : 0.20f);
    int sampleCount = (int)(sampleRate * duration);
    short* pcm = (short*)MemAlloc(sampleCount * sizeof(short));

    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / (float)sampleRate;
        float noise = (float)GetRandomValue(-1000, 1000) / 1000.0f;
        float s = 0.0f;

        if (type == 0) { // SMG
            s = (noise * 0.65f + sinf(t * 880.0f * expf(-t * 22.0f)) * 0.5f) * expf(-t * 18.0f);
        } else if (type == 1) { // AK-47 Heavy
            s = (noise * 0.80f + sinf(t * 410.0f * expf(-t * 14.0f)) * 0.75f) * expf(-t * 13.0f);
        } else if (type == 2) { // Suppressed M4 / USP
            s = (noise * 0.45f + sinf(t * 1350.0f * expf(-t * 32.0f)) * 0.35f) * expf(-t * 26.0f);
        } else if (type == 3) { // AWP Sniper
            s = (noise * 0.90f + sinf(t * 165.0f * expf(-t * 5.5f)) * 0.95f) * expf(-t * 6.0f);
        } else if (type == 4) { // Grenade Explosion
            s = (noise * 0.95f + sinf(t * 95.0f) * 0.65f) * expf(-t * 4.5f);
        } else if (type == 5) { // Headshot Helmet Dink
            s = (sinf(t * 5800.0f) * 0.6f + sinf(t * 8900.0f) * 0.4f) * expf(-t * 20.0f);
        }
        s = Clamp(s, -1.0f, 1.0f);
        pcm[i] = (short)(s * 26000.0f);
    }

    Wave wave = { 0 };
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = pcm;

    Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return snd;
}

// ----------------------------------------------------------------------------
// ФУНКЦИИ СОХРАНЕНИЯ И ПРОВЕРКИ ДОСТИЖЕНИЙ
// ----------------------------------------------------------------------------
bool IsSkinUnlocked(const CareerProfile& p, int skinIdx) {
    switch (skinIdx) {
        case 0: return true;
        case 1: return p.headshotKills >= 15;
        case 2: return p.trickKills >= 5;
        case 3: return p.flawlessRounds >= 3;
        case 4: return p.clutchWins >= 1;
        case 5: return (p.totalKills >= 40 && p.awpKills >= 10 && p.grenadeKills >= 3);
        default: return false;
    }
}

void SaveProfile(const CareerProfile& p) {
    std::ofstream f("kinetic_profile.dat", std::ios::binary);
    if (f.is_open()) {
        CareerProfile copy = p;
        f.write(reinterpret_cast<const char*>(&copy), sizeof(CareerProfile));
    }
}

void LoadProfile(CareerProfile& p) {
    std::ifstream f("kinetic_profile.dat", std::ios::binary);
    if (f.is_open()) {
        f.read(reinterpret_cast<char*>(&p), sizeof(CareerProfile));
        if (p.selectedSkin < 0 || p.selectedSkin > 5 || !IsSkinUnlocked(p, p.selectedSkin)) {
            p.selectedSkin = 0;
        }
    }
}

// ----------------------------------------------------------------------------
// ВСПОМОГАТЕЛЬНАЯ ГЕОМЕТРИЯ И ПРОВЕРКА ВИДИМОСТИ
// ----------------------------------------------------------------------------
BoundingBox GetBoxBounds(const MapBox& b) {
    Vector3 half = Vector3Scale(b.size, 0.5f);
    return { Vector3Subtract(b.pos, half), Vector3Add(b.pos, half) };
}

BoundingBox GetAgentBounds(const Agent& a) {
    float h = a.crouching ? 1.25f : 1.75f;
    return {
        { a.pos.x - 0.38f, a.pos.y, a.pos.z - 0.38f },
        { a.pos.x + 0.38f, a.pos.y + h, a.pos.z + 0.38f }
    };
}

Vector3 GetAgentEye(const Agent& a) {
    return { a.pos.x, a.pos.y + (a.crouching ? 1.05f : 1.55f), a.pos.z };
}

bool CheckLineOfSight(Vector3 from, Vector3 to, const std::vector<MapBox>& map, const std::vector<ActiveGrenade>& grens) {
    Vector3 diff = Vector3Subtract(to, from);
    float dist = Vector3Length(diff);
    if (dist < 0.05f) return true;
    Ray ray = { from, Vector3Scale(diff, 1.0f / dist) };

    for (const auto& b : map) {
        if (b.destructible && b.hp <= 0.0f) continue;
        RayCollision col = GetRayCollisionBox(ray, GetBoxBounds(b));
        if (col.hit && col.distance < dist) return false;
    }
    for (const auto& g : grens) {
        if (g.exploded && g.type == GREN_SMOKE && g.smokeDuration > 0.0f) {
            RayCollision col = GetRayCollisionSphere(ray, g.pos, 4.2f);
            if (col.hit && col.distance < dist) return false;
        }
    }
    return true;
}

void ResolveMapCollisions(Vector3& pos, Vector3& vel, float radius, float height, const std::vector<MapBox>& map) {
    for (const auto& b : map) {
        if (b.destructible && b.hp <= 0.0f) continue;
        BoundingBox bb = GetBoxBounds(b);
        if (pos.y + height <= bb.min.y || pos.y >= bb.max.y) continue;

        float closestX = Clamp(pos.x, bb.min.x, bb.max.x);
        float closestZ = Clamp(pos.z, bb.min.z, bb.max.z);
        float dx = pos.x - closestX;
        float dz = pos.z - closestZ;
        float distSq = dx * dx + dz * dz;

        if (distSq < radius * radius) {
            float dist = sqrtf(distSq);
            if (dist < 0.0001f) {
                pos.x += radius;
            } else {
                float overlap = radius - dist;
                pos.x += (dx / dist) * overlap;
                pos.z += (dz / dist) * overlap;
            }
        }
    }
    pos.x = Clamp(pos.x, -28.5f, 28.5f);
    pos.z = Clamp(pos.z, -28.5f, 28.5f);
}

// ----------------------------------------------------------------------------
// ГЕНЕРАЦИЯ ТАКТИЧЕСКОЙ АРЕНЫ (CS-STYLE 3 LANES + DESTRUCTIBLE COVER)
// ----------------------------------------------------------------------------
void BuildArenaMap(std::vector<MapBox>& map) {
    map.clear();
    Color wallCol = { 58, 64, 74, 255 };
    Color pillarCol = { 78, 86, 98, 255 };
    Color woodCol = { 165, 118, 68, 255 };

    // Периметр арены
    map.push_back({{ 0.0f, 2.5f, -29.5f}, {60.0f, 5.0f, 1.0f}, wallCol, false, 999.0f});
    map.push_back({{ 0.0f, 2.5f, 29.5f}, {60.0f, 5.0f, 1.0f}, wallCol, false, 999.0f});
    map.push_back({{-29.5f, 2.5f, 0.0f}, { 1.0f, 5.0f, 60.0f}, wallCol, false, 999.0f});
    map.push_back({{ 29.5f, 2.5f, 0.0f}, { 1.0f, 5.0f, 60.0f}, wallCol, false, 999.0f});

    // Центральные бетонные перегородки (Mid & B-Tunnels & A-Long)
    map.push_back({{ -8.5f, 2.0f, 0.0f}, { 2.5f, 4.0f, 12.0f}, pillarCol, false, 999.0f});
    map.push_back({{ 8.5f, 2.0f, 0.0f}, { 2.5f, 4.0f, 12.0f}, pillarCol, false, 999.0f});
    map.push_back({{ 0.0f, 2.0f, -11.0f}, { 8.0f, 4.0f, 2.2f}, pillarCol, false, 999.0f});
    map.push_back({{ 0.0f, 2.0f, 11.0f}, { 8.0f, 4.0f, 2.2f}, pillarCol, false, 999.0f});

    // Боковые тактические колонны
    map.push_back({{-18.0f, 2.0f, -10.0f}, { 3.5f, 4.0f, 3.5f}, pillarCol, false, 999.0f});
    map.push_back({{-18.0f, 2.0f, 10.0f}, { 3.5f, 4.0f, 3.5f}, pillarCol, false, 999.0f});
    map.push_back({{ 18.0f, 2.0f, -10.0f}, { 3.5f, 4.0f, 3.5f}, pillarCol, false, 999.0f});
    map.push_back({{ 18.0f, 2.0f, 10.0f}, { 3.5f, 4.0f, 3.5f}, pillarCol, false, 999.0f});

    // Простреливаемые и разрушаемые деревянные ящики (Double Doors / Mid / Cover)
    Vector3 crateCoords[] = {
        { 0.0f, 0.9f, 0.0f}, {-2.8f, 0.9f, -4.5f}, { 2.8f, 0.9f, 4.5f},
        {-13.5f, 0.9f, 0.0f}, { 13.5f, 0.9f, 0.0f},
        { -6.0f, 0.9f, -16.5f}, { 6.0f, 0.9f, -16.5f},
        { -6.0f, 0.9f, 16.5f}, { 6.0f, 0.9f, 16.5f},
        {-22.0f, 0.9f, 0.0f}, { 22.0f, 0.9f, 0.0f}
    };
    for (Vector3 c : crateCoords) {
        map.push_back({c, {1.8f, 1.8f, 1.8f}, woodCol, true, 90.0f});
    }
}

// ----------------------------------------------------------------------------
// ГЛАВНЫЙ ЦИКЛ ПРИЛОЖЕНИЯ
// ----------------------------------------------------------------------------
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "KINETIC STRIKE: PROTOCOL // Tactical C++17 3D Shooter");
    InitAudioDevice();
    SetTargetFPS(120);
    DisableCursor();

    // Инициализация звуков
    Sound sndWeapons[4] = {
        GenerateProceduralSound(0),
        GenerateProceduralSound(1),
        GenerateProceduralSound(2),
        GenerateProceduralSound(3)
    };
    Sound sndExplode = GenerateProceduralSound(4);
    Sound sndHeadshot = GenerateProceduralSound(5);

    CareerProfile profile;
    LoadProfile(profile);

    std::vector<MapBox> arenaMap;
    std::vector<DebrisParticle> particles;
    std::vector<BulletTracer> tracers;
    std::vector<ActiveGrenade> grenades;
    std::vector<KillFeedEntry> killFeed;
    std::vector<Agent> agents;

    // Экономика и состояние матча
    int playerMoney = 800;
    int scoreAlpha = 0;
    int scoreOmega = 0;
    int lossStreak = 0;
    float buyPhaseTimer = 8.0f;
    float roundEndTimer = 0.0f;
    int roundWinner = -1; // -1 = идет раунд, 0 = Alpha, 1 = Omega
    bool inBuyMenu = false;
    bool inArmoryMenu = false;
    bool clutchSituationTriggered = false;
    bool roundDamageTaken = false;

    // Анимации оружия от первого лица
    float weaponKickBack = 0.0f;
    float weaponPitchKick = 0.0f;
    float inspectTimer = 0.0f;
    float walkBobTimer = 0.0f;
    float hitMarkerTimer = 0.0f;
    bool hitMarkerHeadshot = false;
    float playerFlashAlpha = 0.0f;
    int scopeZoomLevel = 0; // 0 = нет, 1 = 2.5x, 2 = 6x
    int activeGrenadeSlot = GREN_HE;
    std::string unlockBannerText = "";
    float unlockBannerTimer = 0.0f;

    // Сохранение экипировки игрока при выживании между раундами
    WeaponID savedPlayerWeapon = WPN_USP;
    bool savedPlayerAlive = false;
    float savedPlayerArmor = 0.0f;
    bool savedPlayerHelmet = false;

    auto StartNewRound = [&]() {
        BuildArenaMap(arenaMap);
        particles.clear();
        tracers.clear();
        grenades.clear();
        agents.clear();
        buyPhaseTimer = 7.0f;
        roundEndTimer = 0.0f;
        roundWinner = -1;
        clutchSituationTriggered = false;
        roundDamageTaken = false;
        scopeZoomLevel = 0;

        // Игрок (индекс 0) + 2 союзных бота (Alpha)
        Vector3 alphaSpawns[3] = {{0.0f, 0.0f, 24.0f}, {-4.5f, 0.0f, 24.5f}, {4.5f, 0.0f, 24.5f}};
        const char* alphaNames[3] = {"YOU", "ALLY Falcon", "ALLY Specter"};
        for (int i = 0; i < 3; i++) {
            Agent a = {};
            a.name = alphaNames[i];
            a.team = 0;
            a.isPlayer = (i == 0);
            a.alive = true;
            a.pos = alphaSpawns[i];
            a.yaw = PI; // Смотрим в сторону центра (-Z)
            a.pitch = 0.0f;
            a.hp = 100.0f;
            a.armor = (i == 0 && savedPlayerAlive) ? savedPlayerArmor : (i > 0 ? 100.0f : 0.0f);
            a.hasHelmet = (i == 0 && savedPlayerAlive) ? savedPlayerHelmet : (i > 0);
            a.weapon = (i == 0 && savedPlayerAlive) ? savedPlayerWeapon : (i == 0 ? WPN_USP : (i == 1 ? WPN_M4A1S : WPN_MP9));
            a.magAmmo = WEAPONS[a.weapon].magSize;
            a.reserveAmmo = WEAPONS[a.weapon].reserveMax;
            a.heCount = (i > 0) ? 1 : 0;
            a.flashCount = (i > 0) ? 1 : 0;
            a.smokeCount = 0;
            a.strafeDir = 1.0f;
            a.targetWaypoint = { (i == 1 ? -12.0f : 12.0f), 0.0f, 4.0f };
            agents.push_back(a);
        }

        // 4 вражеских тактических бота (Omega)
        Vector3 omegaSpawns[4] = {{-6.0f, 0.0f, -24.0f}, {-2.0f, 0.0f, -24.5f}, {2.0f, 0.0f, -24.5f}, {6.0f, 0.0f, -24.0f}};
        const char* omegaNames[4] = {"BOT Viper [AWP]", "BOT Titan [AK]", "BOT Ghost [M4]", "BOT Striker [MP9]"};
        WeaponID omegaWpns[4] = {WPN_AWP, WPN_AK47, WPN_M4A1S, WPN_MP9};
        Vector3 omegaWaypoints[4] = {{0.0f, 0.0f, -14.0f}, {-14.0f, 0.0f, -2.0f}, {14.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}};

        for (int i = 0; i < 4; i++) {
            Agent b = {};
            b.name = omegaNames[i];
            b.team = 1;
            b.isPlayer = false;
            b.alive = true;
            b.pos = omegaSpawns[i];
            b.yaw = 0.0f;
            b.hp = 100.0f;
            b.armor = 100.0f;
            b.hasHelmet = true;
            b.weapon = (scoreAlpha + scoreOmega == 0) ? WPN_USP : omegaWpns[i];
            b.magAmmo = WEAPONS[b.weapon].magSize;
            b.reserveAmmo = WEAPONS[b.weapon].reserveMax;
            b.heCount = 1;
            b.flashCount = 1;
            b.smokeCount = 1;
            b.strafeDir = (i % 2 == 0) ? 1.0f : -1.0f;
            b.targetWaypoint = omegaWaypoints[i];
            agents.push_back(b);
        }
    };

    StartNewRound();

    // Функция уведомления ботов о звуке (шаги, выстрелы, взрывы)
    auto BroadcastSoundAlert = [&](Vector3 soundOrigin, int sourceTeam, float radius) {
        for (auto& ag : agents) {
            if (!ag.alive || ag.isPlayer || ag.team == sourceTeam) continue;
            if (Vector3Distance(ag.pos, soundOrigin) <= radius) {
                ag.lastKnownEnemyPos = soundOrigin;
                ag.alertTimer = 5.0f;
                if (ag.state == BOT_PATROL) ag.state = BOT_INVESTIGATE;
            }
        }
    };

    // Функция проверки новых открытых скинов
    auto CheckNewSkinUnlocks = [&](const CareerProfile& before, const CareerProfile& after) {
        for (int s = 1; s < 6; s++) {
            if (!IsSkinUnlocked(before, s) && IsSkinUnlocked(after, s)) {
                unlockBannerText = std::string("NEW SKIN UNLOCKED: ") + SKINS[s].name + " (Press [K] to equip!)";
                unlockBannerTimer = 6.0f;
                profile.selectedSkin = s;
            }
        }
        SaveProfile(profile);
    };

    // Баллистический выстрел с поддержкой Wallbang и Ricochet
    auto ExecuteBallisticShot = [&](int shooterIdx) {
        Agent& shooter = agents[shooterIdx];
        const WeaponSpec& spec = WEAPONS[shooter.weapon];

        PlaySound(sndWeapons[spec.soundType]);
        BroadcastSoundAlert(shooter.pos, shooter.team, (shooter.weapon == WPN_M4A1S || shooter.weapon == WPN_USP) ? 16.0f : 38.0f);

        // Выброс физической гильзы
        Vector3 eye = GetAgentEye(shooter);
        Vector3 forward = { sinf(shooter.yaw) * cosf(shooter.pitch), sinf(shooter.pitch), cosf(shooter.yaw) * cosf(shooter.pitch) };
        Vector3 right = { -cosf(shooter.yaw), 0.0f, sinf(shooter.yaw) };
        particles.push_back({
            Vector3Add(eye, Vector3Add(Vector3Scale(forward, 0.35f), Vector3Scale(right, 0.2f))),
            { right.x * 3.2f + (float)GetRandomValue(-5, 5) * 0.1f, 2.4f, right.z * 3.2f },
            { 0, 0, 0 }, { 14.0f, 18.0f, 9.0f }, 0.04f, { 235, 195, 65, 255 }, 2.5f, true
        });

        // Расчет разброса и паттерна отдачи (CS-style)
        float speed2D = sqrtf(shooter.vel.x * shooter.vel.x + shooter.vel.z * shooter.vel.z);
        float spread = spec.baseSpread + (speed2D / 6.0f) * spec.moveSpread;
        if (shooter.crouching) spread *= 0.55f;
        if (shooter.isPlayer && scopeZoomLevel > 0 && spec.isScoped) spread *= 0.05f;
        if (!shooter.isPlayer && shooter.blindTimer > 0.0f) spread += 0.22f;

        float recoilPitchOffset = (shooter.recoilShots > 1) ? std::min(shooter.recoilShots * spec.recoilVertical * 0.012f, 0.14f) : 0.0f;
        float recoilYawOffset = (shooter.recoilShots > 3) ? sinf((float)shooter.recoilShots * 0.9f) * spec.recoilHorizontal * 0.015f : 0.0f;

        float shotYaw = shooter.yaw + recoilYawOffset + ((float)GetRandomValue(-1000, 1000) / 1000.0f) * spread;
        float shotPitch = shooter.pitch + recoilPitchOffset + ((float)GetRandomValue(-1000, 1000) / 1000.0f) * spread;

        Vector3 rayDir = Vector3Normalize({
            sinf(shotYaw) * cosf(shotPitch),
            sinf(shotPitch),
            cosf(shotYaw) * cosf(shotPitch)
        });

        Vector3 rayOrigin = eye;
        float damageLeft = spec.damage;
        bool penetratedOrRicocheted = false;

        // До 2 сегментов полета пули (пробитие ящика или рикошет от бетона)
        for (int seg = 0; seg < 2; seg++) {
            Ray ray = { rayOrigin, rayDir };
            float closestWallDist = 100.0f;
            int hitBoxIdx = -1;
            RayCollision bestWallCol = { 0 };

            for (size_t i = 0; i < arenaMap.size(); i++) {
                if (arenaMap[i].destructible && arenaMap[i].hp <= 0.0f) continue;
                RayCollision col = GetRayCollisionBox(ray, GetBoxBounds(arenaMap[i]));
                if (col.hit && col.distance > 0.01f && col.distance < closestWallDist) {
                    closestWallDist = col.distance;
                    hitBoxIdx = (int)i;
                    bestWallCol = col;
                }
            }

            // Проверяем попадание по агентам до стены
            int hitAgentIdx = -1;
            float closestAgentDist = closestWallDist;
            Vector3 hitAgentPoint = { 0 };
            bool isHeadshot = false;

            for (size_t i = 0; i < agents.size(); i++) {
                if ((int)i == shooterIdx || !agents[i].alive || agents[i].team == shooter.team) continue;
                BoundingBox ab = GetAgentBounds(agents[i]);
                RayCollision col = GetRayCollisionBox(ray, ab);
                if (col.hit && col.distance < closestAgentDist) {
                    closestAgentDist = col.distance;
                    hitAgentIdx = (int)i;
                    hitAgentPoint = col.point;
                    float headMinY = ab.max.y - 0.36f;
                    isHeadshot = (col.point.y >= headMinY);
                }
            }

            Vector3 segmentEnd = Vector3Add(rayOrigin, Vector3Scale(rayDir, closestAgentDist));
            Color tracerCol = (shooter.isPlayer && profile.selectedSkin == 5) ? Color{0, 245, 255, 230} : Color{255, 220, 120, 200};
            if (shooter.weapon != WPN_M4A1S) {
                tracers.push_back({ rayOrigin, segmentEnd, tracerCol, 0.09f });
            }

            // Если попали во врага
            if (hitAgentIdx != -1) {
                Agent& victim = agents[hitAgentIdx];
                float finalDmg = damageLeft * (isHeadshot ? spec.headshotMult : 1.0f);
                if (victim.armor > 0.0f) {
                    if (!isHeadshot || victim.hasHelmet) {
                        float absorbed = finalDmg * (1.0f - spec.armorPen);
                        victim.armor = std::max(0.0f, victim.armor - absorbed * 0.6f);
                        finalDmg *= spec.armorPen;
                    }
                }
                victim.hp -= finalDmg;
                if (victim.isPlayer) roundDamageTaken = true;

                // Частицы попадания
                for (int p = 0; p < 8; p++) {
                    particles.push_back({
                        hitAgentPoint,
                        { (float)GetRandomValue(-30, 30) * 0.1f, (float)GetRandomValue(10, 40) * 0.1f, (float)GetRandomValue(-30, 30) * 0.1f },
                        {0,0,0}, {5,5,5}, 0.06f, isHeadshot ? Color{255, 215, 0, 255} : Color{210, 25, 35, 255}, 0.45f, false
                    });
                }

                if (shooter.isPlayer) {
                    hitMarkerTimer = 0.22f;
                    hitMarkerHeadshot = isHeadshot;
                    if (isHeadshot) PlaySound(sndHeadshot);
                }

                if (victim.hp <= 0.0f) {
                    victim.alive = false;
                    killFeed.push_back({ shooter.name, victim.name, spec.name, isHeadshot, penetratedOrRicocheted, 5.0f, shooter.team });
                    if (shooter.isPlayer) {
                        playerMoney = std::min(16000, playerMoney + spec.killAward);
                        CareerProfile before = profile;
                        profile.totalKills++;
                        if (isHeadshot) profile.headshotKills++;
                        if (penetratedOrRicocheted) profile.trickKills++;
                        if (shooter.weapon == WPN_AWP) profile.awpKills++;
                        CheckNewSkinUnlocks(before, profile);
                    }
                }
                break;
            }

            // Если попали в препятствие
            if (hitBoxIdx != -1) {
                MapBox& box = arenaMap[hitBoxIdx];
                // Искры от удара пули
                for (int p = 0; p < 6; p++) {
                    particles.push_back({
                        bestWallCol.point,
                        Vector3Add(Vector3Scale(bestWallCol.normal, 2.5f), {(float)GetRandomValue(-20,20)*0.1f, (float)GetRandomValue(5,25)*0.1f, (float)GetRandomValue(-20,20)*0.1f}),
                        {0,0,0}, {8,8,8}, 0.05f, box.destructible ? Color{190, 140, 80, 255} : Color{255, 210, 90, 255}, 0.4f, false
                    });
                }

                if (box.destructible) {
                    box.hp -= damageLeft * 0.85f;
                    if (box.hp <= 0.0f) {
                        // Ящик разлетается на физические обломки!
                        for (int d = 0; d < 18; d++) {
                            particles.push_back({
                                Vector3Add(box.pos, {(float)GetRandomValue(-7,7)*0.1f, (float)GetRandomValue(-7,7)*0.1f, (float)GetRandomValue(-7,7)*0.1f}),
                                {(float)GetRandomValue(-45,45)*0.1f, (float)GetRandomValue(20,65)*0.1f, (float)GetRandomValue(-45,45)*0.1f},
                                {0,0,0}, {(float)GetRandomValue(-10,10), (float)GetRandomValue(-10,10), 4.0f},
                                0.22f, box.color, 3.0f, true
                            });
                        }
                    }
                    // Прострел сквозь дерево (Wallbang)
                    if (spec.canWallbang && seg == 0) {
                        penetratedOrRicocheted = true;
                        damageLeft *= 0.55f;
                        rayOrigin = Vector3Add(bestWallCol.point, Vector3Scale(rayDir, 1.85f));
                        continue;
                    }
                } else {
                    // Физический рикошет под острым углом от бетона/стали
                    float cosAngle = fabsf(Vector3DotProduct(rayDir, bestWallCol.normal));
                    if (cosAngle < 0.42f && seg == 0) {
                        penetratedOrRicocheted = true;
                        damageLeft *= 0.65f;
                        rayDir = Vector3Normalize(Vector3Subtract(rayDir, Vector3Scale(bestWallCol.normal, 2.0f * Vector3DotProduct(rayDir, bestWallCol.normal))));
                        rayOrigin = Vector3Add(bestWallCol.point, Vector3Scale(rayDir, 0.08f));
                        continue;
                    }
                }
            }
            break;
        }

        shooter.magAmmo--;
        shooter.fireCooldown = 1.0f / spec.fireRate;
        shooter.recoilShots++;
        shooter.recoilResetTimer = 0.32f;

        if (shooter.isPlayer) {
            weaponKickBack = 0.07f;
            weaponPitchKick = spec.recoilVertical * 0.03f;
            if (shooter.weapon == WPN_AWP) scopeZoomLevel = 0;
        }
    };

    // ------------------------------------------------------------------------
    // ОСНОВНОЙ ИГРОВОЙ ЦИКЛ
    // ------------------------------------------------------------------------
    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.05f);
        Agent& player = agents[0];

        // Переключение меню закупа [B] и Арсенала скинов [K]
        if (IsKeyPressed(KEY_B) && buyPhaseTimer > 0.0f) {
            inBuyMenu = !inBuyMenu;
            inArmoryMenu = false;
            if (inBuyMenu) EnableCursor(); else DisableCursor();
        }
        if (IsKeyPressed(KEY_K)) {
            inArmoryMenu = !inArmoryMenu;
            inBuyMenu = false;
            if (inArmoryMenu) EnableCursor(); else DisableCursor();
        }
        if (buyPhaseTimer <= 0.0f && inBuyMenu) {
            inBuyMenu = false;
            if (!inArmoryMenu) DisableCursor();
        }

        // Покупка снаряжения в Buy Menu
        if (inBuyMenu && buyPhaseTimer > 0.0f && player.alive) {
            auto TryBuyWeapon = [&](WeaponID wid) {
                if (playerMoney >= WEAPONS[wid].price && player.weapon != wid) {
                    playerMoney -= WEAPONS[wid].price;
                    player.weapon = wid;
                    player.magAmmo = WEAPONS[wid].magSize;
                    player.reserveAmmo = WEAPONS[wid].reserveMax;
                }
            };
            if (IsKeyPressed(KEY_ONE)) TryBuyWeapon(WPN_MP9);
            if (IsKeyPressed(KEY_TWO)) TryBuyWeapon(WPN_AK47);
            if (IsKeyPressed(KEY_THREE)) TryBuyWeapon(WPN_M4A1S);
            if (IsKeyPressed(KEY_FOUR)) TryBuyWeapon(WPN_AWP);
            if (IsKeyPressed(KEY_FIVE) && playerMoney >= 1000 && (player.armor < 100.0f || !player.hasHelmet)) {
                playerMoney -= 1000;
                player.armor = 100.0f;
                player.hasHelmet = true;
            }
            if (IsKeyPressed(KEY_SIX) && playerMoney >= 300 && player.heCount < 1) {
                playerMoney -= 300; player.heCount++;
            }
            if (IsKeyPressed(KEY_SEVEN) && playerMoney >= 200 && player.flashCount < 2) {
                playerMoney -= 200; player.flashCount++;
            }
            if (IsKeyPressed(KEY_EIGHT) && playerMoney >= 300 && player.smokeCount < 1) {
                playerMoney -= 300; player.smokeCount++;
            }
        }

        // Выбор скина в меню Арсенала [K]
        if (inArmoryMenu) {
            for (int k = 0; k < 6; k++) {
                if (IsKeyPressed(KEY_ONE + k) && IsSkinUnlocked(profile, k)) {
                    profile.selectedSkin = k;
                    SaveProfile(profile);
                }
            }
        }

        // Таймер фазы закупа (можно скипнуть на SPACE или ENTER)
        if (buyPhaseTimer > 0.0f) {
            buyPhaseTimer -= dt;
            if (IsKeyPressed(KEY_ENTER)) buyPhaseTimer = 0.0f;
        }

        // Управление игроком
        if (player.alive && !inBuyMenu && !inArmoryMenu) {
            Vector2 md = GetMouseDelta();
            float sens = (scopeZoomLevel == 2) ? 0.0007f : (scopeZoomLevel == 1 ? 0.0012f : 0.0022f);
            player.yaw -= md.x * sens;
            player.pitch -= md.y * sens;
            player.pitch = Clamp(player.pitch, -1.45f, 1.45f);

            player.crouching = IsKeyDown(KEY_LEFT_CONTROL);
            if (IsKeyPressed(KEY_F)) inspectTimer = 2.8f;

            // Переключение типа гранаты на [4] или [G] для броска
            if (IsKeyPressed(KEY_FOUR)) activeGrenadeSlot = (activeGrenadeSlot + 1) % 3;

            // Бросок гранаты на [G] или [Q]
            if ((IsKeyPressed(KEY_G) || IsKeyPressed(KEY_Q)) && buyPhaseTimer <= 0.0f) {
                int* countPtr = (activeGrenadeSlot == GREN_HE) ? &player.heCount : (activeGrenadeSlot == GREN_FLASH ? &player.flashCount : &player.smokeCount);
                if (*countPtr > 0) {
                    (*countPtr)--;
                    Vector3 dir = { sinf(player.yaw) * cosf(player.pitch), sinf(player.pitch) + 0.18f, cosf(player.yaw) * cosf(player.pitch) };
                    grenades.push_back({ GetAgentEye(player), Vector3Scale(Vector3Normalize(dir), 19.0f), activeGrenadeSlot, 1.8f, false, 14.0f, 0 });
                }
            }

            // Прицел AWP (Правая кнопка мыши)
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (WEAPONS[player.weapon].isScoped && player.reloadTimer <= 0.0f) {
                    scopeZoomLevel = (scopeZoomLevel + 1) % 3;
                }
            }

            // Перезарядка [R]
            if (IsKeyPressed(KEY_R) && player.reloadTimer <= 0.0f && player.magAmmo < WEAPONS[player.weapon].magSize && player.reserveAmmo > 0) {
                player.reloadTimer = WEAPONS[player.weapon].reloadTime;
                scopeZoomLevel = 0;
            }

            // Передвижение с инерцией (Counter-Strafing как в CS)
            if (buyPhaseTimer <= 0.0f) {
                Vector3 forwardFlat = { sinf(player.yaw), 0.0f, cosf(player.yaw) };
                Vector3 rightFlat = { -cosf(player.yaw), 0.0f, sinf(player.yaw) };
                Vector3 wishDir = { 0, 0, 0 };
                if (IsKeyDown(KEY_W)) wishDir = Vector3Add(wishDir, forwardFlat);
                if (IsKeyDown(KEY_S)) wishDir = Vector3Subtract(wishDir, forwardFlat);
                if (IsKeyDown(KEY_D)) wishDir = Vector3Add(wishDir, rightFlat);
                if (IsKeyDown(KEY_A)) wishDir = Vector3Subtract(wishDir, rightFlat);
                if (Vector3Length(wishDir) > 0.01f) wishDir = Vector3Normalize(wishDir);

                bool walkingQuietly = IsKeyDown(KEY_LEFT_SHIFT);
                float maxSpeed = 6.2f * WEAPONS[player.weapon].moveSpeedMult;
                if (player.crouching) maxSpeed *= 0.48f;
                else if (walkingQuietly) maxSpeed *= 0.52f;
                if (scopeZoomLevel > 0) maxSpeed *= 0.55f;

                player.vel.x = Lerp(player.vel.x, wishDir.x * maxSpeed, 14.0f * dt);
                player.vel.z = Lerp(player.vel.z, wishDir.z * maxSpeed, 14.0f * dt);

                if (IsKeyPressed(KEY_SPACE) && player.pos.y <= 0.01f) {
                    player.vel.y = 6.2f;
                    BroadcastSoundAlert(player.pos, 0, 18.0f);
                }
                player.vel.y -= 16.5f * dt;
                player.pos = Vector3Add(player.pos, Vector3Scale(player.vel, dt));
                if (player.pos.y < 0.0f) { player.pos.y = 0.0f; player.vel.y = 0.0f; }

                ResolveMapCollisions(player.pos, player.vel, 0.42f, player.crouching ? 1.25f : 1.75f, arenaMap);

                float horizSpeed = sqrtf(player.vel.x * player.vel.x + player.vel.z * player.vel.z);
                if (horizSpeed > 0.5f && player.pos.y <= 0.05f) {
                    walkBobTimer += dt * horizSpeed * 1.8f;
                    if (!walkingQuietly && !player.crouching && horizSpeed > 3.6f) {
                        BroadcastSoundAlert(player.pos, 0, 18.0f);
                    }
                }
            }

            // Стрельба
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && buyPhaseTimer <= 0.0f && player.fireCooldown <= 0.0f && player.reloadTimer <= 0.0f) {
                if (player.magAmmo > 0) {
                    inspectTimer = 0.0f;
                    ExecuteBallisticShot(0);
                } else if (player.reserveAmmo > 0) {
                    player.reloadTimer = WEAPONS[player.weapon].reloadTime;
                }
            }
        }

        // Обновление таймеров оружия и перезарядки всех агентов
        for (size_t i = 0; i < agents.size(); i++) {
            Agent& ag = agents[i];
            if (!ag.alive) continue;
            if (ag.fireCooldown > 0.0f) ag.fireCooldown -= dt;
            if (ag.recoilResetTimer > 0.0f) {
                ag.recoilResetTimer -= dt;
                if (ag.recoilResetTimer <= 0.0f) ag.recoilShots = 0;
            }
            if (ag.blindTimer > 0.0f) ag.blindTimer -= dt;
            if (ag.alertTimer > 0.0f) ag.alertTimer -= dt;

            if (ag.reloadTimer > 0.0f) {
                ag.reloadTimer -= dt;
                if (ag.reloadTimer <= 0.0f) {
                    int needed = WEAPONS[ag.weapon].magSize - ag.magAmmo;
                    int take = std::min(needed, ag.reserveAmmo);
                    ag.magAmmo += take;
                    ag.reserveAmmo -= take;
                }
            }
        }

        // --------------------------------------------------------------------
        // ТАКТИЧЕСКИЙ ИИ БОТОВ (ЛИНИЯ ВИДИМОСТИ, ЗВУК, СТРЕЙФЫ, УКРЫТИЯ)
        // --------------------------------------------------------------------
        if (buyPhaseTimer <= 0.0f && roundWinner == -1) {
            for (size_t i = 1; i < agents.size(); i++) {
                Agent& bot = agents[i];
                if (!bot.alive) continue;

                if (bot.magAmmo == 0 && bot.reloadTimer <= 0.0f && bot.reserveAmmo > 0) {
                    bot.reloadTimer = WEAPONS[bot.weapon].reloadTime;
                }

                // Поиск ближайшего видимого врага
                int visibleEnemy = -1;
                float bestDist = 999.0f;
                Vector3 botEye = GetAgentEye(bot);

                if (bot.blindTimer <= 0.8f) {
                    for (size_t j = 0; j < agents.size(); j++) {
                        if (!agents[j].alive || agents[j].team == bot.team) continue;
                        Vector3 enemyEye = GetAgentEye(agents[j]);
                        float d = Vector3Distance(bot.pos, agents[j].pos);
                        if (d < bestDist && CheckLineOfSight(botEye, enemyEye, arenaMap, grenades)) {
                            bestDist = d;
                            visibleEnemy = (int)j;
                        }
                    }
                }

                // Выбор состояния FSM
                if (bot.hp < 35.0f || bot.reloadTimer > 0.0f) {
                    bot.state = BOT_RETREAT;
                } else if (visibleEnemy != -1) {
                    bot.state = BOT_ENGAGE;
                    bot.lastKnownEnemyPos = agents[visibleEnemy].pos;
                    bot.alertTimer = 4.0f;
                } else if (bot.alertTimer > 0.0f) {
                    bot.state = BOT_INVESTIGATE;
                } else {
                    bot.state = BOT_PATROL;
                }

                Vector3 moveGoal = bot.targetWaypoint;
                float moveSpeed = 4.6f * WEAPONS[bot.weapon].moveSpeedMult;
                bot.crouching = false;

                if (bot.state == BOT_ENGAGE && visibleEnemy != -1) {
                    Agent& target = agents[visibleEnemy];
                    Vector3 toEnemy = Vector3Subtract(GetAgentEye(target), botEye);
                    float targetYaw = atan2f(toEnemy.x, toEnemy.z);
                    float horizDist = sqrtf(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
                    float targetPitch = atan2f(toEnemy.y, horizDist);

                    // Плавная наводка прицела
                    float yawDiff = targetYaw - bot.yaw;
                    while (yawDiff > PI) yawDiff -= 2.0f * PI;
                    while (yawDiff < -PI) yawDiff += 2.0f * PI;
                    bot.yaw += yawDiff * std::min(1.0f, 11.0f * dt);
                    bot.pitch = Lerp(bot.pitch, targetPitch, 10.0f * dt);

                    // Тактический стрейф влево-вправо и присед
                    bot.strafeTimer -= dt;
                    if (bot.strafeTimer <= 0.0f) {
                        bot.strafeDir = -bot.strafeDir;
                        bot.strafeTimer = (float)GetRandomValue(6, 15) * 0.1f;
                    }
                    Vector3 perp = { -cosf(bot.yaw) * bot.strafeDir, 0.0f, sinf(bot.yaw) * bot.strafeDir };
                    moveGoal = Vector3Add(bot.pos, Vector3Scale(perp, 3.0f));
                    moveSpeed *= 0.65f;
                    if (bestDist < 16.0f && bot.recoilShots > 3) bot.crouching = true;

                    // Стрельба очередями с контролем спрея
                    if (fabsf(yawDiff) < 0.24f && bot.fireCooldown <= 0.0f && bot.reloadTimer <= 0.0f && bot.magAmmo > 0) {
                        ExecuteBallisticShot((int)i);
                    }
                } else if (bot.state == BOT_INVESTIGATE) {
                    moveGoal = bot.lastKnownEnemyPos;
                    Vector3 dir = Vector3Subtract(moveGoal, bot.pos);
                    if (Vector3Length(dir) > 0.5f) bot.yaw = Lerp(bot.yaw, atan2f(dir.x, dir.z), 6.0f * dt);
                    // Тактический проброс гранаты перед выходом на угол
                    if (bot.flashCount > 0 && Vector3Distance(bot.pos, moveGoal) < 15.0f && GetRandomValue(0, 220) == 1) {
                        bot.flashCount--;
                        Vector3 throwDir = Vector3Normalize({ sinf(bot.yaw), 0.22f, cosf(bot.yaw) });
                        grenades.push_back({ botEye, Vector3Scale(throwDir, 16.0f), GREN_FLASH, 1.6f, false, 0.0f, bot.team });
                    }
                } else if (bot.state == BOT_RETREAT) {
                    // Отступление на свою половину за укрытие
                    moveGoal = { bot.pos.x * 0.5f, 0.0f, (bot.team == 0 ? 22.0f : -22.0f) };
                } else {
                    // Патрулирование по ключевым точкам карты
                    if (Vector3Distance(bot.pos, bot.targetWaypoint) < 2.2f) {
                        bot.targetWaypoint = { (float)GetRandomValue(-18, 18), 0.0f, (float)GetRandomValue(-16, 16) };
                    }
                    Vector3 dir = Vector3Subtract(bot.targetWaypoint, bot.pos);
                    if (Vector3Length(dir) > 0.2f) bot.yaw = atan2f(dir.x, dir.z);
                    bot.pitch = Lerp(bot.pitch, 0.0f, 5.0f * dt);
                }

                // Навигация с обходом препятствий
                Vector3 toGoal = Vector3Subtract(moveGoal, bot.pos);
                toGoal.y = 0.0f;
                if (Vector3Length(toGoal) > 0.35f) {
                    Vector3 stepDir = Vector3Normalize(toGoal);
                    Ray navRay = { {bot.pos.x, 0.9f, bot.pos.z}, stepDir };
                    for (const auto& b : arenaMap) {
                        if (b.destructible && b.hp <= 0.0f) continue;
                        RayCollision rc = GetRayCollisionBox(navRay, GetBoxBounds(b));
                        if (rc.hit && rc.distance < 1.8f) {
                            stepDir = Vector3Normalize(Vector3Add(stepDir, Vector3Scale(rc.normal, 1.6f)));
                        }
                    }
                    bot.vel.x = stepDir.x * moveSpeed;
                    bot.vel.z = stepDir.z * moveSpeed;
                    bot.pos = Vector3Add(bot.pos, Vector3Scale(bot.vel, dt));
                    ResolveMapCollisions(bot.pos, bot.vel, 0.42f, 1.75f, arenaMap);
                }
            }
        }

        // --------------------------------------------------------------------
        // ФИЗИКА ГРАНАТ (HE FRAG, FLASHBANG, SMOKE)
        // --------------------------------------------------------------------
        for (size_t i = 0; i < grenades.size(); i++) {
            ActiveGrenade& g = grenades[i];
            if (!g.exploded) {
                g.vel.y -= 14.5f * dt;
                Vector3 nextPos = Vector3Add(g.pos, Vector3Scale(g.vel, dt));
                if (nextPos.y < 0.15f) {
                    nextPos.y = 0.15f;
                    g.vel.y = -g.vel.y * 0.45f;
                    g.vel.x *= 0.72f;
                    g.vel.z *= 0.72f;
                }
                // Отскок от стен
                Ray gRay = { g.pos, Vector3Normalize(g.vel) };
                float stepLen = Vector3Length(g.vel) * dt;
                for (const auto& b : arenaMap) {
                    if (b.destructible && b.hp <= 0.0f) continue;
                    RayCollision rc = GetRayCollisionBox(gRay, GetBoxBounds(b));
                    if (rc.hit && rc.distance <= stepLen + 0.2f) {
                        g.vel = Vector3Subtract(g.vel, Vector3Scale(rc.normal, 2.0f * Vector3DotProduct(g.vel, rc.normal)));
                        g.vel = Vector3Scale(g.vel, 0.55f);
                        break;
                    }
                }
                g.pos = Vector3Add(g.pos, Vector3Scale(g.vel, dt));
                g.timer -= dt;

                if (g.timer <= 0.0f) {
                    g.exploded = true;
                    PlaySound(sndExplode);
                    if (g.type == GREN_HE) {
                        for (int p = 0; p < 28; p++) {
                            particles.push_back({
                                g.pos, {(float)GetRandomValue(-80,80)*0.1f, (float)GetRandomValue(20,90)*0.1f, (float)GetRandomValue(-80,80)*0.1f},
                                {0,0,0}, {10,10,10}, 0.18f, (p % 2 == 0) ? Color{255, 120, 20, 255} : Color{60, 60, 65, 255}, 0.8f, true
                            });
                        }
                        for (auto& ag : agents) {
                            if (!ag.alive) continue;
                            float d = Vector3Distance(ag.pos, g.pos);
                            if (d < 8.5f) {
                                float dmg = (1.0f - d / 8.5f) * 98.0f;
                                ag.hp -= dmg;
                                if (ag.isPlayer) roundDamageTaken = true;
                                if (ag.hp <= 0.0f) {
                                    ag.alive = false;
                                    killFeed.push_back({ (g.ownerTeam == 0 ? "YOU" : "BOT"), ag.name, "HE Grenade", false, false, 5.0f, g.ownerTeam });
                                    if (g.ownerTeam == 0 && ag.team == 1) {
                                        CareerProfile before = profile;
                                        profile.totalKills++;
                                        profile.grenadeKills++;
                                        CheckNewSkinUnlocks(before, profile);
                                    }
                                }
                            }
                        }
                    } else if (g.type == GREN_FLASH) {
                        for (auto& ag : agents) {
                            if (!ag.alive) continue;
                            Vector3 eye = GetAgentEye(ag);
                            float d = Vector3Distance(eye, g.pos);
                            if (d < 22.0f && CheckLineOfSight(eye, g.pos, arenaMap, grenades)) {
                                Vector3 look = { sinf(ag.yaw) * cosf(ag.pitch), sinf(ag.pitch), cosf(ag.yaw) * cosf(ag.pitch) };
                                Vector3 toFlash = Vector3Normalize(Vector3Subtract(g.pos, eye));
                                float dot = Vector3DotProduct(look, toFlash);
                                if (dot > 0.15f) {
                                    if (ag.isPlayer) playerFlashAlpha = 1.35f * dot;
                                    else ag.blindTimer = 3.2f * dot;
                                }
                            }
                        }
                    }
                }
            } else if (g.type == GREN_SMOKE) {
                g.smokeDuration -= dt;
            }
        }

        // Проверка условий завершения раунда и 1v2/1v3 Clutch
        int aliveAlpha = 0, aliveOmega = 0;
        for (const auto& ag : agents) {
            if (ag.alive) {
                if (ag.team == 0) aliveAlpha++; else aliveOmega++;
            }
        }
        if (player.alive && aliveAlpha == 1 && aliveOmega >= 2) {
            clutchSituationTriggered = true;
        }

        if (roundWinner == -1 && (aliveAlpha == 0 || aliveOmega == 0)) {
            roundWinner = (aliveAlpha > 0) ? 0 : 1;
            roundEndTimer = 4.0f;
            savedPlayerAlive = player.alive;
            savedPlayerWeapon = player.weapon;
            savedPlayerArmor = player.armor;
            savedPlayerHelmet = player.hasHelmet;

            if (roundWinner == 0) {
                scoreAlpha++;
                lossStreak = 0;
                playerMoney = std::min(16000, playerMoney + 3250);
                CareerProfile before = profile;
                if (player.alive && !roundDamageTaken) profile.flawlessRounds++;
                if (player.alive && clutchSituationTriggered) profile.clutchWins++;
                CheckNewSkinUnlocks(before, profile);
            } else {
                scoreOmega++;
                lossStreak++;
                playerMoney = std::min(16000, playerMoney + 1400 + std::min(lossStreak, 4) * 500);
            }
        }

        if (roundWinner != -1) {
            roundEndTimer -= dt;
            if (roundEndTimer <= 0.0f) StartNewRound();
        }

        // Обновление частиц, гильз и трассеров
        for (int i = (int)particles.size() - 1; i >= 0; i--) {
            DebrisParticle& p = particles[i];
            p.life -= dt;
            if (p.life <= 0.0f) { particles.erase(particles.begin() + i); continue; }
            if (p.isShell) p.vel.y -= 15.0f * dt;
            p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
            if (p.pos.y < 0.05f) {
                p.pos.y = 0.05f;
                p.vel.y = -p.vel.y * 0.45f;
                p.vel.x *= 0.7f;
                p.vel.z *= 0.7f;
            }
        }
        for (int i = (int)tracers.size() - 1; i >= 0; i--) {
            tracers[i].life -= dt;
            if (tracers[i].life <= 0.0f) tracers.erase(tracers.begin() + i);
        }
        for (int i = (int)killFeed.size() - 1; i >= 0; i--) {
            killFeed[i].timer -= dt;
            if (killFeed[i].timer <= 0.0f) killFeed.erase(killFeed.begin() + i);
        }

        weaponKickBack = Lerp(weaponKickBack, 0.0f, 14.0f * dt);
        weaponPitchKick = Lerp(weaponPitchKick, 0.0f, 12.0f * dt);
        if (inspectTimer > 0.0f) inspectTimer -= dt;
        if (hitMarkerTimer > 0.0f) hitMarkerTimer -= dt;
        if (playerFlashAlpha > 0.0f) playerFlashAlpha -= dt * 0.45f;
        if (unlockBannerTimer > 0.0f) unlockBannerTimer -= dt;

        // --------------------------------------------------------------------
        // РЕНДЕРИНГ 3D МИРА
        // --------------------------------------------------------------------
        Camera3D worldCam = { 0 };
        worldCam.position = GetAgentEye(player);
        float activePitch = Clamp(player.pitch + weaponPitchKick, -1.5f, 1.5f);
        Vector3 camForward = { sinf(player.yaw) * cosf(activePitch), sinf(activePitch), cosf(player.yaw) * cosf(activePitch) };
        worldCam.target = Vector3Add(worldCam.position, camForward);
        worldCam.up = { 0.0f, 1.0f, 0.0f };
        worldCam.fovy = (scopeZoomLevel == 2) ? 12.0f : (scopeZoomLevel == 1 ? 26.0f : 74.0f);
        worldCam.projection = CAMERA_PERSPECTIVE;

        BeginDrawing();
        ClearBackground({ 18, 22, 30, 255 });

        BeginMode3D(worldCam);
        // Пол арены и тактическая разметка
        DrawPlane({ 0.0f, 0.0f, 0.0f }, { 60.0f, 60.0f }, { 34, 38, 46, 255 });
        for (int g = -28; g <= 28; g += 4) {
            DrawLine3D({ (float)g, 0.01f, -28.0f }, { (float)g, 0.01f, 28.0f }, { 48, 54, 66, 255 });
            DrawLine3D({ -28.0f, 0.01f, (float)g }, { 28.0f, 0.01f, (float)g }, { 48, 54, 66, 255 });
        }

        // Стены и разрушаемые ящики
        for (const auto& b : arenaMap) {
            if (b.destructible && b.hp <= 0.0f) continue;
            DrawCubeV(b.pos, b.size, b.color);
            DrawCubeWiresV(b.pos, b.size, { 25, 28, 35, 255 });
        }

        // Отрисовка агентов (союзников и врагов)
        for (size_t i = 1; i < agents.size(); i++) {
            const Agent& ag = agents[i];
            if (!ag.alive) continue;
            Color teamCol = (ag.team == 0) ? Color{ 45, 155, 245, 255 } : Color{ 230, 55, 65, 255 };
            float torsoH = ag.crouching ? 0.85f : 1.25f;
            Vector3 torsoPos = { ag.pos.x, ag.pos.y + torsoH * 0.5f, ag.pos.z };
            DrawCubeV(torsoPos, { 0.62f, torsoH, 0.42f }, teamCol);
            DrawCubeWiresV(torsoPos, { 0.62f, torsoH, 0.42f }, BLACK);

            // Голова и шлем
            Vector3 headPos = { ag.pos.x, ag.pos.y + torsoH + 0.22f, ag.pos.z };
            DrawSphere(headPos, 0.22f, { 235, 195, 165, 255 });
            if (ag.hasHelmet) {
                DrawSphere({ headPos.x, headPos.y + 0.05f, headPos.z }, 0.23f, { 35, 40, 48, 255 });
            }

            // Оружие в руках бота
            Vector3 botLook = { sinf(ag.yaw), 0.0f, cosf(ag.yaw) };
            Vector3 gunStart = { ag.pos.x, ag.pos.y + torsoH * 0.75f, ag.pos.z };
            DrawCylinderEx(gunStart, Vector3Add(gunStart, Vector3Scale(botLook, 0.75f)), 0.05f, 0.04f, 6, DARKGRAY);
        }

        // Отрисовка гранат и объемного дыма
        for (const auto& g : grenades) {
            if (!g.exploded) {
                Color gc = (g.type == GREN_HE) ? RED : (g.type == GREN_FLASH ? YELLOW : SKYBLUE);
                DrawSphere(g.pos, 0.14f, gc);
            } else if (g.type == GREN_SMOKE && g.smokeDuration > 0.0f) {
                DrawSphere(g.pos, 4.1f, { 195, 202, 210, 215 });
                DrawSphere({ g.pos.x + 1.5f, g.pos.y + 0.8f, g.pos.z - 1.0f }, 3.1f, { 175, 182, 190, 200 });
                DrawSphere({ g.pos.x - 1.4f, g.pos.y + 0.6f, g.pos.z + 1.2f }, 3.1f, { 180, 188, 195, 200 });
            }
        }

        // Отрисовка частиц, гильз и трассеров
        for (const auto& p : particles) {
            DrawCubeV(p.pos, { p.size, p.size, p.size * (p.isShell ? 2.2f : 1.0f) }, p.color);
        }
        for (const auto& tr : tracers) {
            DrawLine3D(tr.start, tr.end, tr.color);
        }
        EndMode3D();

        // --------------------------------------------------------------------
        // РЕНДЕРИНГ 3D-ОРУЖИЯ ОТ ПЕРВОГО ЛИЦА С ВЫБРАННЫМ СКИНОМ
        // --------------------------------------------------------------------
        if (player.alive && scopeZoomLevel == 0) {
            Camera3D viewCam = { 0 };
            viewCam.position = { 0.0f, 0.0f, 0.0f };
            viewCam.target = { 0.0f, 0.0f, -1.0f };
            viewCam.up = { 0.0f, 1.0f, 0.0f };
            viewCam.fovy = 60.0f;
            viewCam.projection = CAMERA_PERSPECTIVE;

            SkinInfo skin = SKINS[profile.selectedSkin];
            if (profile.selectedSkin == 5) {
                // Анимированная пульсация для финального скина Abyssal Singularity
                float pulse = (sinf((float)GetTime() * 4.5f) + 1.0f) * 0.5f;
                skin.accentCol = { (unsigned char)(20 + pulse * 180), (unsigned char)(240 - pulse * 90), 255, 255 };
            }

            float bobX = sinf(walkBobTimer) * 0.012f;
            float bobY = fabsf(cosf(walkBobTimer)) * 0.012f;
            float reloadDrop = (player.reloadTimer > 0.0f) ? sinf((player.reloadTimer / WEAPONS[player.weapon].reloadTime) * PI) * 0.14f : 0.0f;
            float inspectShiftX = (inspectTimer > 0.0f) ? -sinf((inspectTimer / 2.8f) * PI) * 0.11f : 0.0f;
            float inspectLiftY = (inspectTimer > 0.0f) ? sinf((inspectTimer / 2.8f) * PI) * 0.06f : 0.0f;

            Vector3 gunBase = {
                0.22f + bobX + inspectShiftX,
                -0.19f - bobY - reloadDrop + inspectLiftY,
                -0.42f + weaponKickBack
            };

            BeginMode3D(viewCam);
            // Ствольная коробка (Receiver)
            DrawCube(gunBase, 0.065f, 0.085f, 0.34f, skin.bodyCol);
            DrawCubeWires(gunBase, 0.066f, 0.086f, 0.341f, skin.trimCol);

            // Верхняя планка и кожух со скином
            DrawCube({ gunBase.x, gunBase.y + 0.038f, gunBase.z - 0.04f }, 0.055f, 0.028f, 0.36f, skin.accentCol);

            // Подвижный затвор (двигается при каждом выстреле)
            DrawCube({ gunBase.x + 0.034f, gunBase.y + 0.02f, gunBase.z + weaponKickBack * 1.4f }, 0.015f, 0.025f, 0.11f, skin.trimCol);

            // Ствол и дульный тормоз / глушитель
            float barrelLen = (player.weapon == WPN_AWP) ? 0.48f : (player.weapon == WPN_M4A1S ? 0.38f : 0.28f);
            DrawCylinderEx(
                { gunBase.x, gunBase.y + 0.02f, gunBase.z - 0.16f },
                { gunBase.x, gunBase.y + 0.02f, gunBase.z - 0.16f - barrelLen },
                0.016f, 0.016f, 10, skin.trimCol
            );

            // Магазин (опускается вниз при перезарядке)
            float magOffset = (player.reloadTimer > 0.0f) ? reloadDrop * 1.8f : 0.0f;
            DrawCube({ gunBase.x, gunBase.y - 0.10f - magOffset, gunBase.z - 0.05f }, 0.045f, 0.14f, 0.065f, skin.accentCol);

            // Оптический прицел для AWP или коллиматор для винтовок
            if (player.weapon == WPN_AWP) {
                DrawCylinderEx(
                    { gunBase.x, gunBase.y + 0.075f, gunBase.z + 0.06f },
                    { gunBase.x, gunBase.y + 0.075f, gunBase.z - 0.16f },
                    0.026f, 0.030f, 12, skin.accentCol
                );
            } else if (player.weapon != WPN_USP) {
                DrawCube({ gunBase.x, gunBase.y + 0.065f, gunBase.z + 0.02f }, 0.042f, 0.036f, 0.06f, skin.trimCol);
            }
            EndMode3D();
        }

        // --------------------------------------------------------------------
        // HUD, ПРИЦЕЛ, СНАЙПЕРСКАЯ СЕТКА И МЕНЮ
        // --------------------------------------------------------------------
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        int cx = sw / 2;
        int cy = sh / 2;

        // Снайперский прицел AWP
        if (scopeZoomLevel > 0 && player.alive) {
            DrawRectangle(0, 0, cx - 260, sh, BLACK);
            DrawRectangle(cx + 260, 0, sw - (cx + 260), sh, BLACK);
            DrawRectangle(0, 0, sw, cy - 260, BLACK);
            DrawRectangle(0, cy + 260, sw, sh - (cy + 260), BLACK);
            DrawLine(0, cy, sw, cy, BLACK);
            DrawLine(cx, 0, cx, sh, BLACK);
            DrawCircleLines(cx, cy, 260.0f, RED);
            DrawText(TextFormat("ZOOM %dx", scopeZoomLevel == 1 ? 3 : 6), cx + 140, cy + 180, 18, RED);
        } else if (player.alive) {
            // Динамический прицел в стиле CS
            int gap = 5 + (int)(player.recoilShots * 2.5f);
            DrawLine(cx - gap - 8, cy, cx - gap, cy, GREEN);
            DrawLine(cx + gap, cy, cx + gap + 8, cy, GREEN);
            DrawLine(cx, cy - gap - 8, cx, cy - gap, GREEN);
            DrawLine(cx, cy + gap, cx, cy + gap + 8, GREEN);
        }

        // Хитмаркер попадания
        if (hitMarkerTimer > 0.0f) {
            Color hc = hitMarkerHeadshot ? GOLD : WHITE;
            DrawLine(cx - 10, cy - 10, cx - 4, cy - 4, hc);
            DrawLine(cx + 10, cy - 10, cx + 4, cy - 4, hc);
            DrawLine(cx - 10, cy + 10, cx - 4, cy + 4, hc);
            DrawLine(cx + 10, cy + 10, cx + 4, cy + 4, hc);
        }

        // Эффект ослепления от Flashbang
        if (playerFlashAlpha > 0.0f) {
            unsigned char a = (unsigned char)Clamp(playerFlashAlpha * 255.0f, 0.0f, 255.0f);
            DrawRectangle(0, 0, sw, sh, { 255, 255, 255, a });
        }

        // Верхняя панель счета и статуса раунда
        DrawRectangle(cx - 190, 10, 380, 48, { 12, 16, 22, 220 });
        DrawText(TextFormat("ALPHA %d", scoreAlpha), cx - 165, 22, 24, { 65, 175, 255, 255 });
        DrawText("VS", cx - 14, 24, 20, GRAY);
        DrawText(TextFormat("%d OMEGA", scoreOmega), cx + 45, 22, 24, { 245, 75, 85, 255 });

        if (buyPhaseTimer > 0.0f) {
            DrawText(TextFormat("BUY PHASE: %.1fs | Press [B] to Buy | [ENTER] Ready", buyPhaseTimer), cx - 235, 68, 20, GOLD);
        } else if (clutchSituationTriggered && roundWinner == -1 && player.alive) {
            DrawText("CLUTCH SITUATION ACTIVE (1v2+) - WIN FOR SOLAR FADE SKIN!", cx - 280, 68, 18, ORANGE);
        }

        // Kill Feed (правый верхний угол)
        for (size_t i = 0; i < killFeed.size(); i++) {
            const auto& kf = killFeed[i];
            std::string line = kf.killer + " [" + kf.weapon + (kf.headshot ? " HS" : "") + (kf.wallbang ? " WB/RIC" : "") + "] " + kf.victim;
            int tw = MeasureText(line.c_str(), 18);
            DrawRectangle(sw - tw - 30, 15 + (int)i * 28, tw + 18, 24, { 10, 14, 20, 200 });
            DrawText(line.c_str(), sw - tw - 22, 18 + (int)i * 28, 18, kf.killerTeam == 0 ? SKYBLUE : RED);
        }

        // Нижняя панель здоровья, брони, денег и патронов
        DrawRectangle(20, sh - 85, 420, 65, { 12, 16, 22, 220 });
        DrawText(TextFormat("HP: %d", (int)std::max(0.0f, player.hp)), 35, sh - 68, 28, player.hp > 35 ? GREEN : RED);
        DrawText(TextFormat("ARMOR: %d%s", (int)player.armor, player.hasHelmet ? "+H" : ""), 165, sh - 68, 24, SKYBLUE);
        DrawText(TextFormat("$%d", playerMoney), 330, sh - 68, 26, GOLD);

        DrawRectangle(sw - 430, sh - 85, 410, 65, { 12, 16, 22, 220 });
        DrawText(TextFormat("%s | %d / %d", WEAPONS[player.weapon].name, player.magAmmo, player.reserveAmmo), sw - 415, sh - 72, 22, WHITE);
        const char* gName = (activeGrenadeSlot == 0) ? "HE" : (activeGrenadeSlot == 1 ? "FLASH" : "SMOKE");
        DrawText(TextFormat("[4] Grenade: %s (HE:%d FL:%d SM:%d) [G] Throw", gName, player.heCount, player.flashCount, player.smokeCount), sw - 415, sh - 44, 16, LIGHTGRAY);

        // Подсказки управления слева сверху
        DrawText("[B] Buy Menu [K] Skins & Achievements [F] Inspect Skin [R] Reload", 20, 15, 16, LIGHTGRAY);
        DrawText(TextFormat("Active Skin: %s", SKINS[profile.selectedSkin].name), 20, 38, 18, SKINS[profile.selectedSkin].accentCol);

        // Баннер разблокировки нового скина
        if (unlockBannerTimer > 0.0f) {
            DrawRectangle(cx - 320, 110, 640, 44, { 20, 95, 45, 240 });
            DrawRectangleLines(cx - 320, 110, 640, 44, GOLD);
            DrawText(unlockBannerText.c_str(), cx - 300, 122, 20, GOLD);
        }

        // Окно закупа оружия [B]
        if (inBuyMenu) {
            DrawRectangle(cx - 310, cy - 210, 620, 420, { 14, 18, 26, 245 });
            DrawRectangleLines(cx - 310, cy - 210, 620, 420, SKYBLUE);
            DrawText("TACTICAL BUY MENU (Press 1-8 to Purchase, [B] to Close)", cx - 280, cy - 190, 20, GOLD);
            DrawText("[1] MP9 Submachine Gun ($1250) - High mobility, $600 kill reward", cx - 280, cy - 140, 18, WHITE);
            DrawText("[2] AK-47 Kinetic Rifle ($2700) - 1-Tap Headshot + Wallbang power", cx - 280, cy - 105, 18, WHITE);
            DrawText("[3] M4A1-S Suppressed ($2900) - Tight recoil, hidden tracers", cx - 280, cy - 70, 18, WHITE);
            DrawText("[4] AWP Magnum Sniper ($4750) - 1-Shot body kill + Dual Optical Scope", cx - 280, cy - 35, 18, WHITE);
            DrawText("[5] Kevlar + Tactical Helmet ($1000)", cx - 280, cy + 10, 18, SKYBLUE);
            DrawText("[6] HE Frag Grenade ($300) - High explosive shrapnel", cx - 280, cy + 45, 18, ORANGE);
            DrawText("[7] Flashbang ($200) - Blinds bots looking at detonation", cx - 280, cy + 80, 18, YELLOW);
            DrawText("[8] Smoke Canister ($300) - Blocks bot Line-of-Sight for 14s", cx - 280, cy + 115, 18, LIGHTGRAY);
        }

        // Окно Арсенала скинов и прогресса достижений [K]
        if (inArmoryMenu) {
            DrawRectangle(cx - 360, cy - 230, 720, 460, { 14, 18, 26, 248 });
            DrawRectangleLines(cx - 360, cy - 230, 720, 460, GOLD);
            DrawText("ARMORY & HARD ACHIEVEMENT SKINS (Press 1-6 to Equip)", cx - 330, cy - 210, 20, GOLD);
            DrawText(TextFormat("Career Stats: Kills: %d | Headshots: %d | Wallbang/Ricochet: %d | Flawless: %d | Clutches: %d",
                     profile.totalKills, profile.headshotKills, profile.trickKills, profile.flawlessRounds, profile.clutchWins),
                     cx - 330, cy - 175, 15, SKYBLUE);

            for (int s = 0; s < 6; s++) {
                bool unlocked = IsSkinUnlocked(profile, s);
                int yPos = cy - 130 + s * 55;
                DrawRectangle(cx - 330, yPos, 660, 46, (profile.selectedSkin == s) ? Color{35, 55, 85, 255} : Color{22, 28, 38, 255});
                DrawRectangle(cx - 320, yPos + 8, 30, 30, SKINS[s].accentCol);
                DrawText(TextFormat("[%d] %s - %s", s + 1, SKINS[s].name, SKINS[s].desc), cx - 275, yPos + 8, 17, unlocked ? WHITE : GRAY);
                DrawText(unlocked ? (profile.selectedSkin == s ? "EQUIPPED" : "UNLOCKED") : "LOCKED", cx + 225, yPos + 14, 16, unlocked ? GREEN : RED);
            }
        }

        EndDrawing();
    }

    for (int i = 0; i < 4; i++) UnloadSound(sndWeapons[i]);
    UnloadSound(sndExplode);
    UnloadSound(sndHeadshot);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
