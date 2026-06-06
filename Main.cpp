// ============================================================
//  TANK BATTLE - Local Multiplayer Console Game
//  Supports 1-4 players on the same device
//  Gamemodes: Deathmatch, Last Man Standing, Team Battle, Capture the Flag
//  Compatible with Visual Studio 2019/2022 (Windows)
// ============================================================

#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <vector>
#include <string>
#include <conio.h>      // _kbhit(), _getch() - Windows only
#include <windows.h>    // SetConsoleCursorPosition, Sleep
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

// ============================================================
//  CONSTANTS  (must come before any globals that reference them)
// ============================================================

const int MAP_W   = 50;
const int MAP_H   = 22;
const int UI_TOP  = 0;
const int MAP_TOP = 4;

// ============================================================
//  CONSOLE UTILITIES
// ============================================================

HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// -- Back-buffer for flicker-free rendering --
// We maintain a CHAR_INFO grid and write it in one WriteConsoleOutput call.
// Total render area: MAP_W columns x MAP_H rows, offset by MAP_TOP rows.
static CHAR_INFO g_backBuf[22][50];   // [row][col] = MAP_H x MAP_W
// (front buffer removed - not needed with full-frame writes)
static bool      g_firstFrame = true;

void BufSet(int col, int row, char ch, int color) {
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H) return;
    g_backBuf[row][col].Char.AsciiChar = ch;
    g_backBuf[row][col].Attributes     = (WORD)color;
}

void SetColor(int color) {
    SetConsoleTextAttribute(hConsole, color);
}

void GotoXY(int x, int y) {
    COORD coord = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(hConsole, coord);
}

void HideCursor() {
    CONSOLE_CURSOR_INFO info = { 1, FALSE };
    SetConsoleCursorInfo(hConsole, &info);
}

void ClearScreen() {
    system("cls");
}

void ResetColor() { SetColor(7); }

// Colors
#define COL_WHITE    15
#define COL_YELLOW   14
#define COL_GREEN    10
#define COL_CYAN     11
#define COL_RED      12
#define COL_MAGENTA  13
#define COL_BLUE      9
#define COL_DARK_GRAY 8
#define COL_ORANGE   6



// Tile types
enum Tile { EMPTY = 0, WALL, DESTRUCTIBLE, FLAG_RED, FLAG_BLUE, BASE_RED, BASE_BLUE };

// ============================================================
//  STRUCTS
// ============================================================

struct Bullet {
    float x     = 0.0f;
    float y     = 0.0f;
    float dx    = 0.0f;
    float dy    = 0.0f;
    int ownerID = -1;
    bool active = false;
};

struct Player {
    float x            = 0.0f;
    float y            = 0.0f;
    int dx             = 1;
    int dy             = 0;
    int health         = 3;
    int maxHealth      = 3;
    int ammo           = 20;
    int score          = 0;
    int kills          = 0;
    int deaths         = 0;
    bool alive         = true;
    bool hasFlag       = false;  // CTF mode
    int team           = 0;      // 0 = red, 1 = blue
    int colorCode      = 15;
    char symbol        = '?';
    std::string name   = "Player";
    // Controls
    int keyUp          = 'w';
    int keyDown        = 's';
    int keyLeft        = 'a';
    int keyRight       = 'd';
    int keyShoot       = 'e';
    double shootCooldown = 0.0;
    double respawnTimer  = 0.0;
};

// Gamemode
enum GameMode { DEATHMATCH = 1, LAST_MAN_STANDING, TEAM_BATTLE, CAPTURE_THE_FLAG };

// ============================================================
//  GLOBALS
// ============================================================

int g_numPlayers = 2;
GameMode g_mode = DEATHMATCH;
int g_map[MAP_H][MAP_W];
std::vector<Player> g_players;
std::vector<Bullet> g_bullets;
bool g_isBot[4] = { false, false, false, false };

// AI state per bot
struct AIState {
    double thinkTimer  = 0.0;   // how long until next decision
    int    moveX       = 0;     // current chosen move direction
    int    moveY       = 0;
    double stuckTimer  = 0.0;   // time since we last moved
    float  lastX       = 0.0f;
    float  lastY       = 0.0f;
    bool   wander      = false; // true = picking random direction
    double wanderTimer = 0.0;
};
AIState g_aiState[4];
int g_scoreLimit = 10;
int g_timeLimit = 120; // seconds
double g_gameTime = 0;
bool g_running = false;
bool g_roundOver = false;
std::string g_message = "";
double g_messageTimer = 0;

// CTF flags
struct Flag { float x, y; bool atBase; int carrier; };
Flag g_flags[2]; // 0=red 1=blue
float g_baseX[2], g_baseY[2];

// ============================================================
//  MAP GENERATION
// ============================================================

void GenerateMap(int mapStyle) {
    // Clear map
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            g_map[y][x] = EMPTY;

    // Borders
    for (int x = 0; x < MAP_W; x++) { g_map[0][x] = WALL; g_map[MAP_H - 1][x] = WALL; }
    for (int y = 0; y < MAP_H; y++) { g_map[y][0] = WALL; g_map[y][MAP_W - 1] = WALL; }

    srand((unsigned)time(0) + mapStyle * 100);

    if (mapStyle == 0) {
        // Classic symmetric map
        int blocks[][2] = {
            {5,4},{5,5},{10,4},{10,5},{15,4},{15,5},{20,4},{20,5},{25,4},{25,5},{30,4},{30,5},{35,4},{35,5},{40,4},{40,5},
            {5,10},{5,11},{10,10},{10,11},{15,10},{15,11},{20,10},{20,11},{25,10},{25,11},{30,10},{30,11},{35,10},{35,11},{40,10},{40,11},
            {5,16},{5,17},{10,16},{10,17},{15,16},{15,17},{20,16},{20,17},{25,16},{25,17},{30,16},{30,17},{35,16},{35,17},{40,16},{40,17},
            {12,7},{12,8},{12,9},{24,7},{24,8},{24,9},{36,7},{36,8},{36,9},
            {12,13},{12,14},{12,15},{24,13},{24,14},{24,15},{36,13},{36,14},{36,15}
        };
        for (auto& b : blocks) {
            if (b[1] < MAP_H - 1 && b[0] < MAP_W - 1)
                g_map[b[1]][b[0]] = WALL;
        }
        // Destructible walls
        for (int i = 0; i < 20; i++) {
            int rx = 2 + rand() % (MAP_W - 4);
            int ry = 2 + rand() % (MAP_H - 4);
            if (g_map[ry][rx] == EMPTY) g_map[ry][rx] = DESTRUCTIBLE;
        }
    }
    else {
        // Random maze-style
        for (int y = 2; y < MAP_H - 2; y += 3) {
            for (int x = 2; x < MAP_W - 2; x += 4) {
                g_map[y][x] = WALL;
                if (rand() % 2) g_map[y][x + 1] = WALL;
            }
        }
        for (int i = 0; i < 25; i++) {
            int rx = 2 + rand() % (MAP_W - 4);
            int ry = 2 + rand() % (MAP_H - 4);
            if (g_map[ry][rx] == EMPTY) g_map[ry][rx] = DESTRUCTIBLE;
        }
    }
}

// ============================================================
//  PLAYER SETUP
// ============================================================

void SetupPlayers() {
    // Player colors and symbols
    int colors[] = { COL_YELLOW, COL_CYAN, COL_GREEN, COL_MAGENTA };
    char symbols[] = { '1', '2', '3', '4' };
    std::string names[] = { "Player 1", "Bot 2", "Bot 3", "Bot 4" };

    // Controls: W/S/A/D+Space, Arrow+Enter, I/K/J/L+P, Numpad
    // Using single chars for kbhit polling
    // P1: WASD + F
    // P2: Arrows (special) + G
    // P3: IJKL + H
    // P4: 8456 + 0 (numpad won't map nicely in kbhit; use number row)
    int kUp[]    = { 'w','i','t','b' };
    int kDown[]  = { 's','k','g','n' };
    int kLeft[]  = { 'a','j','f','v' };
    int kRight[] = { 'd','l','h','m' };
    int kShoot[] = { 'e','o','y',',' };

    // Spawn positions
    float spawnX[] = { 2.5f, (float)(MAP_W - 3.5f), 2.5f,               (float)(MAP_W - 3.5f) };
    float spawnY[] = { 2.5f, 2.5f,                  (float)(MAP_H - 3.5f),(float)(MAP_H - 3.5f) };
    int teams[]    = { 0, 1, 0, 1 };

    // Spawn facing directions: P1 right, P2 left, P3 right, P4 left
    int spawnDX[] = { 1, -1,  1, -1 };
    int spawnDY[] = { 0,  0,  0,  0 };

    g_players.clear();
    for (int i = 0; i < g_numPlayers; i++) {
        Player p;
        p.x = spawnX[i]; p.y = spawnY[i];
        p.dx = spawnDX[i]; p.dy = spawnDY[i];
        p.health = 3; p.maxHealth = 3;
        p.ammo = 20; p.score = 0;
        p.kills = 0; p.deaths = 0;
        p.alive = true; p.hasFlag = false;
        p.team = teams[i];
        p.colorCode = colors[i];
        p.symbol = symbols[i];
        p.name = names[i];
        p.keyUp = kUp[i]; p.keyDown = kDown[i];
        p.keyLeft = kLeft[i]; p.keyRight = kRight[i];
        p.keyShoot = kShoot[i];
        p.shootCooldown = 0;
        p.respawnTimer = 0;
        g_players.push_back(p);
    }

    // Reset AI state
    for (int i = 0; i < 4; i++) g_aiState[i] = AIState();

    // CTF bases
    g_baseX[0] = 2; g_baseY[0] = MAP_H / 2;
    g_baseX[1] = MAP_W - 3; g_baseY[1] = MAP_H / 2;
    g_flags[0] = { g_baseX[0], g_baseY[0], true, -1 };
    g_flags[1] = { g_baseX[1], g_baseY[1], true, -1 };
    if (g_mode == CAPTURE_THE_FLAG) {
        g_map[(int)g_baseY[0]][(int)g_baseX[0]] = BASE_RED;
        g_map[(int)g_baseY[1]][(int)g_baseX[1]] = BASE_BLUE;
    }
}

// ============================================================
//  RENDERING
// ============================================================

void DrawUI() {
    // Row 0: title bar + mode + time
    std::string modeStr;
    switch (g_mode) {
    case DEATHMATCH:         modeStr = "DEATHMATCH";       break;
    case LAST_MAN_STANDING:  modeStr = "LAST MAN STANDING"; break;
    case TEAM_BATTLE:        modeStr = "TEAM BATTLE";       break;
    case CAPTURE_THE_FLAG:   modeStr = "CAPTURE THE FLAG";  break;
    }
    int secs = (int)(g_timeLimit - g_gameTime);
    if (secs < 0) secs = 0;

    // Build row 0 string, pad to MAP_W
    char row0[64];
    sprintf(row0, " TANK BATTLE | %-18s| Time:%3ds | Limit:%2d ", modeStr.c_str(), secs, g_scoreLimit);
    GotoXY(0, 0);
    SetColor(COL_YELLOW);
    std::cout << std::left << std::setw(MAP_W) << row0;

    // Row 1: player stats - build full string then print once at GotoXY
    GotoXY(0, 1);
    SetColor(0); std::cout << std::string(MAP_W, ' ');
    GotoXY(0, 1);
    for (int i = 0; i < g_numPlayers; i++) {
        SetColor(g_players[i].colorCode);
        std::cout << g_players[i].name;
        SetColor(COL_WHITE);
        std::cout << ":HP[";
        SetColor(COL_RED);
        for (int h = 0; h < g_players[i].maxHealth; h++)
            std::cout << (h < g_players[i].health ? '*' : '.');
        SetColor(COL_WHITE);
        char stat[24];
        sprintf(stat, "]%d/%d ", g_players[i].score, g_players[i].kills);
        std::cout << stat;
        if (g_mode == CAPTURE_THE_FLAG && g_players[i].hasFlag) {
            SetColor(COL_YELLOW); std::cout << "F ";
        }
    }

    // Row 2: controls hint
    GotoXY(0, 2);
    SetColor(0); std::cout << std::string(MAP_W, ' ');
    GotoXY(0, 2);
    SetColor(COL_DARK_GRAY);
    char ctrl[128] = "Keys: ";
    const char* labels[] = {"P1:WASD+E","P2:IJKL+O","P3:TFGH+Y","P4:BVNM+,"};
    for (int i = 0; i < g_numPlayers; i++) {
        if (!g_isBot[i]) { strcat(ctrl, labels[i]); strcat(ctrl, " "); }
    }
    strcat(ctrl, " ESC=Quit");
    std::cout << ctrl;

    // Row 3: separator line
    GotoXY(0, 3);
    SetColor(COL_DARK_GRAY);
    std::cout << std::string(MAP_W, '-');

    ResetColor();
}

// Build entire back-buffer then flush only changed cells
void RenderFrame() {
    // Clear back buffer to empty/black
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            BufSet(x, y, ' ', 0);

    // Map tiles
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            switch (g_map[y][x]) {
            case WALL:         BufSet(x, y, (char)219, COL_DARK_GRAY); break;
            case DESTRUCTIBLE: BufSet(x, y, (char)177, COL_ORANGE);    break;
            case BASE_RED:     BufSet(x, y, 'B',       COL_RED);       break;
            case BASE_BLUE:    BufSet(x, y, 'B',       COL_BLUE);      break;
            default:           BufSet(x, y, ' ',       0);             break;
            }
        }
    }

    // Bullets
    for (auto& b : g_bullets) {
        if (!b.active) continue;
        int bx = (int)b.x, by = (int)b.y;
        char sym = (b.dx != 0) ? '-' : '|';
        BufSet(bx, by, sym, COL_YELLOW);
    }

    // CTF flags
    if (g_mode == CAPTURE_THE_FLAG) {
        for (int i = 0; i < 2; i++) {
            if (g_flags[i].carrier == -1) {
                BufSet((int)g_flags[i].x, (int)g_flags[i].y,
                       'F', i == 0 ? COL_RED : COL_BLUE);
            }
        }
    }

    // Players
    for (auto& p : g_players) {
        if (!p.alive) continue;
        // Draw facing indicator behind player
        int fx = (int)p.x - p.dx, fy = (int)p.y - p.dy;
        if (g_map[fy < 0 ? 0 : fy >= MAP_H ? MAP_H-1 : fy]
                 [fx < 0 ? 0 : fx >= MAP_W ? MAP_W-1 : fx] == EMPTY)
            BufSet(fx, fy, '.', COL_DARK_GRAY);
        BufSet((int)p.x, (int)p.y, p.symbol, p.colorCode);
    }

    // Respawn timers overlay
    for (int i = 0; i < (int)g_players.size(); i++) {
        if (!g_players[i].alive && g_players[i].respawnTimer > 0) {
            int rx = (int)g_baseX[i % 2 == 0 ? 0 : 1];
            int ry = (int)g_baseY[i % 2 == 0 ? 0 : 1];
            char c = '0' + (int)g_players[i].respawnTimer + 1;
            BufSet(rx, ry, c, COL_DARK_GRAY);
        }
    }

    // Message overlay (centered)
    if (g_messageTimer > 0 && !g_message.empty()) {
        int mx = MAP_W / 2 - (int)g_message.size() / 2 - 1;
        int my = MAP_H / 2;
        // Background bar
        for (int x = 0; x < MAP_W; x++) BufSet(x, my, ' ', COL_YELLOW * 16);
        BufSet(mx, my, ' ', COL_YELLOW * 16);
        for (int c = 0; c < (int)g_message.size(); c++)
            BufSet(mx + 1 + c, my, g_message[c], 0 | (COL_YELLOW << 4));
    }

    // Flush: WriteConsoleOutput for the whole map area in ONE call
    COORD bufSize   = { MAP_W, MAP_H };
    COORD bufOrigin = { 0, 0 };
    SMALL_RECT writeRegion = { 0, MAP_TOP, MAP_W - 1, MAP_TOP + MAP_H - 1 };
    WriteConsoleOutputA(hConsole, &g_backBuf[0][0], bufSize, bufOrigin, &writeRegion);
}

// Legacy stubs kept so call sites compile (they're replaced below)
void DrawMap()     {}
void DrawBullets() {}
void DrawPlayers() {}
void DrawMessage() {}

// ============================================================
//  COLLISION / PHYSICS
// ============================================================

bool IsWalkable(float nx, float ny) {
    int ix = (int)nx, iy = (int)ny;
    if (ix < 0 || ix >= MAP_W || iy < 0 || iy >= MAP_H) return false;
    return g_map[iy][ix] == EMPTY || g_map[iy][ix] == FLAG_RED ||
        g_map[iy][ix] == FLAG_BLUE || g_map[iy][ix] == BASE_RED || g_map[iy][ix] == BASE_BLUE;
}

// ============================================================
//  GAME LOGIC
// ============================================================

void ShowMessage(const std::string& msg, double dur = 2.0) {
    g_message = msg;
    g_messageTimer = dur;
}

void RespawnPlayer(int idx) {
    float spawnX[] = { 2.5f, (float)(MAP_W - 3.5f), 2.5f,               (float)(MAP_W - 3.5f) };
    float spawnY[] = { 2.5f, 2.5f,                  (float)(MAP_H - 3.5f),(float)(MAP_H - 3.5f) };
    g_players[idx].x = spawnX[idx];
    g_players[idx].y = spawnY[idx];
    g_players[idx].health = g_players[idx].maxHealth;
    g_players[idx].alive = true;
    g_players[idx].ammo = 20;
    g_players[idx].hasFlag = false;
    // Drop any held flag
    for (int f = 0; f < 2; f++) {
        if (g_flags[f].carrier == idx) {
            g_flags[f].atBase = false;
            g_flags[f].carrier = -1;
        }
    }
}

void KillPlayer(int victim, int killer) {
    Player& v = g_players[victim];
    v.alive = false;
    v.deaths++;
    v.hasFlag = false;
    // Drop flags
    for (int f = 0; f < 2; f++) {
        if (g_flags[f].carrier == victim) {
            g_flags[f].x = v.x;
            g_flags[f].y = v.y;
            g_flags[f].atBase = false;
            g_flags[f].carrier = -1;
        }
    }

    if (killer >= 0 && killer != victim) {
        g_players[killer].kills++;
        if (g_mode == DEATHMATCH || g_mode == LAST_MAN_STANDING)
            g_players[killer].score++;
    }

    std::string msg = v.name + " was eliminated!";
    ShowMessage(msg, 1.5);

    if (g_mode == LAST_MAN_STANDING) {
        // Count alive
        int alive = 0, lastAlive = -1;
        for (int i = 0; i < g_numPlayers; i++)
            if (g_players[i].alive) { alive++; lastAlive = i; }
        if (alive == 1) {
            g_players[lastAlive].score++;
            ShowMessage(g_players[lastAlive].name + " WINS THE ROUND!", 3.0);
            g_roundOver = true;
        }
        else if (alive == 0) {
            ShowMessage("DRAW!", 3.0);
            g_roundOver = true;
        }
    }
    else {
        // Respawn after delay (not LMS)
        v.respawnTimer = 3.0;
    }
}

void UpdateBullets(double dt) {
    float speed = 15.0f;
    for (auto& b : g_bullets) {
        if (!b.active) continue;
        b.x += b.dx * speed * (float)dt;
        b.y += b.dy * speed * (float)dt;

        int nx = (int)b.x, ny = (int)b.y;
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) { b.active = false; continue; }

        int tile = g_map[ny][nx];
        if (tile == WALL) { b.active = false; continue; }
        if (tile == DESTRUCTIBLE) {
            g_map[ny][nx] = EMPTY;
            b.active = false;
            continue;
        }

        // Check player hits
        for (int i = 0; i < g_numPlayers; i++) {
            if (i == b.ownerID || !g_players[i].alive) continue;
            if ((int)g_players[i].x == nx && (int)g_players[i].y == ny) {
                b.active = false;
                g_players[i].health--;
                ShowMessage(g_players[b.ownerID].name + " hit " + g_players[i].name + "!", 0.8);
                if (g_players[i].health <= 0)
                    KillPlayer(i, b.ownerID);
                break;
            }
        }
    }
    // Remove inactive
    g_bullets.erase(std::remove_if(g_bullets.begin(), g_bullets.end(),
        [](const Bullet& b) { return !b.active; }), g_bullets.end());
}

void Shoot(int playerIdx) {
    Player& p = g_players[playerIdx];
    if (p.shootCooldown > 0 || p.ammo <= 0 || !p.alive) return;
    p.ammo--;
    p.shootCooldown = 0.3;
    Bullet b;
    b.x = p.x + p.dx;
    b.y = p.y + p.dy;
    b.dx = (float)p.dx;
    b.dy = (float)p.dy;
    b.ownerID = playerIdx;
    b.active = true;
    g_bullets.push_back(b);
}

void MovePlayer(int idx, int ddx, int ddy) {
    Player& p = g_players[idx];
    if (!p.alive) return;
    // Always update facing direction when a direction key is pressed
    if (ddx != 0 || ddy != 0) { p.dx = ddx; p.dy = ddy; }
    float nx = p.x + ddx;
    float ny = p.y + ddy;
    // (old position erased by RenderFrame back-buffer)
    if (IsWalkable(nx, ny)) { p.x = nx; p.y = ny; }

    // CTF: pick up flag
    if (g_mode == CAPTURE_THE_FLAG) {
        for (int f = 0; f < 2; f++) {
            if (g_flags[f].carrier == -1 && !g_flags[f].atBase) {
                if ((int)p.x == (int)g_flags[f].x && (int)p.y == (int)g_flags[f].y) {
                    if (p.team != f) {
                        g_flags[f].carrier = idx;
                        p.hasFlag = true;
                        ShowMessage(p.name + " picked up the " + (f == 0 ? "RED" : "BLUE") + " flag!", 1.5);
                    }
                }
            }
        }
        // Move flag with carrier
        for (int f = 0; f < 2; f++) {
            if (g_flags[f].carrier == idx) {
                g_flags[f].x = p.x;
                g_flags[f].y = p.y;
            }
        }
        // Capture flag at own base
        int ownBase = p.team;
        if (p.hasFlag && (int)p.x == (int)g_baseX[ownBase] && (int)p.y == (int)g_baseY[ownBase]) {
            for (int f = 0; f < 2; f++) {
                if (g_flags[f].carrier == idx) {
                    // Reset enemy flag
                    g_flags[f].atBase = true;
                    g_flags[f].x = g_baseX[f];
                    g_flags[f].y = g_baseY[f];
                    g_flags[f].carrier = -1;
                    p.hasFlag = false;
                    p.score++;
                    // Award team
                    for (int pi = 0; pi < g_numPlayers; pi++)
                        if (g_players[pi].team == p.team)
                            g_players[pi].score++;
                    ShowMessage((p.team == 0 ? "RED" : "BLUE") + std::string(" team captured the flag! +1"), 2.0);
                    if (p.score >= g_scoreLimit) { g_roundOver = true; ShowMessage(p.name + " team WINS!", 4.0); }
                }
            }
        }
    }
}

// ============================================================
//  INPUT HANDLING (non-blocking polling)
// ============================================================

bool g_keys[256] = {};

void PollInput() {
    // GetAsyncKeyState works without any console mode changes
    // High bit set = key is currently held down
    for (int i = 0; i < 256; i++)
        g_keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
}

// Map char key to virtual key code
int CharToVK(char c) {
    if (c >= 'a' && c <= 'z') return VkKeyScanA(c) & 0xFF;
    if (c >= '0' && c <= '9') return c;
    if (c == ',') return VK_OEM_COMMA;
    return (unsigned char)c;
}

// ============================================================
//  MAIN MENU
// ============================================================

void DrawTitle() {
    ClearScreen();
    // Print each line at exact row using GotoXY so nothing bleeds
    const char* lines[] = {
        "  +--------------------------------------------------+",
        "  |                                                  |",
        "  |   _____ ___  _  _ _  __   ___  _ _____ _____ _  |",
        "  |  |_   _/ _ \\| \\| | |/ /  | _ )/_\\_   _|_   _| | |",
        "  |    | || (_) |  ` | ' <   | _ / _ \\| |   | | | |_||",
        "  |    |_| \\___/|_|\\_|_|\\_\\  |___/_/ \\_|_|   |_| |___||",
        "  |                                                  |",
        "  +--------------------------------------------------+",
        "",
        "    Local Multiplayer  --  1 to 4 Players on Same Device",
        "",
        "    Use UP/DOWN arrows or W/S to navigate, ENTER to select.",
    };
    int numLines = sizeof(lines) / sizeof(lines[0]);
    for (int i = 0; i < numLines; i++) {
        GotoXY(0, i);
        // Clear the line first
        SetColor(0);
        std::cout << std::string(78, ' ');
        GotoXY(0, i);
        if (i == 0 || i == 7) SetColor(COL_YELLOW);
        else if (i == 1 || i == 6) SetColor(COL_YELLOW);
        else if (i == 2 || i == 3 || i == 4 || i == 5) SetColor(COL_WHITE);
        else if (i == 9) SetColor(COL_CYAN);
        else if (i == 11) SetColor(COL_DARK_GRAY);
        else SetColor(COL_WHITE);
        std::cout << lines[i];
    }
    ResetColor();
}

void DrawBox(int x, int y, int w, int h, int color, const std::string& title) {
    SetColor(color);
    GotoXY(x, y); std::cout << (char)201;
    for (int i = 0; i < w - 2; i++) std::cout << (char)205;
    std::cout << (char)187;
    GotoXY(x, y + 1); std::cout << (char)186;
    SetColor(COL_YELLOW);
    std::cout << " " << title;
    for (int i = 0; i < w - 3 - (int)title.size(); i++) std::cout << ' ';
    SetColor(color); std::cout << (char)186;
    for (int row = 2; row < h - 1; row++) {
        GotoXY(x, y + row); std::cout << (char)186;
        GotoXY(x + w - 1, y + row); std::cout << (char)186;
        for (int c = x + 1; c < x + w - 1; c++) { GotoXY(c, y + row); std::cout << ' '; }
    }
    GotoXY(x, y + h - 1); std::cout << (char)200;
    for (int i = 0; i < w - 2; i++) std::cout << (char)205;
    std::cout << (char)188;
    ResetColor();
}

int Menu(const std::string& title, const std::vector<std::string>& options, int x, int y) {
    int sel = 0;
    int lineWidth = 60;
    while (true) {
        // Title line
        GotoXY(x, y);
        SetColor(0); std::cout << std::string(lineWidth, ' ');
        GotoXY(x, y);
        SetColor(COL_CYAN); std::cout << "  " << title;

        // Option lines
        for (int i = 0; i < (int)options.size(); i++) {
            GotoXY(x, y + 2 + i);
            SetColor(0); std::cout << std::string(lineWidth, ' ');
            GotoXY(x, y + 2 + i);
            if (i == sel) {
                SetColor(COL_YELLOW);
                std::cout << "  > " << options[i];
            } else {
                SetColor(COL_WHITE);
                std::cout << "    " << options[i];
            }
        }
        // Clear line below options
        GotoXY(x, y + 2 + (int)options.size());
        SetColor(0); std::cout << std::string(lineWidth, ' ');

        ResetColor();

        int c = _getch();
        if (c == 0 || c == 0xE0) c = _getch(); // consume extended key prefix
        if (c == 72 || c == 'w' || c == 'W') sel = (sel - 1 + (int)options.size()) % (int)options.size();
        if (c == 80 || c == 's' || c == 'S') sel = (sel + 1) % (int)options.size();
        if (c == 13 || c == ' ') return sel;
        if (c == 27) return -1;
    }
}

// ============================================================
//  SCOREBOARD / RESULTS
// ============================================================

void ShowResults() {
    ClearScreen();
    SetColor(COL_YELLOW);
    std::cout << "\n\n  ===== GAME OVER - RESULTS =====\n\n";
    // Sort by score
    std::vector<int> order;
    for (int i = 0; i < g_numPlayers; i++) order.push_back(i);
    std::sort(order.begin(), order.end(), [](int a, int b) {
        return g_players[a].score > g_players[b].score; });
    for (int r = 0; r < (int)order.size(); r++) {
        int i = order[r];
        SetColor(g_players[i].colorCode);
        std::cout << "  " << (r + 1) << ". " << g_players[i].name
            << "  Score:" << g_players[i].score
            << "  Kills:" << g_players[i].kills
            << "  Deaths:" << g_players[i].deaths << "\n";
    }
    // Winner
    SetColor(COL_YELLOW);
    std::cout << "\n  WINNER: " << g_players[order[0]].name << "!\n\n";
    ResetColor();
    std::cout << "  Press any key to return to menu...\n";
    (void)_getch();
}

// ============================================================
//  AI / BOT LOGIC
// ============================================================

// Simple BFS-style direction picker: try to step toward target
// Returns true if a walkable step was found
bool AIStepToward(int botIdx, float tx, float ty, int& outDX, int& outDY) {
    Player& p = g_players[botIdx];
    // Preferred direction
    int pdx = 0, pdy = 0;
    float diffX = tx - p.x, diffY = ty - p.y;
    if (fabsf(diffX) >= fabsf(diffY)) pdx = (diffX > 0) ? 1 : -1;
    else                               pdy = (diffY > 0) ? 1 : -1;

    // Try preferred, then perpendicular, then opposite perpendicular
    int tries[4][2] = {
        {pdx, pdy},
        {pdy, pdx},      // rotate 90
        {-pdy, -pdx},    // rotate -90
        {-pdx, -pdy}     // opposite
    };
    for (auto& t : tries) {
        if (t[0] == 0 && t[1] == 0) continue;
        if (IsWalkable(p.x + t[0], p.y + t[1])) {
            outDX = t[0]; outDY = t[1];
            return true;
        }
    }
    return false;
}

void UpdateAI(int idx, double dt) {
    if (!g_isBot[idx]) return;
    Player& p = g_players[idx];
    AIState& ai = g_aiState[idx];

    if (!p.alive) return;

    // Shoot cooldown handled by main loop already
    // Cooldown our own think timer
    ai.thinkTimer -= dt;

    // -- Find nearest human/enemy --
    int targetIdx = -1;
    float bestDist = 1e9f;
    for (int i = 0; i < g_numPlayers; i++) {
        if (i == idx || !g_players[i].alive) continue;
        if (g_mode == TEAM_BATTLE && g_players[i].team == p.team) continue;
        float dx = g_players[i].x - p.x;
        float dy = g_players[i].y - p.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < bestDist) { bestDist = dist; targetIdx = i; }
    }

    // -- Shoot if lined up with a target --
    if (targetIdx >= 0) {
        Player& tgt = g_players[targetIdx];
        float dx = tgt.x - p.x, dy = tgt.y - p.y;
        // Same row or column, within range
        bool lineX = (fabsf(dy) < 1.0f) && (fabsf(dx) < 12.0f);
        bool lineY = (fabsf(dx) < 1.0f) && (fabsf(dy) < 12.0f);
        if (lineX || lineY) {
            // Face target
            if (lineX) { p.dx = (dx > 0) ? 1 : -1; p.dy = 0; }
            else        { p.dy = (dy > 0) ? 1 : -1; p.dx = 0; }
            Shoot(idx);
        }
    }

    // -- CTF: go for flag if don't have it, else return to base --
    float goalX = p.x, goalY = p.y;
    if (g_mode == CAPTURE_THE_FLAG) {
        int enemyFlag = 1 - p.team;
        if (!p.hasFlag) {
            // Go grab enemy flag if it's dropped or at base
            goalX = g_flags[enemyFlag].x;
            goalY = g_flags[enemyFlag].y;
        } else {
            // Return to own base
            goalX = g_baseX[p.team];
            goalY = g_baseY[p.team];
        }
    } else if (targetIdx >= 0) {
        // Chase target
        goalX = g_players[targetIdx].x;
        goalY = g_players[targetIdx].y;
    }

    // -- Move every thinkTimer tick --
    if (ai.thinkTimer <= 0.0) {
        ai.thinkTimer = 0.13 + (rand() % 5) * 0.01; // slight randomness

        // Stuck detection
        float movedDist = fabsf(p.x - ai.lastX) + fabsf(p.y - ai.lastY);
        ai.lastX = p.x; ai.lastY = p.y;

        if (movedDist < 0.5f) {
            ai.stuckTimer += ai.thinkTimer;
        } else {
            ai.stuckTimer = 0.0;
            ai.wander = false;
        }

        // If stuck for >0.6s, wander randomly
        if (ai.stuckTimer > 0.6) {
            ai.wander = true;
            ai.wanderTimer = 0.5 + (rand() % 4) * 0.15;
            ai.stuckTimer = 0.0;
            // Pick a random walkable direction
            int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            int r = rand() % 4;
            ai.moveX = dirs[r][0];
            ai.moveY = dirs[r][1];
        }

        if (ai.wander) {
            ai.wanderTimer -= ai.thinkTimer;
            if (ai.wanderTimer <= 0) ai.wander = false;
            // Try wander dir, else pick new random
            if (!IsWalkable(p.x + ai.moveX, p.y + ai.moveY)) {
                int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                int r = rand() % 4;
                ai.moveX = dirs[r][0];
                ai.moveY = dirs[r][1];
            }
        } else {
            // Navigate toward goal
            int dx = 0, dy = 0;
            if (!AIStepToward(idx, goalX, goalY, dx, dy)) {
                // Fallback random
                int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                int r = rand() % 4;
                dx = dirs[r][0]; dy = dirs[r][1];
            }
            ai.moveX = dx; ai.moveY = dy;
        }

        MovePlayer(idx, ai.moveX, ai.moveY);
    }
}

// ============================================================
//  GAME LOOP
// ============================================================

void GameLoop() {
    HideCursor();
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    // Keep ENABLE_PROCESSED_INPUT so Ctrl+C doesn't crash; disable echo/line only
    SetConsoleMode(hIn, ENABLE_PROCESSED_INPUT);

    ClearScreen();
    GenerateMap(rand() % 2);
    SetupPlayers();

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    g_gameTime = 0;
    g_running = true;
    g_roundOver = false;
    g_bullets.clear();

    // Reset static timers
    g_firstFrame = true;

    DrawMap();

    while (g_running) {
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
        prev = now;
        if (dt > 0.05) dt = 0.05;

        g_gameTime += dt;

        // Update timers
        if (g_messageTimer > 0) g_messageTimer -= dt;

        // ---- INPUT ----
        PollInput();

        // ESC = quit
        if (g_keys[VK_ESCAPE]) { g_running = false; break; }

        // Player movement (once per ~0.12s to avoid too fast)
        static double moveTimer = 0;
        moveTimer += dt;
        bool doMove = moveTimer >= 0.12;
        if (doMove) moveTimer = 0;

        for (int i = 0; i < g_numPlayers; i++) {
            Player& p = g_players[i];
            if (p.shootCooldown > 0) p.shootCooldown -= dt;

            // Respawn
            if (!p.alive && g_mode != LAST_MAN_STANDING) {
                p.respawnTimer -= dt;
                if (p.respawnTimer <= 0) RespawnPlayer(i);
            }

            if (!p.alive || !doMove) continue;

            if (g_isBot[i]) continue; // bots handled by UpdateAI

            int vkUp = CharToVK(p.keyUp), vkDown = CharToVK(p.keyDown);
            int vkLeft = CharToVK(p.keyLeft), vkRight = CharToVK(p.keyRight);
            int vkShoot = CharToVK(p.keyShoot);

            if (g_keys[vkUp])    MovePlayer(i, 0, -1);
            if (g_keys[vkDown])  MovePlayer(i, 0, 1);
            if (g_keys[vkLeft])  MovePlayer(i, -1, 0);
            if (g_keys[vkRight]) MovePlayer(i, 1, 0);
            if (g_keys[vkShoot]) Shoot(i);
        }

        // ---- AI BOTS ----
        for (int i = 0; i < g_numPlayers; i++)
            if (g_isBot[i]) UpdateAI(i, dt);

        UpdateBullets(dt);

        // Ammo regen
        static double ammoTimer = 0;
        ammoTimer += dt;
        if (ammoTimer > 5.0) {
            ammoTimer = 0;
            for (auto& p : g_players) if (p.ammo < 20) p.ammo++;
        }

        // Check win conditions
        if (!g_roundOver) {
            if (g_mode == DEATHMATCH || g_mode == TEAM_BATTLE) {
                for (auto& p : g_players)
                    if (p.score >= g_scoreLimit) { g_roundOver = true; ShowMessage(p.name + " WINS!", 3.0); }
            }
            if (g_gameTime >= g_timeLimit) {
                g_roundOver = true;
                // Find highest score
                int best = 0;
                for (int i = 1; i < g_numPlayers; i++)
                    if (g_players[i].score > g_players[best].score) best = i;
                ShowMessage("TIME UP! " + g_players[best].name + " wins!", 3.0);
            }
        }
        if (g_roundOver) {
            // Give 3 secs then exit
            static double endTimer = 0;
            endTimer += dt;
            if (endTimer > 3.0) { endTimer = 0; g_running = false; }
        }

        // ---- RENDER ----
        DrawUI();
        RenderFrame();   // single WriteConsoleOutput - no flicker

        Sleep(16); // ~60fps cap
    }

    // Restore console
    SetConsoleMode(hIn, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
}

// ============================================================
//  MAIN
// ============================================================

int main() {
    srand((unsigned)time(0));
    HideCursor();

    while (true) {
        // ---- Step 1: Player count ----
        DrawTitle();
        std::vector<std::string> playerOpts = {
            "1 Player (vs 3 AI bots)",
            "2 Players",
            "3 Players",
            "4 Players"
        };
        int pc = Menu("Select Number of Players  [UP/DOWN + ENTER]:", playerOpts, 2, 13);
        if (pc < 0) break;
        for (int i = 0; i < 4; i++) g_isBot[i] = false;
        if (pc == 0) {
            g_numPlayers = 4;
            g_isBot[1] = true; g_isBot[2] = true; g_isBot[3] = true;
        } else {
            g_numPlayers = pc + 1;
        }

        // ---- Step 2: Game mode ----
        DrawTitle();
        std::vector<std::string> modeOpts = {
            "Deathmatch        - Most kills wins, respawn on",
            "Last Man Standing - No respawn, survive!",
            "Team Battle       - 2v2, teams share score",
            "Capture the Flag  - Grab enemy flag, return home"
        };
        int m = Menu("Select Game Mode  [UP/DOWN + ENTER]:", modeOpts, 2, 13);
        if (m < 0) continue;
        g_mode = (GameMode)(m + 1);

        // ---- Step 3: Match length ----
        DrawTitle();
        std::vector<std::string> limitOpts = {
            "Quick  -  5 pts / 60 seconds",
            "Normal - 10 pts / 120 seconds",
            "Long   - 20 pts / 300 seconds"
        };
        int lim = Menu("Select Match Length  [UP/DOWN + ENTER]:", limitOpts, 2, 13);
        if (lim == 0)      { g_scoreLimit = 5;  g_timeLimit = 60; }
        else if (lim == 1) { g_scoreLimit = 10; g_timeLimit = 120; }
        else               { g_scoreLimit = 20; g_timeLimit = 300; }

        // ---- Step 4: Controls reminder ----
        DrawTitle();
        int row = 13;
        auto PrintRow = [&](int col, const std::string& s) {
            GotoXY(col, row++);
            SetColor(0); std::cout << std::string(60, ' ');
            GotoXY(col, row - 1);
        };
        GotoXY(2, row); SetColor(COL_YELLOW);
        std::cout << "  CONTROLS (all players share one keyboard):";
        row++;
        GotoXY(2, row++); SetColor(COL_WHITE);  std::cout << "  Player 1 : W A S D  to move,  E to shoot";
        GotoXY(2, row++); SetColor(COL_CYAN);   std::cout << "  Player 2 : I J K L  to move,  O to shoot";
        GotoXY(2, row++); SetColor(COL_GREEN);  std::cout << "  Player 3 : T F G H  to move,  Y to shoot";
        GotoXY(2, row++); SetColor(COL_MAGENTA);std::cout << "  Player 4 : B V N M  to move,  , to shoot";
        GotoXY(2, row++); SetColor(COL_DARK_GRAY); std::cout << "  ESC = quit game";
        row++;
        GotoXY(2, row);   SetColor(COL_YELLOW); std::cout << "  Press any key to START...";
        ResetColor();
        (void)_getch();

        GameLoop();
        ShowResults();
    }

    ClearScreen();
    SetColor(COL_YELLOW);
    std::cout << "\n  Thanks for playing TANK BATTLE!\n\n";
    ResetColor();
    return 0;
}
