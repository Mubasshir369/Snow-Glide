#include "raylib.h"
#include <vector>
#include <cmath>
#include <random>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ------------ Game Constants & UI ---------------
#define SCREEN_WIDTH 1500
#define SCREEN_HEIGHT 800
#define GROUND_Y 775
#define PLAYER_X 250
#define PLAYER_SPEED 10.0f
#define GRAVITY 1.0f
#define JUMP_FORCE -20.0f
#define MAGNET_RADIUS 400.0f
#define MAGNET_DURATION 5.0f
#define MAGNET_SPAWN_INTERVAL 10.0f
#define ROCK_SPAWN_MIN_INTERVAL 1.0f
#define ROCK_SPAWN_MAX_INTERVAL 6.0f
#define TREE_SPAWN_MIN_INTERVAL 2.0f
#define TREE_SPAWN_MAX_INTERVAL 4.0f
float manaRegenAccumulator = 0.0f;

const int iconSize = 48;
const int labelFontSize = 20;
const int tapFontSize = 30;

// ---------------- Persistent Shop Upgrades -------------------
const char* UPGRADE_FILE = "upgrades.txt";
int maxHealth = 100, maxMana = 100;
int bankaiCooldownUpgrade = 0, bankaiManaCostUpgrade = 0;
int selectedSkin = 0; // 0: default, 1: alt, ... Add more as you expand.
const int NUM_SKINS = 3; // Change to match how many you support.
Texture2D skinPreviews[NUM_SKINS];

const int HEALTH_MANA_STEP = 10;
const int UPGRADE_COST = 100;

const int BANKAI_COOLDOWN_BASE = 60;
const int BANKAI_COOLDOWN_STEP = 5;
const int BANKAI_COOLDOWN_MIN = 10;

const int BANKAI_COST_BASE = 25;
const int BANKAI_COST_STEP = 2;
const int BANKAI_COST_MIN = 5;

bool exitDialogOpen = false;    // Is the dialog showing?
Rectangle exitYesBtn, exitNoBtn; // Button rectangles for Yes/No

Rectangle tapToStartRect = {
    SCREEN_WIDTH / 2 - 180,    // X position (centered; adjust width as needed)
    SCREEN_HEIGHT - 130,       // Y position (near bottom; adjust as needed)
    360,                       // Width
    60                         // Height
};



void LoadUpgrades() {
    std::ifstream f(UPGRADE_FILE);
    if (f) f >> maxHealth >> maxMana >> bankaiCooldownUpgrade >> bankaiManaCostUpgrade >> selectedSkin;
    else { maxHealth = 100; maxMana = 100; bankaiCooldownUpgrade = 0; bankaiManaCostUpgrade = 0; selectedSkin = 0; }
}
void SaveUpgrades() {
    std::ofstream f(UPGRADE_FILE);
    f << maxHealth << " " << maxMana << " " << bankaiCooldownUpgrade << " " << bankaiManaCostUpgrade << " " << selectedSkin;
}
float getBankaiCooldown() {
    int v = BANKAI_COOLDOWN_BASE - bankaiCooldownUpgrade * BANKAI_COOLDOWN_STEP;
    if (v < BANKAI_COOLDOWN_MIN) v = BANKAI_COOLDOWN_MIN;
    return (float)v;
}
int getBankaiCost() {
    int v = BANKAI_COST_BASE - bankaiManaCostUpgrade * BANKAI_COST_STEP;
    if (v < BANKAI_COST_MIN) v = BANKAI_COST_MIN;
    return v;
}

// ------------- High Score Table ----------------
#define MAX_HIGHSCORES 5
#define MAX_NAME_LEN 20
struct HighScoreEntry { char name[MAX_NAME_LEN + 1]; int score; };
HighScoreEntry highScores[MAX_HIGHSCORES];
char nameInput[MAX_NAME_LEN + 1] = "";
bool waitingNameInput = false;
int insertIndex = -1;
void LoadHighScores() {
    std::ifstream in("highscores.txt");
    for (int i = 0; i < MAX_HIGHSCORES; i++) {
        if (!(in >> highScores[i].name >> highScores[i].score)) {
            strcpy(highScores[i].name, "---");
            highScores[i].score = 0;
        }
    }
}
void SaveHighScores() {
    std::ofstream out("highscores.txt");
    for (int i = 0; i < MAX_HIGHSCORES; i++)
        out << highScores[i].name << " " << highScores[i].score << std::endl;
}

// ------------ Sound Toggle --------------
#define SOUND_STATE_FILE "soundstate.txt"
bool isSoundOn = true;
Texture2D soundOnTexture, soundOffTexture;
Rectangle soundRect;
void LoadSoundState() {
    std::ifstream f(SOUND_STATE_FILE); int val = 1;
    if (f) f >> val;
    isSoundOn = (val == 1);
}
void SaveSoundState() {
    std::ofstream f(SOUND_STATE_FILE);
    f << (isSoundOn ? 1 : 0);
}
#define PLAY_SOUND(snd) do { if (isSoundOn) PlaySound(snd); } while(0)

bool isPaused = false;
Texture2D pauseIcon, resumeIcon;
Rectangle pauseRect;

// ------------ States & UI Strings --------------
enum AppState {
    MENU, STANDUP, GAME, SHOP, HIGH_SCORE,
    ZEN_TRANSITION, ZEN_MODE, ZEN_TRANSITION_BACK,
    GAME_OVER_STATE
};
AppState currentState = MENU;
const char* workshopText = "Workshop";
const char* highScoreText = "High Score";

// ------------ Structures -----------------
struct Coin { Vector2 position; bool active; };

struct Magnet { Vector2 position; bool active; };

struct Rock { Vector2 position; bool active; };

struct Tree { Vector2 position; float scale; bool active; };

struct Particle {
    Vector2 position, velocity;
    float life, maxLife;
    Color color;
};

struct Ic {
    Vector2 position;
    bool active;
    bool destroyed;
    float destroyTimer;
};

struct SnowFlake {
    float x, y;
    float speedY;
    float driftX;
    float size;
    float opacity;
};
std::vector<SnowFlake> snowflakes;
const int MAX_SNOWFLAKES = 236; // Increase for denser snow

struct RainDrop {
    float x, y, speed, length, thickness, opacity;
};

struct Bird { Vector2 position; float speed, scale; bool active; };
std::vector<Bird> birds;
Texture2D birdTexture;

std::vector<RainDrop> rainDrops;
bool raining = false;
float rainTimer = 0.0f;
int rainState = 0;
int rainPeriodCount = 0;
float rainNextEventTime = 10.0f;
float rainDuration = 60.0f, clearDuration = 20.0f;
float rainIntensity = 0.0f;
Sound rainSnd, thunderSnd;
bool rainSoundPlaying = false;
float thunderTimer = 0.0f, thunderInterval = 0.0f;
float timeSinceGameStarted = 0.0f;

// ------------ Persistent Storage -----------------
const char* COIN_FILE = "coin.txt";
const char* HIGH_SCORE_FILE = "highscore.txt";
int highScore = 0, totalCoins = 0;
void LoadStats() {
    std::ifstream coinFile(COIN_FILE); if (coinFile) coinFile >> totalCoins; coinFile.close();
    std::ifstream scoreFile(HIGH_SCORE_FILE); if (scoreFile) scoreFile >> highScore; scoreFile.close();
}
void SaveStats() {
    std::ofstream coinFile(COIN_FILE); coinFile << totalCoins; coinFile.close();
    std::ofstream scoreFile(HIGH_SCORE_FILE); scoreFile << highScore; scoreFile.close();
}

// -------------- Assets -------------------
Texture2D bg1, bg2, workshopIcon, highScoreIcon;
Texture2D coinTexture, magnetTexture, rockTexture, treeTexture, icTexture, icDestroyedTexture;
Texture2D healthTexture, manaTexture;
std::vector<Texture2D> playerFrames;
Texture2D jumpFrames[4];
const int NUM_STANDUP_FRAMES = 4;
Texture2D standUpFrames[NUM_STANDUP_FRAMES];
int standUpCurrentFrame = 0;
float standUpFrameTimer = 0.0f;
float standUpFrameDuration = 0.17f;
Sound bankaiSnd;
float bankaiFlashTimer = 0.0f;
float bankaiTextAlpha = 0.0f;
Music bgm1;
Texture2D clockTexture;
Texture2D exitIcon;
Rectangle exitRect;

// ======== SKIN LOADING ==========
void LoadSkin(int skin) {
    if (!playerFrames.empty()) for (auto& t : playerFrames) UnloadTexture(t);
    for (int i = 0; i < 4; i++) UnloadTexture(jumpFrames[i]);
    for (int i = 0; i < NUM_STANDUP_FRAMES; i++) UnloadTexture(standUpFrames[i]);
    playerFrames.clear();
    char buf[64];

    if (skin == 0) { // Default skin
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/s%d.png", i); playerFrames.push_back(LoadTexture(buf)); }
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/j%d.png", i); jumpFrames[i - 1] = LoadTexture(buf); }
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/w%d.png", 5-i); standUpFrames[i-1] = LoadTexture(buf); }
    }
    else if (skin == 1) { // Alt 1
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/s%d%d.png", i, i); playerFrames.push_back(LoadTexture(buf)); }
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/j%d%d.png", i, i); jumpFrames[i - 1] = LoadTexture(buf); }
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/w%d%d.png", 5-i, 5-i); standUpFrames[i-1] = LoadTexture(buf); }
    }
    else if (skin == 2) { // Alt 2
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/s%d%d%d.png", i,i,i); playerFrames.push_back(LoadTexture(buf)); }
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/j%d%d%d.png", i,i,i); jumpFrames[i - 1] = LoadTexture(buf); }
        for (int i = 1; i <= 4; i++) { sprintf(buf, "img/w%d%d%d.png", 5-i,5-i,5-i); standUpFrames[i-1] = LoadTexture(buf); }
    }
}


void LoadAssets() {
    clockTexture = LoadTexture("img/clock.png");
    bgm1 = LoadMusicStream("sound/bgm1.ogg");
    bg1 = LoadTexture("img/bg1.png");
    bg2 = LoadTexture("img/bg2.png");
    pauseIcon = LoadTexture("img/p.png");
    resumeIcon = LoadTexture("img/r.png");
    workshopIcon = LoadTexture("img/workshop.png");
    highScoreIcon = LoadTexture("img/hs.png");
    coinTexture = LoadTexture("img/coin.png");
    magnetTexture = LoadTexture("img/magnet.png");
    rockTexture = LoadTexture("img/rock.png");
    treeTexture = LoadTexture("img/tree.png");
    icTexture = LoadTexture("img/ic.png");
    icDestroyedTexture = LoadTexture("img/icd.png");
    healthTexture = LoadTexture("img/he.png");
    manaTexture = LoadTexture("img/mi.png");
    bankaiSnd = LoadSound("sound/bankai.ogg");
    rainSnd = LoadSound("sound/rain.ogg");
    thunderSnd = LoadSound("sound/th.ogg");
    LoadSkin(selectedSkin);
    soundOnTexture = LoadTexture("img/sb.png");
    soundOffTexture = LoadTexture("img/sbd.png");
    exitIcon = LoadTexture("img/exit.png");
    birdTexture = LoadTexture("img/bird.png");
    skinPreviews[0] = LoadTexture("img/s1p.png");
    skinPreviews[1] = LoadTexture("img/s11p.png");
    skinPreviews[2] = LoadTexture("img/s111p.png");
}
void UnloadAssets() {
    for (int i = 0; i < NUM_SKINS; i++) UnloadTexture(skinPreviews[i]);
    for (auto& t : playerFrames) UnloadTexture(t);
    for (int i = 0; i < 4; i++) UnloadTexture(jumpFrames[i]);
    for (int i = 0; i < NUM_STANDUP_FRAMES; i++) UnloadTexture(standUpFrames[i]);
    UnloadTexture(bg1); UnloadTexture(bg2);
    UnloadTexture(workshopIcon); UnloadTexture(highScoreIcon);
    UnloadTexture(coinTexture); UnloadTexture(magnetTexture);
    UnloadTexture(rockTexture); UnloadTexture(treeTexture);
    UnloadTexture(icTexture); UnloadTexture(icDestroyedTexture);
    UnloadTexture(pauseIcon); UnloadTexture(resumeIcon);
    UnloadTexture(soundOnTexture); UnloadTexture(soundOffTexture);
    UnloadTexture(healthTexture); UnloadTexture(manaTexture);
    UnloadSound(bankaiSnd);
    UnloadTexture(birdTexture);
    UnloadSound(rainSnd);
    UnloadSound(thunderSnd);
    UnloadTexture(clockTexture);
    UnloadTexture(exitIcon);
}

// -------------- Day-Night --------------
float dayNightTimer = 0.0f;
const int numPhases = 5;
const float dayNightDuration = 90.0f;
const float phaseDuration = dayNightDuration / numPhases;
Color phaseColors[numPhases] = {
    {100, 180, 255, 45},
    {255, 165, 0, 45},
    {255, 20, 147, 45},
    {0, 0, 139, 85},
    {0, 0, 220, 95}
};
Color LerpColor(Color color1, Color color2, float t) {
    Color result;
    result.r = (unsigned char)(color1.r + (color2.r - color1.r) * t);
    result.g = (unsigned char)(color1.g + (color2.g - color1.g) * t);
    result.b = (unsigned char)(color1.b + (color2.b - color1.b) * t);
    result.a = (unsigned char)(color1.a + (color2.a - color1.a) * t);
    return result;
}
float Distance(Vector2 a, Vector2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
Vector2 Normalize(Vector2 v) {
    float length = sqrtf(v.x * v.x + v.y * v.y);
    if (length > 0) return { v.x / length, v.y / length };
    return { 0, 0 };
}
void SpawnCoinBurst(std::vector<Particle>& burstParticles, Vector2 pos) {
    int numParticles = 20;
    for (int i = 0; i < numParticles; i++) {
        float angle = 2 * PI * i / numParticles;
        float speed = 180 + GetRandomValue(-30, 30);
        Vector2 vel = {cosf(angle) * speed, sinf(angle) * speed - GetRandomValue(30, 90)};
        Particle p;
        p.position = pos;
        p.velocity = vel;
        p.life = 0.23f + GetRandomValue(0, 10) / 100.0f;
        p.maxLife = p.life;
        p.color = GOLD;
        burstParticles.push_back(p);
    }
}

// -------------- UI State --------------
unsigned char tapTextAlpha = 80;
float transitionX = 0, transitionSpeed = 2500;
float bg1Offset = 0, bg2Offset = 0, bgScrollSpeed = 60;
Rectangle workshopRect = { SCREEN_WIDTH - 78, 24, (float)iconSize, (float)iconSize };
Rectangle highScoreRect = { 30, 24, (float)iconSize, (float)iconSize };

// ------------- Game State --------------
std::vector<Coin> coins;
std::vector<Magnet> magnets;
std::vector<Rock> rocks;
std::vector<Tree> trees;
std::vector<Particle> burstParticles;
Ic ic = { {0, 0}, false, false, 0 };
float icSpawnTimer = 0.0f, icSpawnInterval = 10.0f;
float icSlowTimer = 0.0f;
bool icSlowing = false;

float birdSpawnTimer = 0.0f, birdSpawnInterval = 3.0f + (float)GetRandomValue(0, 200)/50.0f; // 3–7s

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_real_distribution<> disXCoin(0.0f, SCREEN_WIDTH * 10.0f);
std::uniform_real_distribution<> disYCoin(500.0f, 600.0f);
std::uniform_real_distribution<> disRockInterval(ROCK_SPAWN_MIN_INTERVAL, ROCK_SPAWN_MAX_INTERVAL);
std::uniform_real_distribution<> disTreeInterval(TREE_SPAWN_MIN_INTERVAL, TREE_SPAWN_MAX_INTERVAL);
std::uniform_real_distribution<> disIcInterval(8.0f, 14.0f);

void InitGameLogic(
    Vector2& playerPos, float& verticalSpeed, float& currentSpeed, float& speedTimer,
    bool& onGround, int& currentFrame, float& frameCounter, int& coinCount,
    bool& magnetActive, float& magnetTimer, float& magnetSpawnTimer, float& rockSpawnTimer,
    float& rockSpawnInterval, float& treeSpawnTimer, float& treeSpawnInterval, bool& gameOver,
    float playerWidth, float playerHeight,
    int& health, int& mana,
    float& spawnBlockTimer
) {
    playerPos = { PLAYER_X, GROUND_Y - playerHeight };
    verticalSpeed = 0.0f; currentSpeed = PLAYER_SPEED; speedTimer = 0.0f;
    onGround = true; currentFrame = 0; frameCounter = 0.0f; coinCount = 0;
    magnetActive = false; magnetTimer = 0.0f; magnetSpawnTimer = 0.0f;
    rockSpawnTimer = 0.0f; rockSpawnInterval = (float)disRockInterval(gen);
    treeSpawnTimer = 0.0f; treeSpawnInterval = (float)disTreeInterval(gen);
    gameOver = false;
    rocks.clear(); magnets.clear(); trees.clear(); burstParticles.clear();
    for (auto& c : coins) c.active = true;
    ic = { {0, 0}, false, false, 0 };
    icSpawnTimer = 0.0f;
    icSpawnInterval = (float)disIcInterval(gen);
    icSlowTimer = 0.0f;
    icSlowing = false;
    health = maxHealth;
    mana = maxMana;

    birds.clear(); birdSpawnTimer = 0.0f;
    birdSpawnInterval = 3.0f + (float)GetRandomValue(0, 200)/50.0f;

    rainDrops.clear();
    raining = false;
    rainTimer = 0.0f;
    rainState = 0;
    rainPeriodCount = 0;
    rainNextEventTime = 20.0f;
    timeSinceGameStarted = 0.0f;
    rainSoundPlaying = false;

    spawnBlockTimer = 0.0f; // Used for 5 second no-spawn
}

float gameIntroTimer = 0.0f;
const float GAME_INTRO_DURATION = 2.0f;
bool gameIntroActive = false;

// ======= MAIN LOOP ==========
int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Snow Glide");
    SetExitKey(0);
    SetTargetFPS(60);
    InitAudioDevice();
    LoadUpgrades();
    LoadAssets();
    LoadStats();
    LoadSoundState();
    LoadHighScores();

    // --- Initialize SnowFlakes ---
    snowflakes.clear();
    for (int i = 0; i < MAX_SNOWFLAKES; i++) {
        SnowFlake s;
        s.x = GetRandomValue(0, SCREEN_WIDTH);
        s.y = GetRandomValue(0, SCREEN_HEIGHT);
        s.speedY = 70 + GetRandomValue(0, 100);      // Faster fall
        s.driftX = GetRandomValue(-18, 18) / 10.0f;  // More visible horizontal drift
        s.size = 2.5f + GetRandomValue(0, 6) / 2.0f; // Slightly bigger average
        s.opacity = 0.5f + GetRandomValue(50, 100)/255.0f; // More visible
        snowflakes.push_back(s);
    }

    PlayMusicStream(bgm1);
    bgm1.looping = true;

    pauseRect = { SCREEN_WIDTH - iconSize - 24, 24, (float)iconSize, (float)iconSize };
    soundRect = {workshopRect.x, workshopRect.y + iconSize + 40, (float)iconSize, (float)iconSize };
    exitRect = { 24, SCREEN_HEIGHT - iconSize - 32, (float)iconSize, (float)iconSize };

    float playerScale = 0.4f;
    float playerHeight = playerFrames[0].height * playerScale;
    float playerWidth = playerFrames[0].width * playerScale;
    coins.clear();
    const int maxCoins = 10;
    for (int i = 0; i < maxCoins;) {
        float x = (float)disXCoin(gen);
        float y = (float)disYCoin(gen);
        bool tooClose = false;
        for (const auto& existing : coins) {
            float dx = fabsf(x - existing.position.x);
            float dy = fabsf(y - existing.position.y);
            if (dx < 32.0f && dy < 40.0f) { tooClose = true; break; }
        }
        if (!tooClose) { coins.push_back({ { x, y }, true }); i++; }
    }

    Vector2 playerPos = { PLAYER_X, GROUND_Y - playerHeight };
    float verticalSpeed = 0.0f, currentSpeed = PLAYER_SPEED, speedTimer = 0.0f;
    bool onGround = true, gameOver = false, magnetActive = false;
    int currentFrame = 0, coinCount = 0;
    float frameCounter = 0.0f;
    float magnetTimer = 0.0f, magnetSpawnTimer = 0.0f;
    float rockSpawnTimer = 0.0f, rockSpawnInterval = (float)disRockInterval(gen);
    float treeSpawnTimer = 0.0f, treeSpawnInterval = (float)disTreeInterval(gen);
    float magnetScale = (playerWidth * 0.6f) / magnetTexture.width;
    float rockScale = (playerWidth * 1.5f) / rockTexture.width;
    float treeScale = 0.8f;
    float coinScale = (playerWidth * 0.5f) / coinTexture.width;
    float icScale = playerHeight / (float)icTexture.height;
    int lastRunCoinCount = 0;
    int health = maxHealth, mana = maxMana;
    float spawnBlockTimer = 0.0f; // For 5 second "nothing" at start

    bankaiFlashTimer = 0.0f;
    bankaiTextAlpha = 0.0f;
    bool bankaiActive = false;
    bool lowManaMsg = false;
    float lowManaMsgTimer = 0.0f;
    const float BANKAI_TEXT_FADE = 1.2f;
    float bankaiCooldown = 0.0f;

    while (!WindowShouldClose()) {

        float dt = GetFrameTime();

        if (isSoundOn) {
            UpdateMusicStream(bgm1);
            if (!IsMusicStreamPlaying(bgm1)) PlayMusicStream(bgm1);
        } else {
            PauseMusicStream(bgm1);
        }

        // Sound button
        if (CheckCollisionPointRec(GetMousePosition(), soundRect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isSoundOn = !isSoundOn;
            SaveSoundState();
        }
        if (currentState == GAME && !isPaused)
            timeSinceGameStarted += dt;

        if (bankaiCooldown > 0.0f) bankaiCooldown -= dt;
        if (bankaiCooldown < 0.0f) bankaiCooldown = 0.0f;

        // --------- RAIN LOGIC ---------
        if (currentState == GAME && !isPaused && rainPeriodCount < 4) {
            rainTimer += dt;
            if (rainState == 0 && rainTimer >= rainNextEventTime) {
                raining = true;
                rainState = 1;
                rainTimer = 0.0f;
                rainPeriodCount++;
                rainIntensity = 0.6f + 0.4f * ((float)GetRandomValue(0, 100) / 100.0f);
                rainDrops.clear();
                int dropCount = 250 + (int)(rainIntensity * 300);
                for (int i = 0; i < dropCount; i++) {
                    RainDrop r;
                    r.x = (float)GetRandomValue(0, SCREEN_WIDTH);
                    r.y = (float)GetRandomValue(0, SCREEN_HEIGHT);
                    r.length = 16 + rainIntensity * GetRandomValue(8, 32);
                    r.thickness = 1 + rainIntensity * GetRandomValue(1, 2);
                    r.opacity = 0.45f + rainIntensity * 0.45f;
                    r.speed = 600 + rainIntensity * GetRandomValue(100, 500);
                    rainDrops.push_back(r);
                }
                PLAY_SOUND(rainSnd);
                rainSoundPlaying = true;
                thunderInterval = 2.0f + ((float)GetRandomValue(0, 300) / 100.0f);
                thunderTimer = 0.0f;
            }
            else if (rainState == 1 && rainTimer >= rainDuration) {
                raining = false;
                rainState = 0;
                rainTimer = 0.0f;
                rainNextEventTime = clearDuration;
                if (rainSoundPlaying) {
                    StopSound(rainSnd);
                    rainSoundPlaying = false;
                }
            }
            if (raining) {
                thunderTimer += dt;
                if (thunderTimer > thunderInterval) {
                    PLAY_SOUND(thunderSnd);
                    thunderInterval = 2.0f + ((float)GetRandomValue(0, 300) / 100.0f);
                    thunderTimer = 0.0f;
                }
                for (auto &drop : rainDrops) {
                    drop.y += drop.speed * dt;
                    if (drop.y > SCREEN_HEIGHT) {
                        drop.x = (float)GetRandomValue(0, SCREEN_WIDTH);
                        drop.y = -GetRandomValue(10, 400);
                    }
                }
            }
        } else if ((isPaused || currentState != GAME) && rainSoundPlaying) {
            PauseSound(rainSnd);
        } else if (!isPaused && currentState == GAME && raining && !IsSoundPlaying(rainSnd)) {
            ResumeSound(rainSnd);
        }

        // ---- BANKAI trigger (B KEY) ----
        if ((currentState == GAME) && !bankaiActive && bankaiCooldown <= 0.0f && IsKeyPressed(KEY_B)) {
            if (mana >= getBankaiCost()) {
                bankaiActive = true;
                bankaiFlashTimer = 0.25f;
                bankaiTextAlpha = 1.0f;
                PLAY_SOUND(bankaiSnd);
                mana -= getBankaiCost();
                if (mana < 0) mana = 0;
                bankaiCooldown = getBankaiCooldown();
                for (auto& c : coins) c.active = false;
                rocks.clear();
                trees.clear();
                magnets.clear();
                ic.active = false;
                ic.destroyed = false;
            } else {
                lowManaMsg = true;
                lowManaMsgTimer = BANKAI_TEXT_FADE;
            }
        }

        if (currentState == GAME && !isPaused) {
            bg1Offset += bgScrollSpeed * dt;
            if (bg1Offset >= bg1.width) bg1Offset -= bg1.width;
            bg2Offset += bgScrollSpeed * dt;
            if (bg2Offset >= bg2.width) bg2Offset -= bg2.width;
        } else {
            bg1Offset = 0;
            bg2Offset = 0;
        }

        Vector2 mouse = GetMousePosition();

        // ---- MENU/INPUT ----
        if (currentState == MENU) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();
                if (CheckCollisionPointRec(mouse, workshopRect)) {
                    currentState = SHOP;
                }
                else if (CheckCollisionPointRec(mouse, highScoreRect)) {
                    currentState = HIGH_SCORE;
                }
                else if (CheckCollisionPointRec(mouse, exitRect)) {
                    exitDialogOpen = true;
                }
                // Only start if click is inside "tap to start" rect
                else if (CheckCollisionPointRec(mouse, tapToStartRect)) {
                    standUpCurrentFrame = 0;
                    standUpFrameTimer = 0.0f;
                    InitGameLogic(playerPos, verticalSpeed, currentSpeed, speedTimer,
                        onGround, currentFrame, frameCounter, coinCount,
                        magnetActive, magnetTimer, magnetSpawnTimer, rockSpawnTimer,
                        rockSpawnInterval, treeSpawnTimer, treeSpawnInterval, gameOver,
                        playerWidth, playerHeight,
                        health, mana,
                        spawnBlockTimer
                    );
                    currentState = STANDUP;
                }
                // Else: clicked outside, do nothing!
            }
            if (IsKeyPressed(KEY_RIGHT)) { currentState = ZEN_TRANSITION; transitionX = 0; }
        }


        else if (currentState == STANDUP) {
            standUpFrameTimer += dt;
            if (standUpFrameTimer >= standUpFrameDuration) {
                standUpFrameTimer = 0.0f;
                standUpCurrentFrame++;
                if (standUpCurrentFrame >= NUM_STANDUP_FRAMES) {
                    currentState = GAME;
                    gameIntroTimer = 0.0f;
                    gameIntroActive = true;
                    dayNightTimer = 0.0f;
                    spawnBlockTimer = 0.0f;
                }
            }
        }
        else if (currentState == ZEN_MODE && IsKeyPressed(KEY_LEFT)) {
            currentState = ZEN_TRANSITION_BACK; transitionX = SCREEN_WIDTH;
        }
        else if ((currentState == GAME || currentState == SHOP || currentState == HIGH_SCORE)
                    && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE))) {
            if (currentState == GAME && coinCount > 0) {
                if (coinCount > highScore) highScore = coinCount;
                totalCoins += coinCount;
                SaveStats();
            }
            currentState = MENU;
        }
        if (currentState == ZEN_TRANSITION) {
            transitionX += transitionSpeed * dt;
            if (transitionX >= SCREEN_WIDTH) { transitionX = SCREEN_WIDTH; currentState = ZEN_MODE; }
        } else if (currentState == ZEN_TRANSITION_BACK) {
            transitionX -= transitionSpeed * dt;
            if (transitionX <= 0) { transitionX = 0; currentState = MENU; }
        }

        // -------- GAME LOGIC --------
        float effectiveSpeed = currentSpeed;
        if (raining) effectiveSpeed *= 0.95f;
        if (currentState == GAME) {
            if (!gameIntroActive && CheckCollisionPointRec(mouse, pauseRect) && !CheckCollisionPointRec(mouse, soundRect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                isPaused = !isPaused;
            }
            if (!isPaused) {
                if (gameIntroActive) {
                    gameIntroTimer += dt;
                    if (gameIntroTimer >= GAME_INTRO_DURATION) {
                        gameIntroActive = false;
                    }
                }
                speedTimer += dt;
                if (speedTimer >= 1.0f) { currentSpeed += 0.1f; speedTimer = 0.0f; }
                if (icSlowing) {
                    icSlowTimer -= dt;
                    if (icSlowTimer <= 0.0f) {
                        icSlowing = false;
                        currentSpeed = PLAYER_SPEED + (currentSpeed - PLAYER_SPEED) * 0.0f;
                    }
                }
                if (IsKeyPressed(KEY_SPACE) && onGround && mana >= 1) {
                    verticalSpeed = JUMP_FORCE; onGround = false;
                    mana -= 1;
                    if (mana < 0) mana = 0;
                }
                const float manaRegenPerSecond = 0.5f;
                if (mana < maxMana) {
                    manaRegenAccumulator += manaRegenPerSecond * dt;
                    while (manaRegenAccumulator >= 1.0f && mana < maxMana) {
                        mana++;
                        manaRegenAccumulator -= 1.0f;
                    }
                    if (mana > maxMana) mana = maxMana;
                }
                verticalSpeed += GRAVITY;
                playerPos.y += verticalSpeed;
                if (playerPos.y >= GROUND_Y - playerHeight) { playerPos.y = GROUND_Y - playerHeight; verticalSpeed = 0.0f; onGround = true; }
                if (onGround) {
                    frameCounter += dt;
                    if (frameCounter >= 0.10f) { frameCounter = 0.0f; currentFrame = (currentFrame + 1) % playerFrames.size(); }
                } else {
                    if (verticalSpeed < -5.0f) currentFrame = 0;
                    else if (verticalSpeed < -1.0f) currentFrame = 1;
                    else if (verticalSpeed < 2.0f) currentFrame = 2;
                    else currentFrame = 3;
                }
                if (!isPaused) dayNightTimer += dt;
                // ----- SPAWN-BLOCK: 5 seconds delay -----
                spawnBlockTimer += dt;
                bool canSpawn = (spawnBlockTimer > 5.0f);

                // Only spawn/animate entities after 5 seconds
                if (!gameIntroActive && canSpawn) {
                    magnetSpawnTimer += dt;
                    if (magnetSpawnTimer >= MAGNET_SPAWN_INTERVAL) {
                        magnetSpawnTimer = 0.0f;
                        float x = playerPos.x + SCREEN_WIDTH + GetRandomValue(200, 800);
                        float y = (float)GetRandomValue(500, 600);
                        magnets.push_back({ { x, y }, true });
                    }
                    rockSpawnTimer += dt;
                    if (rockSpawnTimer >= rockSpawnInterval) {
                        rockSpawnTimer = 0.0f; rockSpawnInterval = (float)disRockInterval(gen);
                        bool validPosition = false; int attempts = 0;
                        while (!validPosition && attempts < 20) {
                            float x = playerPos.x + SCREEN_WIDTH + GetRandomValue(300, 1000);
                            float y = GROUND_Y - 110;
                            bool overlapsWithCoin = false;
                            for (const auto& c : coins) {
                                if (c.active) {
                                    float coinWidth = coinTexture.width * coinScale;
                                    float coinHeight = coinTexture.height * coinScale;
                                    if (x < c.position.x + coinWidth + 50 &&
                                        x + rockTexture.width * rockScale > c.position.x - 50 &&
                                        y < c.position.y + coinHeight + 50 &&
                                        y + rockTexture.height * rockScale > c.position.y - 50) {
                                        overlapsWithCoin = true; break;
                                    }
                                }
                            }
                            bool overlapsWithRock = false;
                            for (const auto& r : rocks) {
                                if (r.active) {
                                    if (x < r.position.x + rockTexture.width * rockScale + 100 &&
                                        x + rockTexture.width * rockScale > r.position.x - 100) {
                                        overlapsWithRock = true; break;
                                    }
                                }
                            }
                            if (!overlapsWithCoin && !overlapsWithRock) {
                                rocks.push_back({ { x, y }, true }); validPosition = true;
                            } else attempts++;
                        }
                    }
                    treeSpawnTimer += dt;
                    if (treeSpawnTimer >= treeSpawnInterval) {
                        treeSpawnTimer = 0.0f; treeSpawnInterval = (float)disTreeInterval(gen);
                        float x = playerPos.x + SCREEN_WIDTH + GetRandomValue(200, 600);
                        float y = GROUND_Y - treeTexture.height * treeScale + 45;
                        trees.push_back({ { x, y }, treeScale, true });
                    }
                    // === BIRD SPAWNING ===
                    birdSpawnTimer += dt;
                    if (birdSpawnTimer >= birdSpawnInterval) {
                        birdSpawnTimer = 0.0f;
                        birdSpawnInterval = 3.0f + (float)GetRandomValue(0, 200)/50.0f;

                        float x = playerPos.x + SCREEN_WIDTH + GetRandomValue(300, 900);

                        // Jump-over bird Y positioning
                        float playerBaseY = GROUND_Y - playerHeight;
                        float minBirdY = playerBaseY - 180;
                        float maxBirdY = playerBaseY - 160;
                        float y = (float)GetRandomValue((int)minBirdY, (int)maxBirdY);

                        float scale = 0.25f;
                        float speed = effectiveSpeed + 5.0f + GetRandomValue(0, 50)/10.0f;
                        birds.push_back({ {x, y}, speed, scale, true });
                    }

                    // === UPDATE BIRDS ===
                    for (auto it = birds.begin(); it != birds.end();) {
                        if (it->active) {
                            it->position.x -= it->speed;
                            if (it->position.x < -birdTexture.width * it->scale) {
                                it = birds.erase(it);
                                continue;
                            }
                            // --- Collision with player ---
                            Rectangle birdRect = { it->position.x, it->position.y,
                                birdTexture.width * it->scale, birdTexture.height * it->scale
                            };
                            Rectangle playerRect = { playerPos.x, playerPos.y, playerWidth, playerHeight };
                            if (CheckCollisionRecs(birdRect, playerRect)) {
                                it->active = false;
                                // --- COLLISION CONSEQUENCE (EXAMPLE): ---
                                // e.g., reset coin count and damage player
                                lastRunCoinCount = coinCount;
                                health -= 30;
                                if(coinCount>2) coinCount -= 3; // you can change this effect
                                else coinCount=0;
                                if (health <= 0) {
                                    health = 0;
                                    if (coinCount > highScore) highScore = coinCount;
                                    totalCoins += coinCount;
                                    SaveStats();
                                    currentState = GAME_OVER_STATE;
                                    bankaiActive = false;
                                    bankaiFlashTimer = 0.0f;
                                    bankaiTextAlpha = 0.0f;
                                    bankaiCooldown = 0.0f;
                                    goto drawSection;
                                }
                            }
                        }
                        ++it;
                    }
                }

                // All entity updates and collisions are only allowed after 5 seconds
                if (!gameIntroActive && canSpawn) {
                    if (magnetActive) { magnetTimer -= dt; if (magnetTimer <= 0.0f) magnetActive = false; }
                    Vector2 playerCenter = { playerPos.x + playerWidth / 2.0f, playerPos.y + playerHeight / 2.0f };
                    Rectangle playerRect = { playerPos.x, playerPos.y, playerWidth, playerHeight };
                    for (auto it = burstParticles.begin(); it != burstParticles.end(); ) {
                        it->position.x += it->velocity.x * dt;
                        it->position.y += it->velocity.y * dt;
                        it->velocity.y += 800.0f * dt;
                        it->life -= dt;
                        if (it->life <= 0.0f) it = burstParticles.erase(it); else ++it;
                    }

                    icSpawnTimer += dt;
                    if (!ic.active && !ic.destroyed && icSpawnTimer >= icSpawnInterval) {
                        icSpawnTimer = 0.0f;
                        icSpawnInterval = (float)disIcInterval(gen);
                        bool valid = false; int tries = 0;
                        while (!valid && tries < 20) {
                            float x = playerPos.x + SCREEN_WIDTH + GetRandomValue(350, 1000);
                            float y = GROUND_Y - icTexture.height * icScale;
                            Rectangle icRect = { x, y, icTexture.width * icScale, icTexture.height * icScale };
                            bool overlapsRock = false;
                            for (const auto& r : rocks) {
                                if (r.active) {
                                    Rectangle rockRect = { r.position.x, r.position.y, rockTexture.width * rockScale, rockTexture.height * rockScale };
                                    if (CheckCollisionRecs(icRect, rockRect)) { overlapsRock = true; break; }
                                }
                            }
                            if (!overlapsRock) {
                                ic.position = { x, y };
                                ic.active = true;
                                ic.destroyed = false;
                                ic.destroyTimer = 0;
                                valid = true;
                            }
                            tries++;
                        }
                    }

                    if (ic.active && !ic.destroyed) {
                        ic.position.x -= effectiveSpeed;
                        if (ic.position.x < -icTexture.width * icScale) ic.active = false;
                        Rectangle icRect = { ic.position.x, ic.position.y, icTexture.width * icScale, icTexture.height * icScale };
                        if (CheckCollisionRecs(icRect, playerRect)) {
                            ic.active = false;
                            ic.destroyed = true;
                            ic.destroyTimer = 2.0f;
                            icSlowing = true;
                            icSlowTimer = 4.0f;
                            currentSpeed *= 0.8f;
                        }
                    }
                    if (ic.destroyed) {
                        ic.destroyTimer -= dt;
                        if (ic.destroyTimer <= 0) {
                            ic.destroyed = false;
                        }
                    }

                    for (const auto& r : rocks) {
                        if (r.active) {
                            Rectangle rockRect = { r.position.x, r.position.y, rockTexture.width * rockScale, rockTexture.height * rockScale };
                            if (CheckCollisionRecs(playerRect, rockRect)) {
                                lastRunCoinCount = coinCount;
                                health -= 40;
                                if (health <= 0) {
                                    health = 0;
                                    if (coinCount > highScore) highScore = coinCount;
                                    totalCoins += coinCount;
                                    SaveStats();
                                    currentState = GAME_OVER_STATE;
                                    bankaiActive = false;
                                    bankaiFlashTimer = 0.0f;
                                    bankaiTextAlpha = 0.0f;
                                    bankaiCooldown = 0.0f;
                                    goto drawSection;
                                }
                                const_cast<Rock&>(r).active = false;
                            }
                        }
                    }
                    int activeCoinCount = 0;
                    for (const auto& c : coins) if (c.active) activeCoinCount++;
                    for (auto& c : coins) {
                        if (c.active) {
                            if (magnetActive) {
                                Vector2 coinCenter = { c.position.x + (coinTexture.width * coinScale) / 2.0f, c.position.y + (coinTexture.height * coinScale) / 2.0f };
                                float dist = Distance(playerCenter, coinCenter);
                                if (dist < MAGNET_RADIUS && dist > 5.0f) {
                                    Vector2 direction = { playerCenter.x - coinCenter.x, playerCenter.y - coinCenter.y };
                                    direction = Normalize(direction);
                                    float attractionSpeed = 300.0f * dt;
                                    c.position.x += direction.x * attractionSpeed;
                                    c.position.y += direction.y * attractionSpeed;
                                }
                            }
                            Rectangle coinRect = { c.position.x, c.position.y, coinTexture.width * coinScale, coinTexture.height * coinScale };
                            if (CheckCollisionRecs(playerRect, coinRect)) {
                                c.active = false; coinCount++; activeCoinCount--;
                                Vector2 burstPos = { c.position.x + (coinTexture.width * coinScale)/2, c.position.y + (coinTexture.height * coinScale)/2 };
                                SpawnCoinBurst(burstParticles, burstPos);
                            }
                        }
                        c.position.x -= effectiveSpeed;
                        if (c.position.x < -coinTexture.width && activeCoinCount < 20) {
                            bool validPosition = false; int attempts = 0;
                            while (!validPosition && attempts < 20) {
                                float newX = playerPos.x + SCREEN_WIDTH + GetRandomValue(300, 1000);
                                float newY = (float)GetRandomValue(500, 630);
                                bool overlapsWithRock = false;
                                float coinWidth = coinTexture.width * coinScale;
                                float coinHeight = coinTexture.height * coinScale;
                                for (const auto& r : rocks) {
                                    if (r.active) {
                                        if (newX < r.position.x + rockTexture.width * rockScale + 50 &&
                                            newX + coinWidth > r.position.x - 50 &&
                                            newY < r.position.y + rockTexture.height * rockScale + 50 &&
                                            newY + coinHeight > r.position.y - 50) {
                                            overlapsWithRock = true; break;
                                        }
                                    }
                                }
                                bool overlapsWithCoin = false;
                                for (const auto& existing : coins) {
                                    if (existing.active && &existing != &c) {
                                        float dx = fabsf(newX - existing.position.x);
                                        float dy = fabsf(newY - existing.position.y);
                                        if (dx < coinWidth + 32.0f && dy < coinHeight + 40.0f) { overlapsWithCoin = true; break; }
                                    }
                                }
                                if (!overlapsWithRock && !overlapsWithCoin) {
                                    c.position.x = newX; c.position.y = newY;
                                    c.active = true; activeCoinCount++; validPosition = true;
                                } else attempts++;
                            }
                            if (!validPosition) {
                                c.position.x = playerPos.x + SCREEN_WIDTH + GetRandomValue(300, 1000);
                                c.position.y = (float)GetRandomValue(500, 630);
                                c.active = true; activeCoinCount++;
                            }
                        }
                    }

                    for (auto it = magnets.begin(); it != magnets.end();) {
                        if (it->active) {
                            Rectangle magnetRect = { it->position.x, it->position.y, magnetTexture.width * magnetScale, magnetTexture.height * magnetScale };
                            if (CheckCollisionRecs(playerRect, magnetRect)) { it->active = false; magnetActive = true; magnetTimer = MAGNET_DURATION; }
                        }
                        it->position.x -= effectiveSpeed;
                        if (it->position.x < -magnetTexture.width) it = magnets.erase(it); else ++it;
                    }
                    for (auto it = rocks.begin(); it != rocks.end();) {
                        it->position.x -= effectiveSpeed;
                        if (it->position.x < -rockTexture.width * rockScale) it = rocks.erase(it); else ++it;
                    }
                    for (auto it = trees.begin(); it != trees.end();) {
                        it->position.x -= effectiveSpeed;
                        if (it->position.x < -treeTexture.width * it->scale) it = trees.erase(it); else ++it;
                    }
                }
                for (auto& s : snowflakes) {
                    s.y += s.speedY * dt;
                    s.x += s.driftX * dt * 12.0f; // Stronger drift

                    // Wrap snowflakes to the top when out of screen
                    if (s.y > SCREEN_HEIGHT) {
                        s.y = -s.size;
                        s.x = GetRandomValue(0, SCREEN_WIDTH);
                    }
                    if (s.x < 0) s.x += SCREEN_WIDTH;
                    if (s.x > SCREEN_WIDTH) s.x -= SCREEN_WIDTH;
                }
            }
        }

drawSection:;
        BeginDrawing();
        ClearBackground(BLACK);

        if (currentState == GAME) {
            float offset1 = fmodf(bg1Offset, bg1.width);
            DrawTexturePro(bg1, { offset1, 0, (float)bg1.width, (float)bg1.height },
                        { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);

            float offset2 = fmodf(bg2Offset, bg2.width);
            DrawTexturePro(bg2, { offset2, 0, (float)bg2.width, (float)bg2.height },
                        { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);
            DrawTexturePro(bg2, { offset2 - bg2.width, 0, (float)bg2.width, (float)bg2.height },
                        { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);
        } else {
            DrawTexturePro(bg1, { 0, 0, (float)bg1.width, (float)bg1.height },
                        { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);
        }

        if (currentState == MENU) {
            DrawTexturePro(highScoreIcon, { 0, 0, (float)highScoreIcon.width, (float)highScoreIcon.height },
                                          { highScoreRect.x, highScoreRect.y, iconSize, iconSize }, { 0, 0 }, 0, WHITE);
            DrawText(highScoreText, highScoreRect.x + iconSize + 8, highScoreRect.y + (iconSize - labelFontSize) / 2, labelFontSize, DARKGRAY);

            int workshopTextWidth = MeasureText(workshopText, labelFontSize);
            DrawTexturePro(workshopIcon, { 0, 0, (float)workshopIcon.width, (float)workshopIcon.height },
                                           { workshopRect.x, workshopRect.y, iconSize, iconSize }, { 0, 0 }, 0, WHITE);
            DrawText(workshopText, workshopRect.x - workshopTextWidth - 8, workshopRect.y + (iconSize - labelFontSize) / 2, labelFontSize, DARKGRAY);

           // Draw the tap-to-start rectangle outline (optional)
            DrawRectangleLines(tapToStartRect.x, tapToStartRect.y, tapToStartRect.width, tapToStartRect.height, (Color){80, 180, 255, 70});

            // --- Hover effect: if mouse is over tap area, draw a transparent highlight ---
            if (CheckCollisionPointRec(GetMousePosition(), tapToStartRect)) {
                DrawRectangleRec(tapToStartRect, (Color){40, 180, 255, 60}); // adjust alpha (60) as you want
            }

            // Center the text inside the tapToStartRect
            const char* tapMsg = "tap to start";
            int tapWidth = MeasureText(tapMsg, tapFontSize);
            int tapTextX = tapToStartRect.x + tapToStartRect.width/2 - tapWidth/2;
            int tapTextY = tapToStartRect.y + tapToStartRect.height/2 - tapFontSize/2;
            DrawText(tapMsg, tapTextX, tapTextY, tapFontSize, (Color){30, 30, 30, tapTextAlpha});

            DrawTextureEx(standUpFrames[0], playerPos, 0.0f, playerScale, WHITE);


        }
        else if (currentState == STANDUP) {
            DrawTextureEx(standUpFrames[standUpCurrentFrame], playerPos, 0.0f, playerScale, WHITE);
        }

        // *** SKIN SELECTION SLOT IN SHOP ***
        if (currentState == SHOP) {
            DrawText(TextFormat("Total Coins: %d", totalCoins), SCREEN_WIDTH / 2 - 180, 150, 32, DARKGRAY);

            int btnW = 480, btnH = 70, gapY = 30;
            int startX = SCREEN_WIDTH/2 - btnW/2, startY = 240;
            Rectangle btns[5];
            for (int i=0;i<5;i++) btns[i] = { (float)startX, (float)(startY + i*(btnH+gapY)), (float)btnW, (float)btnH };

            Color buy = (Color){180, 130, 255, 220};
            Color buyHover = (Color){200, 160, 255, 240};
            Color noBuy = (Color){150,150,150,170};

            for (int i = 0; i < 5; i++) {
                bool hover = CheckCollisionPointRec(GetMousePosition(), btns[i]);
                DrawRectangleRec(btns[i], hover ? buyHover : buy);
            }
            // Health upgrade
            bool maxedHealth = (maxHealth >= 200);
            bool canBuyHealth = !maxedHealth && totalCoins >= UPGRADE_COST;
            if (!canBuyHealth) DrawRectangleRec(btns[0], noBuy);
            DrawTextureEx(healthTexture, (Vector2){(float)(startX), (float)(startY + 18)}, 0.0f, 0.05f, WHITE);
            DrawText(TextFormat("Max Health: %d  (+10)", maxHealth), startX +60, startY + 12, 32, PINK);
            DrawTextureEx(coinTexture, (Vector2){(float)(startX + 500), (float)(startY + 18)}, 0.0f, 0.03f, WHITE);
            DrawText(TextFormat("%d", UPGRADE_COST), startX + 540, startY + 22, 28, DARKGRAY);
            if (maxedHealth) DrawText("MAX", startX + 415, startY + 12, 28, RED);

            // Mana upgrade
            bool maxedMana = (maxMana >= 200);
            bool canBuyMana = !maxedMana && totalCoins >= UPGRADE_COST;
            if (!canBuyMana) DrawRectangleRec(btns[1], noBuy);
            DrawTextureEx(manaTexture, (Vector2){(float)(startX), (float)(startY + btnH + gapY + 18)}, 0.0f, 0.11f, WHITE);
            DrawText(TextFormat("Max Mana: %d  (+10)", maxMana), startX + 60, startY + btnH + gapY + 12, 32, BLACK);
            DrawTextureEx(coinTexture, (Vector2){(float)(startX + 500), (float)(startY + btnH + gapY + 18)}, 0.0f, 0.03f, WHITE);
            DrawText(TextFormat("%d", UPGRADE_COST), startX + 540, startY + btnH + gapY + 22, 28, DARKGRAY);
            if (maxedMana) DrawText("MAX", startX + 415, startY + btnH + gapY + 12, 28, RED);

            // Bankai Cooldown (with clock icon)
            float showCd = getBankaiCooldown();
            bool minCd = (showCd <= BANKAI_COOLDOWN_MIN);
            bool canBuyCd = !minCd && totalCoins >= UPGRADE_COST;
            if (!canBuyCd) DrawRectangleRec(btns[2], noBuy);
            DrawTextureEx(clockTexture, (Vector2){(float)(startX), (float)(startY + 2*(btnH + gapY) + 18)}, 0.0f, 0.04f, WHITE);
            DrawText(TextFormat("Bankai Cooldown: %.0fs(-5s)", showCd), startX + 60, startY + 2*(btnH + gapY) + 12, 32, BLACK);
            DrawTextureEx(coinTexture, (Vector2){(float)(startX + 500), (float)(startY + 2*(btnH + gapY) + 18)}, 0.0f, 0.03f, WHITE);
            DrawText(TextFormat("%d", UPGRADE_COST), startX + 540, startY + 2*(btnH + gapY) + 22, 28, DARKGRAY);
            if (minCd) DrawText("MIN", startX + 420, startY + 2*(btnH + gapY) + 12, 28, RED);

            // Bankai Mana Cost
            int showCost = getBankaiCost();
            bool minCost = (showCost <= BANKAI_COST_MIN);
            bool canBuyCost = !minCost && totalCoins >= UPGRADE_COST;
            if (!canBuyCost) DrawRectangleRec(btns[3], noBuy);
            DrawTextureEx(manaTexture, (Vector2){(float)(startX), (float)(startY + 3*(btnH + gapY) + 18)}, 0.0f, 0.11f, WHITE);
            DrawText(TextFormat("Bankai Cost: %d  (-2)", showCost), startX + 60, startY + 3*(btnH + gapY) + 12, 32, BLACK);
            DrawTextureEx(coinTexture, (Vector2){(float)(startX + 500), (float)(startY + 3*(btnH + gapY) + 18)}, 0.0f, 0.03f, WHITE);
            DrawText(TextFormat("%d", UPGRADE_COST), startX + 540, startY + 3*(btnH + gapY) + 22, 28, DARKGRAY);
            if (minCost) DrawText("MIN", startX + 420, startY + 3*(btnH + gapY) + 12, 28, RED);

            // --- SKIN SELECTION SLOT ---
            DrawText("Selected Skin:", startX + 60, startY + 4*(btnH+gapY) + 12, 32, DARKBLUE);
            char skinName[32];
            if (selectedSkin == 0) strcpy(skinName, "YUSEOL");
            else if (selectedSkin == 1) strcpy(skinName, "ITACHI");
            else if (selectedSkin == 2) strcpy(skinName, "GOKU");
            else strcpy(skinName, "???");

            DrawText(skinName, startX + 300, startY + 4*(btnH+gapY) + 12, 32, (Color){40, 140, 220, 255});
            // Preview: show first player frame of selected skin

            float previewScale = 0.13f; // You can adjust this
            DrawTextureEx(skinPreviews[selectedSkin], {(float)(startX+10), (float)(startY + 4*(btnH+gapY) + 8)}, 0.0f, previewScale, WHITE);

            // --- UPGRADE BUYING LOGIC ---
            if (currentState == SHOP && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();
                if (CheckCollisionPointRec(mouse, btns[0]) && canBuyHealth) {
                    maxHealth += HEALTH_MANA_STEP;
                    if (maxHealth > 200) maxHealth = 200;
                    totalCoins -= UPGRADE_COST;
                    SaveUpgrades();
                    SaveStats();
                }
                if (CheckCollisionPointRec(mouse, btns[1]) && canBuyMana) {
                    maxMana += HEALTH_MANA_STEP;
                    if (maxMana > 200) maxMana = 200;
                    totalCoins -= UPGRADE_COST;
                    SaveUpgrades();
                    SaveStats();
                }
                if (CheckCollisionPointRec(mouse, btns[2]) && canBuyCd) {
                    bankaiCooldownUpgrade++;
                    totalCoins -= UPGRADE_COST;
                    SaveUpgrades();
                    SaveStats();
                }
                if (CheckCollisionPointRec(mouse, btns[3]) && canBuyCost) {
                    bankaiManaCostUpgrade++;
                    totalCoins -= UPGRADE_COST;
                    SaveUpgrades();
                    SaveStats();
                }
                if (CheckCollisionPointRec(mouse, btns[4])) {
                    selectedSkin = (selectedSkin + 1) % NUM_SKINS;
                    SaveUpgrades();
                    LoadSkin(selectedSkin);
                }
            }
        }

        if (currentState == GAME) {
            float offset2 = fmodf(bg2Offset, bg2.width);
            DrawTexturePro(bg2, { offset2, 0, (float)bg2.width, (float)bg2.height }, { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);
            DrawTexturePro(bg2, { offset2 - bg2.width, 0, (float)bg2.width, (float)bg2.height }, { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);

            float timer = std::fmod(dayNightTimer, dayNightDuration);
            int phaseIndex = (int)(timer / phaseDuration);
            float t = (timer - phaseIndex * phaseDuration) / phaseDuration;
            Color skyOverlay = LerpColor(phaseColors[phaseIndex], phaseColors[(phaseIndex + 1) % numPhases], t);
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, skyOverlay);

            for (const auto& s : snowflakes) {
                DrawCircleV((Vector2){s.x, s.y}, s.size, (Color){255, 255, 255, (unsigned char)(240 * s.opacity)});
            }

            if (raining) {
                for (const auto &drop : rainDrops) {
                    DrawLineEx(
                        (Vector2){ drop.x, drop.y },
                        (Vector2){ drop.x, drop.y + drop.length },
                        drop.thickness,
                        (Color){160, 180, 255, (unsigned char)(220 * drop.opacity)}
                    );
                }
            }

            // --- During first 5 seconds, only draw base UI! ---
            if (spawnBlockTimer <= 5.0f) {
                DrawTextureEx(playerFrames[currentFrame], playerPos, 0.0f, playerScale, WHITE);
                DrawTextureEx(coinTexture, { 20, 20 }, 0.0f, coinScale, WHITE);
                DrawText(TextFormat("%d", coinCount), 80, 25, 40, DARKPURPLE);
                float iconOffsetY = 20 + coinTexture.height * coinScale + 12;
                DrawTextureEx(healthTexture, { 20, iconOffsetY }, 0.0f, 0.05, WHITE);
                DrawText(TextFormat("%d", health), 80, (int)iconOffsetY + 5, 40, RED);
                float manaOffsetY = iconOffsetY + healthTexture.height * coinScale + 12;
                DrawTextureEx(manaTexture, { 20, manaOffsetY }, 0.0f, 0.1, WHITE);
                DrawText(TextFormat("%d", mana), 80, (int)manaOffsetY + 5, 40, BLUE);

                DrawTextureEx(isPaused ? resumeIcon : pauseIcon,
                    {pauseRect.x, pauseRect.y}, 0.0f, (float)iconSize / pauseIcon.width, WHITE);

                DrawTexturePro(
                    isSoundOn ? soundOnTexture : soundOffTexture,
                    {0, 0, (float)soundOnTexture.width, (float)soundOnTexture.height},
                    {soundRect.x, soundRect.y, iconSize, iconSize},
                    {0, 0}, 0, WHITE
                );

                DrawText(
                "Sound",
                soundRect.x - 16 - MeasureText("Sound", labelFontSize),
                soundRect.y + (iconSize - labelFontSize)/2,
                labelFontSize,
                DARKGRAY
            );

                EndDrawing();
                continue;
            }

            if (!gameIntroActive) {
                for (const auto& b : birds) if(b.active) DrawTextureEx(birdTexture, b.position, 0.0f, b.scale, WHITE);
                for (const auto& t : trees) if (t.active) DrawTextureEx(treeTexture, t.position, 0.0f, treeScale, WHITE);
                for (const auto& c : coins) if (c.active) DrawTextureEx(coinTexture, c.position, 0.0f, coinScale, WHITE);
                for (const auto& m : magnets) if (m.active) DrawTextureEx(magnetTexture, m.position, 0.0f, magnetScale, WHITE);
                for (const auto& r : rocks) if (r.active) DrawTextureEx(rockTexture, r.position, 0.0f, rockScale, WHITE);
                if (ic.active && !ic.destroyed) DrawTextureEx(icTexture, ic.position, 0.0f, icScale, WHITE);
                else if (ic.destroyed) DrawTextureEx(icDestroyedTexture, ic.position, 0.0f, icScale, WHITE);
                for (const auto& p : burstParticles) {
                    Color c = p.color; float fade = p.life / p.maxLife; c.a = (unsigned char)(255 * fade);
                    DrawCircleV(p.position, 6, c);
                }
            }
            DrawTextureEx(onGround ? playerFrames[currentFrame] : jumpFrames[currentFrame], playerPos, 0.0f, playerScale, WHITE);

            if (!gameIntroActive) {
                DrawTextureEx(coinTexture, { 20, 20 }, 0.0f, coinScale, WHITE);
                DrawText(TextFormat("%d", coinCount), 80, 25, 40, DARKPURPLE);
                float iconOffsetY = 20 + coinTexture.height * coinScale + 12;
                DrawTextureEx(healthTexture, { 20, iconOffsetY }, 0.0f, 0.05, WHITE);
                DrawText(TextFormat("%d", health), 80, (int)iconOffsetY + 5, 40, RED);
                float manaOffsetY = iconOffsetY + healthTexture.height * coinScale + 12;
                DrawTextureEx(manaTexture, { 20, manaOffsetY }, 0.0f, 0.1, WHITE);
                DrawText(TextFormat("%d", mana), 80, (int)manaOffsetY + 5, 40, BLUE);

                if (magnetActive) DrawText(TextFormat("MAGNET: %.1fs", magnetTimer), 20, 180, 30, RED);
                if (icSlowing) DrawText("Slowed!", SCREEN_WIDTH / 2 - 70, 70, 36, SKYBLUE);
                DrawTextureEx(isPaused ? resumeIcon : pauseIcon,
                    {pauseRect.x, pauseRect.y}, 0.0f, (float)iconSize / pauseIcon.width, WHITE);
                if (bankaiCooldown > 0.0f) {
                    DrawText(TextFormat("Bankai: %.0fs", ceilf(bankaiCooldown)),
                             SCREEN_WIDTH / 2 - 90, SCREEN_HEIGHT - 70, 38, RED);
                }
                if (raining)
                    DrawText("RAIN", SCREEN_WIDTH - 180, 60, 40, (Color){80, 80, 220, 170});
            }
            if (bankaiActive && bankaiFlashTimer > 0.0f) {
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(WHITE, 0.80f));
            }
            if (bankaiActive && bankaiTextAlpha > 0.0f) {
                int fontSize = 120;
                const char* text = "BANKAI";
                int textW = MeasureText(text, fontSize);
                DrawText(text, SCREEN_WIDTH/2 - textW/2, SCREEN_HEIGHT/3, fontSize, Fade(RED, bankaiTextAlpha));
            }
            if (lowManaMsg) {
                float alpha = (lowManaMsgTimer / BANKAI_TEXT_FADE);
                if (alpha > 1.0f) alpha = 1.0f;
                int warnFont = 56;
                const char* text = "Low Mana!";
                int txtW = MeasureText(text, warnFont);
                DrawText(text, SCREEN_WIDTH/2 - txtW/2, SCREEN_HEIGHT/2 - warnFont/2,
                    warnFont, (Color){255, 40, 40, (unsigned char)(255*alpha)});
            }
        }
        // ---- BANKAI FLASH & FADE ----
        if (bankaiActive) {
            if (bankaiFlashTimer > 0.0f) {
                bankaiFlashTimer -= dt;
                if (bankaiFlashTimer < 0.0f) bankaiFlashTimer = 0.0f;
            } else if (bankaiTextAlpha > 0.0f) {
                bankaiTextAlpha -= dt*1.5f;
                if (bankaiTextAlpha < 0.0f) { bankaiTextAlpha = 0.0f; bankaiActive = false; }
            }
        }
        if (lowManaMsg) {
            lowManaMsgTimer -= dt;
            if (lowManaMsgTimer <= 0.0f) {
                lowManaMsg = false;
                lowManaMsgTimer = 0.0f;
            }
        }

        // ------------ GAME OVER STATE --------------
        if (currentState == GAME_OVER_STATE) {
            if (rainSoundPlaying) { StopSound(rainSnd); rainSoundPlaying = false; }
            float centerX = SCREEN_WIDTH / 2.0f;
            float centerY = SCREEN_HEIGHT / 2.0f;
            float coinDisplayScale = 0.05f;
            float coinIconW = coinTexture.width * coinDisplayScale;
            float coinIconH = coinTexture.height * coinDisplayScale;
            int fontMed = tapFontSize;
            DrawTextureEx(coinTexture,
                { centerX - coinIconW / 2 - 40, centerY - 160 }, 0.0f, coinDisplayScale, (Color){255,255,255,210});
            DrawText(TextFormat("%d", lastRunCoinCount),
                (int)(centerX + coinIconW / 2), (int)(centerY - 160 + coinIconH / 2 - fontMed / 2), fontMed, (Color){255,215,0,210});

            if (waitingNameInput) {
                const char* congrats = "Congratulations! High Score! Enter your name";
                int congratsFont = tapFontSize;
                int congratsWidth = MeasureText(congrats, congratsFont);
                int congratsY = centerY - 160 + coinIconH + 20;
                DrawText(congrats, centerX - congratsWidth/2, congratsY, congratsFont, (Color){30,30,30,210});
                int inputBoxW = 400, inputBoxH = 48;
                int inputBoxX = centerX - inputBoxW / 2;
                int inputBoxY = congratsY + congratsFont + 20;
                DrawRectangle(inputBoxX, inputBoxY, inputBoxW, inputBoxH, (Color){255,255,255,170});
                DrawText(nameInput, inputBoxX+16, inputBoxY+8, 32, BLACK);
                int key = GetCharPressed();
                while (key > 0) {
                    int len = strlen(nameInput);
                    if ((key >= 32) && (key <= 125) && (len < MAX_NAME_LEN)) {
                        nameInput[len] = (char)key;
                        nameInput[len+1] = '\0';
                    }
                    key = GetCharPressed();
                }
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    int len = strlen(nameInput);
                    if (len > 0) nameInput[len-1] = '\0';
                }
                if (IsKeyPressed(KEY_ENTER) && strlen(nameInput)>0) {
                    strncpy(highScores[insertIndex].name, nameInput, MAX_NAME_LEN);
                    SaveHighScores();
                    waitingNameInput = false;
                    strcpy(nameInput,"");
                    currentState = MENU;
                    playerPos.x = PLAYER_X;
                    playerPos.y = GROUND_Y - playerHeight;
                    verticalSpeed = 0;
                    onGround = true;
                    standUpCurrentFrame = 0;
                }
            } else {
                const char* retryText = "Tap anywhere to return to Menu";
                int retryWidth = MeasureText(retryText, tapFontSize);
                DrawText(retryText, centerX - retryWidth/2, SCREEN_HEIGHT - 140, tapFontSize, LIGHTGRAY);
                if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || GetKeyPressed() != 0) && !waitingNameInput) {
                    currentState = MENU;
                    playerPos.x = PLAYER_X;
                    playerPos.y = GROUND_Y - playerHeight;
                    verticalSpeed = 0;
                    onGround = true;
                    standUpCurrentFrame = 0;
                }
            }
        }

        if (currentState == HIGH_SCORE) {
            DrawText("HIGH SCORE", SCREEN_WIDTH / 2 - 140, 150, 40, RED);
            DrawText(TextFormat("Best Score: %d", highScore), SCREEN_WIDTH / 2 - 120, 240, 28, BLACK);
            DrawText("Top 5 Highscores:", SCREEN_WIDTH / 2 - 140, 300, 28, WHITE);
            for (int i = 0; i < MAX_HIGHSCORES; i++) {
                DrawText(TextFormat("%d. %-20s %5d", i+1, highScores[i].name, highScores[i].score), SCREEN_WIDTH/2 - 120, 340 + i*38, 28, (i==0?GOLD: (i==1)?(Color){192,192,192,255}: (i==2)?(Color){205,127,50,255}:WHITE));
            }
        }
        if (currentState == ZEN_TRANSITION || currentState == ZEN_TRANSITION_BACK || currentState == ZEN_MODE) {
            float t = (currentState == ZEN_TRANSITION) ? transitionX : (currentState == ZEN_TRANSITION_BACK ? transitionX : SCREEN_WIDTH);
            float bg1X = -t, bg2X = SCREEN_WIDTH - t;
            float bg1off = fmodf(bg1Offset, bg1.width);
            DrawTexturePro(bg1, { bg1off, 0, (float)bg1.width, (float)bg1.height }, { bg1X, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);
            float bg2off = fmodf(bg2Offset, bg2.width);
            DrawTexturePro(bg2, { bg2off, 0, (float)bg2.width, (float)bg2.height }, { bg2X, 0, SCREEN_WIDTH, SCREEN_HEIGHT }, { 0, 0 }, 0, WHITE);
            if (t > 0) {
                
                  // Credits and How to Play - Multi-line Centered              
                const char* credits[] = {
                    "Credits",
                    "A.S.Ayon ",
                    "Tahsin Mubbassir",
                    "Tilde Ipson ",
                    "Nahid",
                    "",
                    "How to Play",
                    "Explore and Enjoy"
                };
                int numCredits = sizeof(credits)/sizeof(credits[0]);
                int baseFont = tapFontSize + 8;
                int subFont = tapFontSize;

                int y = SCREEN_HEIGHT / 2 - (numCredits * subFont) / 2 - 30;

                for (int i = 0; i < numCredits; i++) {
                    int font = (i == 0 || i == 5) ? baseFont : subFont;
                    int width = MeasureText(credits[i], font);
                    DrawText(
                        credits[i],
                        (int)(bg2X + SCREEN_WIDTH / 2 - width / 2),
                        y,
                        font,
                        (Color){ 30, 30, 30, tapTextAlpha }
                    );
                    y += font + ((i == 0 || i == 5) ? 16 : 6);
                }

            }
        }

        DrawTexturePro(
            isSoundOn ? soundOnTexture : soundOffTexture,
            {0, 0, (float)soundOnTexture.width, (float)soundOnTexture.height},
            {soundRect.x, soundRect.y, iconSize, iconSize},
            {0, 0}, 0, WHITE
        );
        DrawText(
            "Sound",
            soundRect.x - 16 - MeasureText("Sound", labelFontSize),
            soundRect.y + (iconSize - labelFontSize)/2,
            labelFontSize,
            DARKGRAY
        );

        // Draw Exit Button
        DrawTextureEx(exitIcon, {exitRect.x, exitRect.y}, 0.0f, (float)iconSize / exitIcon.width, WHITE);

        // Show "Exit to Menu" if in GAME, SHOP, or HIGH_SCORE
        int exitFontSize = labelFontSize; // keep consistent with other icon labels
        const char* exitLabel = (
            currentState == GAME ||
            currentState == SHOP ||
            currentState == HIGH_SCORE
        ) ? "Exit to Menu" : "Exit Game";

        int exitTextX = exitRect.x + iconSize + 16; // 16 px gap to the right of icon
        int exitTextY = exitRect.y + (iconSize - exitFontSize) / 2; // vertical align with icon

        DrawText(exitLabel, exitTextX, exitTextY, exitFontSize, DARKGRAY);



        // Exit Button Click
        if (!exitDialogOpen && CheckCollisionPointRec(GetMousePosition(), exitRect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            exitDialogOpen = true;
        }

        if (exitDialogOpen) {
            // Dialog background
            int dialogW = 440, dialogH = 200;
            int dialogX = SCREEN_WIDTH / 2 - dialogW / 2;
            int dialogY = SCREEN_HEIGHT / 2 - dialogH / 2;

            DrawRectangle(dialogX, dialogY, dialogW, dialogH, (Color){80, 160, 255, 210});
            DrawRectangleLines(dialogX, dialogY, dialogW, dialogH, GRAY);

            // Confirmation text
            const char* areYouSure = "Sure to Exit?";
            int fontSize = 34;
            int textW = MeasureText(areYouSure, fontSize);
            DrawText(areYouSure, dialogX + (dialogW - textW) / 2, dialogY + 36, fontSize, WHITE);

            // Yes/No buttons
            int btnW = 120, btnH = 48, btnY = dialogY + 120;
            exitYesBtn = { (float)(dialogX + 44), (float)btnY, (float)btnW, (float)btnH };
            exitNoBtn  = { (float)(dialogX + dialogW - btnW - 44), (float)btnY, (float)btnW, (float)btnH };

            Color yesColor = (CheckCollisionPointRec(GetMousePosition(), exitYesBtn) ? SKYBLUE : DARKGRAY);
            Color noColor  = (CheckCollisionPointRec(GetMousePosition(), exitNoBtn) ? PINK : DARKGRAY);

            DrawRectangleRec(exitYesBtn, yesColor);
            DrawText("Yes", exitYesBtn.x + 32, exitYesBtn.y + 8, 32, WHITE);
            DrawRectangleRec(exitNoBtn, noColor);
            DrawText("No",  exitNoBtn.x  + 36, exitNoBtn.y  + 8, 32, WHITE);

            // Prevent clicking other game elements while dialog is open!
        }
        EndDrawing();

                if (exitDialogOpen) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();

                if (CheckCollisionPointRec(mouse, exitYesBtn)) {
                    if (currentState == GAME || currentState == SHOP || currentState == HIGH_SCORE) {
                        // Return to menu instead of closing game
                        if (currentState == GAME && coinCount > 0) {
                            if (coinCount > highScore) highScore = coinCount;
                            totalCoins += coinCount;
                            SaveStats();
                        }
                        currentState = MENU;
                    } else {
                        // Fully exit game in menu or other states
                        SaveStats();
                        SaveSoundState();
                        SaveHighScores();
                        SaveUpgrades();
                        UnloadAssets();
                        UnloadMusicStream(bgm1);
                        CloseAudioDevice();
                        CloseWindow();
                        exit(0);
                    }
                    exitDialogOpen = false;
                }


                if (CheckCollisionPointRec(mouse, exitNoBtn)) {
                    exitDialogOpen = false; // Close dialog
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                exitDialogOpen = false;
            }
        }


        // --- High Score Name Insert (if needed) ---
        if (currentState == GAME_OVER_STATE && !waitingNameInput) {
            int idx = -1;
            for (int i = 0; i < MAX_HIGHSCORES; i++) {
                if (lastRunCoinCount > highScores[i].score) {
                    idx = i;
                    break;
                }
            }
            if (idx >= 0) {
                for (int j = MAX_HIGHSCORES-1; j > idx; j--)
                    highScores[j] = highScores[j-1];
                highScores[idx].score = lastRunCoinCount;
                strcpy(highScores[idx].name, "");
                insertIndex = idx;
                waitingNameInput = true;
                strcpy(nameInput,"");
            }
        }
    }

    SaveStats();
    SaveSoundState();
    SaveHighScores();
    SaveUpgrades();
    UnloadAssets();
    UnloadMusicStream(bgm1);

    CloseAudioDevice();
    CloseWindow();
    return 0;
} 