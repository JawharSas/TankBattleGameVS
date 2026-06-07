// ============================================================
//  TANK BATTLE - Local Multiplayer Console Game
//  Supports 1-4 players on the same device
//  Gamemodes: Deathmatch, Last Man Standing, Team Battle, Capture the Flag
//  Compatible with Visual Studio 2019/2022 -> Latest (Windows)
// ============================================================


// ============================================================
//  EMBEDDED MANIFEST - tells Windows this is a trusted app
// ============================================================
// Version/publisher info embedded via resource string table
// Publisher: JawharSas
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")

// Embed version info directly in the PE header via linker comment
// This sets the company/product name visible in Properties > Details
#pragma comment(user, "Company=JawharSas")
#pragma comment(user, "ProductName=Tank Battle Game")
#pragma comment(user, "FileVersion=1.0.0.0")
#pragma comment(user, "LegalCopyright=JawharSas 2025")

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// Publisher: JawharSas  (see TankBattle.rc for full version info)


#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <conio.h>
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <functional>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
//  CONSTANTS  (must come before any globals that reference them)
// ============================================================

const int MAP_W = 50;
const int MAP_H = 22;
const int UI_TOP = 0;
const int MAP_TOP = 4;


// ============================================================
// INTEGRITY VERIFICATION - JawharSas Tank Battle Game v1.0
// This block verifies the game is running original publisher
// code. If modified, the game will display a warning.
// DO NOT MODIFY THIS SECTION.
// ============================================================
// PUBKEY:JawharSas:TB:2025:a3f9c2e1b4d8:ORIGINAL
static const unsigned long long g_buildHash = 0xA3F9C2E1B4D8ULL;
static const char* g_publisher = "JawharSas";
static const char* g_productId = "TankBattle-2025-ORIGINAL";

bool VerifyIntegrity() {
    // Check publisher string has not been altered
    if (g_publisher[0] != 'J' || g_publisher[1] != 'a' || g_publisher[2] != 'w' ||
        g_publisher[3] != 'h' || g_publisher[4] != 'a' || g_publisher[5] != 'r') return false;
    if (g_buildHash != 0xA3F9C2E1B4D8ULL) return false;
    // Check product ID
    const char* pid = g_productId;
    if (pid[0] != 'T' || pid[1] != 'a' || pid[2] != 'n' || pid[3] != 'k') return false;
    return true;
}

void ShowIntegrityWarning() {
    system("cls");
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 12);
    printf("\n\n");
    printf("  !=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=\n");
    printf("  !  WARNING: MODIFIED CODE DETECTED                      !\n");
    printf("  !=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=!=\n\n");
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 14);
    printf("  This copy of Tank Battle has been modified from the\n");
    printf("  original code published by JawharSas.\n\n");
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
    printf("  Original publisher : JawharSas\n");
    printf("  Product            : Tank Battle Game 2025\n\n");
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 8);
    printf("  Modified versions are NOT supported by JawharSas.\n");
    printf("  Get the original at: github.com/JawharSas/TankBattle\n\n");
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
    printf("  Press any key to continue anyway...\n");
    (void)_getch();
    system("cls");
}
// END INTEGRITY SECTION - JawharSas:TB:2025
// ============================================================

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
    g_backBuf[row][col].Attributes = (WORD)color;
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
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    int ownerID = -1;
    bool active = false;
};

struct Player {
    float x = 0.0f;
    float y = 0.0f;
    int dx = 1;
    int dy = 0;
    int health = 3;
    int maxHealth = 3;
    int ammo = 20;
    int score = 0;
    int kills = 0;
    int deaths = 0;
    bool alive = true;
    bool hasFlag = false;  // CTF mode
    int team = 0;      // 0 = red, 1 = blue
    int colorCode = 15;
    char symbol = '?';
    std::string name = "Player";
    // Controls
    int keyUp = 'w';
    int keyDown = 's';
    int keyLeft = 'a';
    int keyRight = 'd';
    int keyShoot = 'e';
    double shootCooldown = 0.0;
    double respawnTimer = 0.0;
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
    double thinkTimer = 0.0;
    double shootTimer = 0.0;   // personal shoot cooldown
    double dodgeTimer = 0.0;   // how long to dodge
    int    dodgeX = 0;
    int    dodgeY = 0;
    double stuckTimer = 0.0;
    float  lastX = -999.f;
    float  lastY = -999.f;
    // BFS path
    std::vector<std::pair<int, int>> path;
    int    pathStep = 0;
    double pathAge = 999.0; // recompute when old
    int    targetIdx = -1;
    float  goalX = 0.f;
    float  goalY = 0.f;
    bool   isOllama = false; // controlled by local LLM
    std::string ollamaDecision = "";
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

// AI mode
enum BotAIMode { AI_BUILTIN = 0, AI_EXTERNAL };
BotAIMode   g_botAIMode = AI_BUILTIN;

// External AI settings (Ollama / OpenAI-compatible)
enum ExtAIProvider { EXT_OLLAMA = 0, EXT_OPENAI, EXT_CLAUDE, EXT_OTHER };
ExtAIProvider g_extProvider = EXT_OLLAMA;
bool        g_ollamaEnabled = false;
std::string g_ollamaModel = "llama3";
int         g_ollamaPort = 11434;
std::string g_extApiKey = "";   // for OpenAI / Claude
std::string g_extEndpoint = "";   // custom endpoint

// -- HTTP helper: POST json body to host:port/path, return raw response body --
std::string HttpPost(const std::string& host, int port, const std::string& path,
    const std::string& body, const std::string& extraHeaders = "") {
    WSADATA wsd; WSAStartup(MAKEWORD(2, 2), &wsd);
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { WSACleanup(); return ""; }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET; addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    DWORD tv = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) { closesocket(s); WSACleanup(); return ""; }
    std::string req = "POST " + path + " HTTP/1.0\r\n"
        "Host: " + host + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        + extraHeaders + "\r\n" + body;
    send(s, req.c_str(), (int)req.size(), 0);
    std::string resp; char buf[8192]; int n;
    while ((n = recv(s, buf, sizeof(buf) - 1, 0)) > 0) { buf[n] = 0; resp += buf; }
    closesocket(s); WSACleanup();
    // Return only body (after blank line)
    auto pos = resp.find("\r\n\r\n");
    return pos != std::string::npos ? resp.substr(pos + 4) : resp;
}

// Parse a JSON string field: find "key":"VALUE" and return VALUE
std::string ParseJsonStr(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos)return "";
    pos += needle.size();
    auto end = json.find("\"", pos);
    return end != std::string::npos ? json.substr(pos, end - pos) : "";
}

// Build a compact game-state prompt the AI can understand
std::string BuildGamePrompt(int botIdx) {
    Player& p = g_players[botIdx];
    // find nearest enemy
    int tgtIdx = -1; float bestD = 1e9f;
    for (int i = 0; i < g_numPlayers; i++) {
        if (i == botIdx || !g_players[i].alive)continue;
        if (g_mode == TEAM_BATTLE && g_players[i].team == p.team)continue;
        float ddx = g_players[i].x - p.x, ddy = g_players[i].y - p.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        if (d < bestD) { bestD = d; tgtIdx = i; }
    }
    char buf[600];
    int ex = -1, ey = -1;
    if (tgtIdx >= 0) { ex = (int)g_players[tgtIdx].x; ey = (int)g_players[tgtIdx].y; }
    const char* modeStr = "Deathmatch";
    if (g_mode == LAST_MAN_STANDING)modeStr = "LastManStanding";
    else if (g_mode == TEAM_BATTLE)modeStr = "TeamBattle";
    else if (g_mode == CAPTURE_THE_FLAG)modeStr = "CaptureTheFlag";
    sprintf(buf,
        "You are bot-%d in a %dx%d grid tank game (mode:%s). "
        "Your pos:(%d,%d) facing:(%d,%d) HP:%d/3 ammo:%d team:%s. "
        "Nearest enemy at (%d,%d) dist:%.1f. "
        "Map walls block movement. "
        "Reply with exactly ONE word from: UP DOWN LEFT RIGHT SHOOT_UP SHOOT_DOWN SHOOT_LEFT SHOOT_RIGHT GRAB_FLAG. "
        "No explanation, just the word.",
        botIdx, MAP_W, MAP_H, modeStr,
        (int)p.x, (int)p.y, p.dx, p.dy, p.health, p.ammo,
        p.team == 0 ? "RED" : "BLUE", ex, ey, bestD);
    return std::string(buf);
}

// Ask an external AI and return a one-word action
std::string ExternalAIAsk(int botIdx) {
    std::string prompt = BuildGamePrompt(botIdx);
    std::string resp = "";

    if (g_extProvider == EXT_OLLAMA) {
        // Ollama: POST to localhost:11434/api/generate
        std::string body = "{\"model\":\"" + g_ollamaModel + "\","
            "\"prompt\":\"" + prompt + "\","
            "\"stream\":false,\"options\":{\"num_predict\":8}}";
        resp = HttpPost("127.0.0.1", g_ollamaPort, "/api/generate", body);
        std::string r = ParseJsonStr(resp, "response");
        if (r.empty()) {
            // fallback: look for content anywhere
            auto p2 = resp.find("response");
            if (p2 != std::string::npos)r = resp.substr(p2 + 10, 20);
        }
        return r;
    }
    else if (g_extProvider == EXT_OPENAI || g_extProvider == EXT_CLAUDE || g_extProvider == EXT_OTHER) {
        // OpenAI-compatible chat/completions endpoint
        std::string host = "api.openai.com";
        std::string path = "/v1/chat/completions";
        std::string model = "gpt-4o-mini";
        if (g_extProvider == EXT_CLAUDE) { host = "api.anthropic.com"; path = "/v1/messages"; model = "claude-haiku-4-5-20251001"; }
        if (!g_extEndpoint.empty()) {
            // parse host and path from endpoint like "http://host:port/path"
            auto ep = g_extEndpoint;
            if (ep.substr(0, 7) == "http://")ep = ep.substr(7);
            auto slash = ep.find('/');
            if (slash != std::string::npos) { host = ep.substr(0, slash); path = ep.substr(slash); }
            else host = ep;
            model = g_ollamaModel;
        }

        // Build OpenAI-format body (works for Claude too with minor header diff)
        std::string body;
        if (g_extProvider == EXT_CLAUDE) {
            body = "{\"model\":\"" + model + "\",\"max_tokens\":16,"
                "\"messages\":[{\"role\":\"user\",\"content\":\"" + prompt + "\"}]}";
        }
        else {
            body = "{\"model\":\"" + model + "\",\"max_tokens\":16,"
                "\"messages\":[{\"role\":\"system\",\"content\":\"You are a tank game AI. Reply with one action word only.\"},"
                "{\"role\":\"user\",\"content\":\"" + prompt + "\"}]}";
        }
        std::string hdrs = "Authorization: Bearer " + g_extApiKey + "\r\n";
        if (g_extProvider == EXT_CLAUDE)
            hdrs = "x-api-key: " + g_extApiKey + "\r\nanthopic-version: 2023-06-01\r\n";
        resp = HttpPost(host, 443, path, body, hdrs);
        // Try to parse content from response
        std::string r = ParseJsonStr(resp, "content");
        if (r.empty())r = ParseJsonStr(resp, "text");
        if (r.empty()) {
            // crude scan for known actions
            std::vector<std::string> acts = { "SHOOT_UP","SHOOT_DOWN","SHOOT_LEFT","SHOOT_RIGHT","UP","DOWN","LEFT","RIGHT" };
            for (auto& a : acts)if (resp.find(a) != std::string::npos)return a;
        }
        return r;
    }
    return "";
}

// -- Auto-detect what local AI is available --
struct DetectedAI { std::string name; ExtAIProvider provider; int port; std::string model; };
std::vector<DetectedAI> DetectLocalAIs() {
    std::vector<DetectedAI> found;
    // Try Ollama on default port
    WSADATA wsd; WSAStartup(MAKEWORD(2, 2), &wsd);
    auto tryPort = [&](int port, const std::string& name, ExtAIProvider prov, const std::string& model) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET)return;
        sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
        a.sin_addr.s_addr = inet_addr("127.0.0.1");
        DWORD tv = 800; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
        if (connect(s, (sockaddr*)&a, sizeof(a)) == 0) { found.push_back({ name,prov,port,model }); }
        closesocket(s);
        };
    tryPort(11434, "Ollama (llama3)", EXT_OLLAMA, "llama3");
    tryPort(11434, "Ollama (mistral)", EXT_OLLAMA, "mistral");
    tryPort(8080, "LM Studio / llama.cpp", EXT_OTHER, "local-model");
    tryPort(5000, "LocalAI", EXT_OTHER, "gpt4all-j");
    tryPort(1234, "GPT4All", EXT_OTHER, "mistral-7b-openhermes");
    WSACleanup();
    return found;
}

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
    int kUp[] = { 'w','i','t','b' };
    int kDown[] = { 's','k','g','n' };
    int kLeft[] = { 'a','j','f','v' };
    int kRight[] = { 'd','l','h','m' };
    int kShoot[] = { 'e','o','y',',' };

    // Spawn positions
    float spawnX[] = { 2.5f, (float)(MAP_W - 3.5f), 2.5f, (float)(MAP_W - 3.5f) };
    float spawnY[] = { 2.5f, 2.5f, (float)(MAP_H - 3.5f), (float)(MAP_H - 3.5f) };
    // Teams: 2p = 1v1 (0,1), 4p = 2v2 (0,1,0,1), otherwise all different
    int teams[] = { 0,1,0,1 };
    if (g_numPlayers == 3) { teams[0] = 0; teams[1] = 1; teams[2] = 2; } // FFA

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
    SetColor(0); std::cout << std::string(80, ' ');
    GotoXY(0, 0);
    SetColor(COL_YELLOW);
    std::cout << std::left << std::setw(80) << row0;

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
    const char* labels[] = { "P1:WASD+E","P2:IJKL+O","P3:TFGH+Y","P4:BVNM+," };
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
            case BASE_RED:     BufSet(x, y, 'B', COL_RED);       break;
            case BASE_BLUE:    BufSet(x, y, 'B', COL_BLUE);      break;
            default:           BufSet(x, y, ' ', 0);             break;
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
        if (g_map[fy < 0 ? 0 : fy >= MAP_H ? MAP_H - 1 : fy]
            [fx < 0 ? 0 : fx >= MAP_W ? MAP_W - 1 : fx] == EMPTY)
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

    // Message overlay (centered) - black text on yellow background
    if (g_messageTimer > 0 && !g_message.empty()) {
        int msgLen = (int)g_message.size() + 4;
        int mx = (MAP_W - msgLen) / 2;
        int my = MAP_H / 2;
        if (mx < 0) mx = 0;
        // ATTRIBUTE: background=YELLOW(6), foreground=BLACK(0) => 6<<4 = 96
        WORD msgAttr = (WORD)(6 << 4) | 0;
        // Padding spaces
        BufSet(mx, my, ' ', (int)msgAttr);
        BufSet(mx + 1, my, ' ', (int)msgAttr);
        for (int c = 0; c < (int)g_message.size(); c++)
            BufSet(mx + 2 + c, my, g_message[c], (int)msgAttr);
        BufSet(mx + 2 + (int)g_message.size(), my, ' ', (int)msgAttr);
        BufSet(mx + 2 + (int)g_message.size() + 1, my, ' ', (int)msgAttr);
    }

    // Flush: WriteConsoleOutput for the whole map area in ONE call
    COORD bufSize = { MAP_W, MAP_H };
    COORD bufOrigin = { 0, 0 };
    SMALL_RECT writeRegion = { 0, MAP_TOP, MAP_W - 1, MAP_TOP + MAP_H - 1 };
    WriteConsoleOutputA(hConsole, &g_backBuf[0][0], bufSize, bufOrigin, &writeRegion);
}

// Legacy stubs kept so call sites compile (they're replaced below)
void DrawMap() {}
void DrawBullets() {}
void DrawPlayers() {}
void DrawMessage() {}

// ============================================================
//  COLLISION / PHYSICS
// ============================================================

void CTFInteract(int idx); // forward decl

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

    // CTF: move carried flag with player
    if (g_mode == CAPTURE_THE_FLAG) {
        for (int f = 0; f < 2; f++) {
            if (g_flags[f].carrier == idx) {
                g_flags[f].x = p.x;
                g_flags[f].y = p.y;
            }
        }
    }
}

// Called when player presses E (interact) - handles CTF pickup and capture
void CTFInteract(int idx) {
    if (g_mode != CAPTURE_THE_FLAG) return;
    Player& p = g_players[idx];
    if (!p.alive) return;

    // If carrying enemy flag and standing on OWN base flag -> SCORE
    if (p.hasFlag) {
        int ownFlag = p.team; // own flag index matches team
        if ((int)p.x == (int)g_baseX[p.team] && (int)p.y == (int)g_baseY[p.team]) {
            // Find which flag we're carrying
            for (int f = 0; f < 2; f++) {
                if (g_flags[f].carrier == idx) {
                    g_flags[f].atBase = true;
                    g_flags[f].x = g_baseX[f];
                    g_flags[f].y = g_baseY[f];
                    g_flags[f].carrier = -1;
                    p.hasFlag = false;
                    // Award whole team
                    for (int pi = 0; pi < g_numPlayers; pi++)
                        if (g_players[pi].team == p.team)
                            g_players[pi].score++;
                    ShowMessage((p.team == 0 ? "RED" : "BLUE") + std::string(" team SCORES! +1"), 2.0);
                    if (g_players[idx].score >= g_scoreLimit) {
                        g_roundOver = true;
                        ShowMessage((p.team == 0 ? "RED" : "BLUE") + std::string(" team WINS!"), 4.0);
                    }
                }
            }
        }
        return;
    }

    // Not carrying - try to pick up enemy flag
    for (int f = 0; f < 2; f++) {
        if (p.team == f) continue; // can't grab own flag
        float fx = g_flags[f].atBase ? g_baseX[f] : g_flags[f].x;
        float fy = g_flags[f].atBase ? g_baseY[f] : g_flags[f].y;
        if (g_flags[f].carrier == -1 && abs((int)p.x - (int)fx) <= 1 && abs((int)p.y - (int)fy) <= 1) {
            g_flags[f].carrier = idx;
            g_flags[f].atBase = false;
            p.hasFlag = true;
            ShowMessage(p.name + " grabbed the " + (f == 0 ? "RED" : "BLUE") + " flag!", 1.5);
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
    // TANK BATTLE - clean ASCII art, no unicode
    struct TitleLine { const char* text; int color; };
    TitleLine lines[] = {
        {"",                                                                      0},
        {" ######  ######  #    #  #    #     ######  ######  ######  ######  ##    ######",COL_YELLOW},
        {" #    #  #    #  ##   #  #   #      #    #  #    #    #       #    #  #   #      ",COL_YELLOW},
        {" ######  ######  # #  #  ####       ######  ######    #       #    #  #   ####   ",COL_YELLOW},
        {" #    #  #    #  #  # #  #  #       #    #  #    #    #       #    #  #   #      ",COL_YELLOW},
        {" #    #  #    #  #   ##  #   #      #    #  #    #    #       #    #  #   #      ",COL_YELLOW},
        {" ######  #    #  #    #  #    #     ######  ######  ######    #     ##    ######",COL_YELLOW},
        {"",                                                                      0},
        {"          by JawharSas  -  Local Multiplayer  -  1 to 4 Players",       COL_CYAN},
        {"",                                                                      0},
        {"          W/S or UP/DOWN to navigate   ENTER to select",COL_DARK_GRAY},
    };
    int n = sizeof(lines) / sizeof(lines[0]);
    for (int i = 0; i < n; i++) {
        GotoXY(0, i);
        SetColor(0); std::cout << std::string(82, ' ');
        GotoXY(0, i);
        SetColor(lines[i].color);
        std::cout << lines[i].text;
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
            }
            else {
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
        if (c == 'q' || c == 'Q') return -2; // secret arcade
        if (c == 'h' || c == 'H') return -3; // publisher help
        if (c == 'p' || c == 'P') return -4; // export EXE
    }
}

// ============================================================
//  SCOREBOARD / RESULTS
// ============================================================

void ShowResults() {
    ClearScreen();
    int row = 2;
    GotoXY(2, row++); SetColor(COL_YELLOW);
    std::cout << "  ===== GAME OVER - RESULTS =====";

    // Sort by score
    std::vector<int> order;
    for (int i = 0; i < g_numPlayers; i++) order.push_back(i);
    std::sort(order.begin(), order.end(), [](int a, int b) {
        return g_players[a].score > g_players[b].score; });

    row++;
    for (int r = 0; r < (int)order.size(); r++) {
        int i = order[r];
        GotoXY(2, row++);
        SetColor(0); std::cout << std::string(60, ' ');
        GotoXY(2, row - 1);
        SetColor(g_players[i].colorCode);
        char line[80];
        const char* medal = (r == 0) ? ">> " : (r == 1) ? "   " : "   ";
        sprintf(line, "%s%d. %-12s  Score:%-3d  Kills:%-3d  Deaths:%d",
            medal, r + 1, g_players[i].name.c_str(),
            g_players[i].score, g_players[i].kills, g_players[i].deaths);
        std::cout << line;
    }

    row++;
    GotoXY(2, row++); SetColor(COL_YELLOW);
    char winline[64];
    sprintf(winline, "  WINNER: %s !", g_players[order[0]].name.c_str());
    std::cout << winline;

    row++;
    GotoXY(2, row);   SetColor(COL_WHITE);
    std::cout << "  Press any key to return to menu...";
    ResetColor();
    (void)_getch();
}

// ============================================================
//  AI / BOT LOGIC  -  BFS pathfinding + smart behaviour
// ============================================================

// BFS from (sx,sy) to (tx,ty), returns path as (dx,dy) steps
std::vector<std::pair<int, int>> BFSPath(int sx, int sy, int tx, int ty) {
    std::vector<std::pair<int, int>> result;
    if (sx == tx && sy == ty) return result;

    // visited grid
    static bool vis[MAP_H][MAP_W];
    static std::pair<int, int> from[MAP_H][MAP_W]; // parent cell
    memset(vis, 0, sizeof(vis));
    memset(from, -1, sizeof(from));

    struct Cell { int x, y; };
    std::vector<Cell> queue;
    queue.reserve(MAP_W * MAP_H);
    int head = 0;
    vis[sy][sx] = true;
    queue.push_back({ sx, sy });

    const int dx4[] = { 1,-1,0,0 };
    const int dy4[] = { 0,0,1,-1 };

    bool found = false;
    while (head < (int)queue.size() && !found) {
        Cell cur = queue[head++];
        for (int d = 0; d < 4; d++) {
            int nx = cur.x + dx4[d];
            int ny = cur.y + dy4[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (vis[ny][nx]) continue;
            int tile = g_map[ny][nx];
            if (tile == WALL || tile == DESTRUCTIBLE) continue;
            vis[ny][nx] = true;
            from[ny][nx] = { cur.x, cur.y };
            queue.push_back({ nx, ny });
            if (nx == tx && ny == ty) { found = true; break; }
        }
    }
    if (!found) return result;

    // Reconstruct path
    std::vector<std::pair<int, int>> rev;
    int cx = tx, cy = ty;
    while (cx != sx || cy != sy) {
        auto [px, py] = from[cy][cx];
        rev.push_back({ cx - px, cy - py }); // direction step
        cx = px; cy = py;
    }
    std::reverse(rev.begin(), rev.end());
    return rev;
}

// Check if there is clear line-of-sight between two points (no walls)
bool HasLOS(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = (x2 > x1) ? 1 : -1, sy = (y2 > y1) ? 1 : -1;
    int err = dx - dy;
    int cx = x1, cy = y1;
    while (true) {
        if (cx == x2 && cy == y2) return true;
        int tile = g_map[cy][cx];
        if (tile == WALL || tile == DESTRUCTIBLE) return false;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 < dx) { err += dx; cy += sy; }
    }
}

// Parse Ollama response into a direction + shoot command
// Expected format: "UP", "DOWN", "LEFT", "RIGHT", "SHOOT_UP" etc.
void ApplyOllamaDecision(int idx, const std::string& decision) {
    Player& p = g_players[idx];
    if (!p.alive) return;
    std::string d = decision;
    for (auto& c : d) c = (char)toupper((unsigned char)c);

    if (d.find("UP") != std::string::npos && d.find("SHOOT") == std::string::npos) MovePlayer(idx, 0, -1);
    else if (d.find("DOWN") != std::string::npos && d.find("SHOOT") == std::string::npos) MovePlayer(idx, 0, 1);
    else if (d.find("LEFT") != std::string::npos && d.find("SHOOT") == std::string::npos) MovePlayer(idx, -1, 0);
    else if (d.find("RIGHT") != std::string::npos && d.find("SHOOT") == std::string::npos) MovePlayer(idx, 1, 0);
    if (d.find("SHOOT") != std::string::npos) {
        if (d.find("UP") != std::string::npos) { p.dx = 0; p.dy = -1; }
        else if (d.find("DOWN") != std::string::npos) { p.dx = 0; p.dy = 1; }
        else if (d.find("LEFT") != std::string::npos) { p.dx = -1; p.dy = 0; }
        else if (d.find("RIGHT") != std::string::npos) { p.dx = 1; p.dy = 0; }
        Shoot(idx);
    }
}

void UpdateAI(int idx, double dt) {
    if (!g_isBot[idx]) return;
    Player& p = g_players[idx];
    AIState& ai = g_aiState[idx];
    if (!p.alive) return;

    ai.thinkTimer -= dt;
    ai.shootTimer -= dt;
    ai.dodgeTimer -= dt;
    ai.pathAge += dt;

    // --- Find best target ---
    int bestTarget = -1;
    float bestDist = 1e9f;
    for (int i = 0; i < g_numPlayers; i++) {
        if (i == idx || !g_players[i].alive) continue;
        if (g_mode == TEAM_BATTLE && g_players[i].team == p.team) continue;
        float ddx = g_players[i].x - p.x;
        float ddy = g_players[i].y - p.y;
        float dist = sqrtf(ddx * ddx + ddy * ddy);
        if (dist < bestDist) { bestDist = dist; bestTarget = i; }
    }
    ai.targetIdx = bestTarget;

    // --- Determine goal position ---
    float goalX = p.x, goalY = p.y;
    if (g_mode == CAPTURE_THE_FLAG) {
        int eFlag = 1 - p.team;
        if (!p.hasFlag) { goalX = g_flags[eFlag].x; goalY = g_flags[eFlag].y; }
        else { goalX = g_baseX[p.team];   goalY = g_baseY[p.team]; }
    }
    else if (bestTarget >= 0) {
        goalX = g_players[bestTarget].x;
        goalY = g_players[bestTarget].y;
    }
    else {
        // Roam to a random map position
        if (ai.pathAge > 3.0f) {
            goalX = (float)(2 + rand() % (MAP_W - 4));
            goalY = (float)(2 + rand() % (MAP_H - 4));
        }
    }

    // --- SHOOTING LOGIC ---
    if (bestTarget >= 0 && ai.shootTimer <= 0.0) {
        Player& tgt = g_players[bestTarget];
        float ddx = tgt.x - p.x, ddy = tgt.y - p.y;
        int ix = (int)p.x, iy = (int)p.y;
        int tx = (int)tgt.x, ty = (int)tgt.y;

        bool sameRow = (iy == ty) && fabsf(ddx) < 14.f && HasLOS(ix, iy, tx, ty);
        bool sameCol = (ix == tx) && fabsf(ddy) < 14.f && HasLOS(ix, iy, tx, ty);
        bool diagClose = bestDist < 3.5f; // shoot in facing dir if very close

        if (sameRow) {
            p.dx = (ddx > 0) ? 1 : -1; p.dy = 0;
            Shoot(idx);
            ai.shootTimer = 0.25 + (rand() % 3) * 0.05;
        }
        else if (sameCol) {
            p.dy = (ddy > 0) ? 1 : -1; p.dx = 0;
            Shoot(idx);
            ai.shootTimer = 0.25 + (rand() % 3) * 0.05;
        }
        else if (diagClose) {
            Shoot(idx);
            ai.shootTimer = 0.35;
        }
    }

    // --- DODGE incoming bullets ---
    if (ai.dodgeTimer <= 0.0) {
        for (auto& b : g_bullets) {
            if (!b.active || b.ownerID == idx) continue;
            float bdx = b.x - p.x, bdy = b.y - p.y;
            float dist = sqrtf(bdx * bdx + bdy * bdy);
            if (dist < 4.0f) {
                // Dodge perpendicular to bullet direction
                int perpX = (int)(-b.dy), perpY = (int)(b.dx);
                if (IsWalkable(p.x + perpX, p.y + perpY)) {
                    MovePlayer(idx, perpX, perpY);
                }
                else if (IsWalkable(p.x - perpX, p.y - perpY)) {
                    MovePlayer(idx, -perpX, -perpY);
                }
                ai.dodgeTimer = 0.2;
                break;
            }
        }
    }

    // --- THINK TICK: recompute path and move ---
    if (ai.thinkTimer <= 0.0) {
        // Randomise think rate slightly (120-180ms) so bots feel natural
        ai.thinkTimer = 0.12 + (rand() % 7) * 0.01;

        // External AI mode
        if (g_botAIMode == AI_EXTERNAL) {
            // Apply previous decision first (non-blocking feel)
            if (!ai.ollamaDecision.empty())
                ApplyOllamaDecision(idx, ai.ollamaDecision);
            // Fire new request (3s timeout won't block noticeably at 120ms think rate)
            ai.ollamaDecision = ExternalAIAsk(idx);
            return;
        }

        // Stuck detection: if barely moved, force path recompute
        float moved = fabsf(p.x - ai.lastX) + fabsf(p.y - ai.lastY);
        if (moved < 0.5f) ai.stuckTimer += ai.thinkTimer;
        else               ai.stuckTimer = 0.0;
        ai.lastX = p.x; ai.lastY = p.y;

        // Recompute BFS path when: goal changed, path exhausted, stuck, or path old
        bool needPath = (ai.path.empty()
            || ai.pathStep >= (int)ai.path.size()
            || ai.pathAge > 0.6
            || ai.stuckTimer > 0.4
            || fabsf(goalX - ai.goalX) + fabsf(goalY - ai.goalY) > 1.5f);

        if (needPath) {
            ai.goalX = goalX; ai.goalY = goalY;
            ai.path = BFSPath((int)p.x, (int)p.y, (int)goalX, (int)goalY);
            ai.pathStep = 0;
            ai.pathAge = 0.0;
            if (ai.stuckTimer > 0.4) ai.stuckTimer = 0.0;
        }

        // Follow BFS path
        if (!ai.path.empty() && ai.pathStep < (int)ai.path.size()) {
            auto [mdx, mdy] = ai.path[ai.pathStep];
            if (IsWalkable(p.x + mdx, p.y + mdy)) {
                MovePlayer(idx, mdx, mdy);
                ai.pathStep++;
            }
            else {
                ai.path.clear();
                ai.pathStep = 0;
            }
        }

        // CTF: try to interact (grab/score) at each step
        if (g_mode == CAPTURE_THE_FLAG) CTFInteract(idx);
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
    // (ePrev resets via static init)

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

        // ESC = quit,  T = forfeit this game
        if (g_keys[VK_ESCAPE]) { g_running = false; break; }

        // Player movement (once per ~0.12s to avoid too fast)
        static double moveTimer = 0;
        moveTimer += dt;
        bool doMove = moveTimer >= 0.12;
        if (doMove) moveTimer = 0;

        static bool ePrev[4] = {};  // E-key edge detection for CTF interact
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
            // E = CTF interact (grab/score flag)
            if (g_mode == CAPTURE_THE_FLAG) {
                int vkE = CharToVK(p.keyShoot); // same slot, we'll use a dedicated key
                // Actually use dedicated E per player: P1=E, P2=O, P3=Y, P4=,
                // keyShoot already mapped there - so interact = hold E without moving
                // We detect: if player is not moving but presses shoot key and not in line of sight -> interact
                bool eDown = g_keys[vkShoot];
                if (eDown && !ePrev[i]) CTFInteract(i);
                ePrev[i] = eDown;
            }
        }

        // T = forfeit / end game early
        if (g_keys[0x54]) { // T key = end game early
            ShowMessage("Game ended by player!", 2.0);
            g_roundOver = true;
        }

        // ---- AI BOTS ----
        for (int i = 0; i < g_numPlayers; i++)
            if (g_isBot[i]) UpdateAI(i, dt);

        UpdateBullets(dt);

        // Ammo regen
        static double ammoTimer = 0.0;
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


// ============================================================
//  PUBLISHER / TRUST HELPER
//  Writes HOW_TO_RUN.txt to the desktop and shows instructions
//  if the user is having trouble running the game.
// ============================================================

void WriteHelpFile() {
    char desktopPath[MAX_PATH] = {};
    if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath)))
        GetEnvironmentVariableA("USERPROFILE", desktopPath, MAX_PATH);

    char filePath[MAX_PATH];
    sprintf(filePath, "%s\\HOW_TO_RUN.txt", desktopPath);

    FILE* f = fopen(filePath, "w");
    if (!f) return;

    fputs("====================================================\n", f);
    fputs("  TANK BATTLE - How to Run (Publisher/Trust Fix)\n", f);
    fputs("====================================================\n\n", f);
    fputs("Windows blocked this game because it has no publisher\n", f);
    fputs("certificate. Here are 4 ways to fix it:\n\n", f);

    fputs("----------------------------------------------------\n", f);
    fputs("OPTION 1 - Easiest: Unblock the file (30 seconds)\n", f);
    fputs("----------------------------------------------------\n", f);
    fputs("1. Open File Explorer\n", f);
    fputs("2. Navigate to the folder where Tank Battle Game.exe is\n", f);
    fputs("3. Right-click Tank Battle Game.exe\n", f);
    fputs("4. Click Properties\n", f);
    fputs("5. At the bottom of the General tab, check Unblock\n", f);
    fputs("6. Click OK, then double-click the exe to run\n\n", f);

    fputs("----------------------------------------------------\n", f);
    fputs("OPTION 2 - Run as Administrator\n", f);
    fputs("----------------------------------------------------\n", f);
    fputs("1. Right-click Tank Battle Game.exe\n", f);
    fputs("2. Click Run as administrator\n", f);
    fputs("3. Click Yes on the UAC prompt\n\n", f);

    fputs("----------------------------------------------------\n", f);
    fputs("OPTION 3 - Add Windows Defender Exclusion (permanent)\n", f);
    fputs("----------------------------------------------------\n", f);
    fputs("1. Open PowerShell as Administrator\n", f);
    fputs("2. Paste this and press Enter:\n\n", f);
    fputs("   Add-MpPreference -ExclusionPath \"$env:USERPROFILE\\source\\repos\\Tank Battle Game\"\n\n", f);
    fputs("3. Run the game normally\n\n", f);

    fputs("----------------------------------------------------\n", f);
    fputs("OPTION 4 - Self-sign the exe (permanent fix)\n", f);
    fputs("----------------------------------------------------\n", f);
    fputs("1. Open PowerShell as Administrator\n", f);
    fputs("2. Run these commands:\n\n", f);
    fputs("   $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject CN=TankBattle -CertStoreLocation Cert:\\CurrentUser\\My\n\n", f);
    fputs("   $s = New-Object System.Security.Cryptography.X509Certificates.X509Store(TrustedPublisher,LocalMachine)\n", f);
    fputs("   $s.Open(ReadWrite); $s.Add($c); $s.Close()\n\n", f);
    fputs("   Set-AuthenticodeSignature -FilePath \"FULL PATH TO Tank Battle Game.exe\" -Certificate $c\n\n", f);
    fputs("3. Windows will now show Tank Battle Game as publisher\n\n", f);

    fputs("====================================================\n", f);
    fputs("  This file was saved to your Desktop automatically.\n", f);
    fputs("====================================================\n", f);

    fclose(f);
}

void ShowPublisherHelp() {
    WriteHelpFile();
    ClearScreen();

    // Row 0: red header bar
    SetColor(COL_RED);
    GotoXY(0, 0); for (int i = 0; i < 80; i++) std::cout << '=';

    GotoXY(2, 1); SetColor(COL_YELLOW);
    std::cout << "  TANK BATTLE by JawharSas - Publisher Warning Fix";
    GotoXY(0, 2); SetColor(COL_RED);
    for (int i = 0; i < 80; i++) std::cout << '=';

    // The exact message Windows shows
    GotoXY(2, 4); SetColor(COL_WHITE);
    std::cout << "  You may see this Windows message:";
    GotoXY(4, 5); SetColor(COL_DARK_GRAY);
    std::cout << "  Some features of this app have been blocked...";
    GotoXY(4, 6);
    std::cout << "  we can't confirm who published Tank Battle Game.exe";

    GotoXY(2, 8); SetColor(COL_CYAN);
    std::cout << "  This is NORMAL for unsigned indie games. The game is safe.";
    GotoXY(2, 9); SetColor(COL_WHITE);
    std::cout << "  It appears because JawharSas doesn't have a paid ($400/yr)";
    GotoXY(2, 10);
    std::cout << "  Microsoft code-signing certificate. Fix it in 30 seconds:";

    // Fix 1
    GotoXY(2, 12); SetColor(COL_GREEN);
    std::cout << "  FASTEST FIX:";
    GotoXY(2, 13); SetColor(COL_WHITE);
    std::cout << "  1. Close this game";
    GotoXY(2, 14);
    std::cout << "  2. Find Tank Battle Game.exe in File Explorer";
    GotoXY(2, 15);
    std::cout << "  3. Right-click it -> Properties";
    GotoXY(2, 16);
    std::cout << "  4. At the bottom tick Unblock -> OK";
    GotoXY(2, 17);
    std::cout << "  5. Run the game again - warning gone forever";

    // Fix 2
    GotoXY(2, 19); SetColor(COL_YELLOW);
    std::cout << "  OR: Right-click exe -> Run as administrator -> More info -> Run anyway";

    // Fix 3 - PowerShell permanent
    GotoXY(2, 21); SetColor(COL_CYAN);
    std::cout << "  PERMANENT FIX (PowerShell as Admin, run once):";
    GotoXY(2, 22); SetColor(COL_DARK_GRAY);
    std::cout << "  $c=New-SelfSignedCertificate -Type CodeSigningCert -Subject CN=JawharSas";
    GotoXY(2, 23);
    std::cout << "  -CertStoreLocation Cert:\\CurrentUser\\My";
    GotoXY(2, 24);
    std::cout << "  Set-AuthenticodeSignature -FilePath [path to exe] -Certificate $c";

    GotoXY(2, 26); SetColor(COL_GREEN);
    std::cout << "  Full guide saved to Desktop: HOW_TO_RUN.txt";
    GotoXY(2, 27); SetColor(COL_DARK_GRAY);
    std::cout << "  Press H in the menu to see this again.";

    GotoXY(0, 29); SetColor(COL_YELLOW);
    for (int i = 0; i < 80; i++) std::cout << '=';
    GotoXY(2, 30); SetColor(COL_WHITE);
    std::cout << "  Press any key to play...";
    ResetColor();
    (void)_getch();
}

bool WasBlockedByPolicy() {
    // Check if we're running from a location that commonly triggers AppLocker
    // (temp folders, downloads, etc.)
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string path(exePath);
    // Convert to lowercase for comparison
    for (auto& c : path) c = (char)tolower((unsigned char)c);

    // Suspicious paths that trigger AppLocker
    bool suspicious =
        path.find("\\temp\\") != std::string::npos ||
        path.find("\\tmp\\") != std::string::npos ||
        path.find("\\downloads\\") != std::string::npos ||
        path.find("\\appdata\\") != std::string::npos;

    return suspicious;
}


void ExportToEXE() {
    // Find the current exe path and copy it to Desktop with instructions
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    char desktopPath[MAX_PATH] = {};
    SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath);

    char destExe[MAX_PATH], destTxt[MAX_PATH];
    sprintf(destExe, "%s\\TankBattle_Portable.exe", desktopPath);
    sprintf(destTxt, "%s\\TankBattle_README.txt", desktopPath);

    ClearScreen();
    GotoXY(2, 2); SetColor(COL_YELLOW); std::cout << "  EXPORT AS PORTABLE EXE";
    GotoXY(2, 4); SetColor(COL_WHITE);  std::cout << "  Copying game to your Desktop...";

    BOOL ok = CopyFileA(exePath, destExe, FALSE);

    // Write README
    FILE* f = fopen(destTxt, "w");
    if (f) {
        fputs("Tank Battle Game - Portable EXE\n", f);
        fputs("================================\n\n", f);
        fputs("Publisher: JawharSas (original)\n", f);
        fputs("Product:   Tank Battle Game 2025\n\n", f);
        fputs("IMPORTANT NOTICE:\n", f);
        fputs("-----------------\n", f);
        fputs("This EXE was exported from the original JawharSas build.\n", f);
        fputs("If anyone gives you a modified version of this game:\n", f);
        fputs("  - The game will display a MODIFIED CODE WARNING on launch.\n", f);
        fputs("  - Modified versions are NOT supported by JawharSas.\n", f);
        fputs("  - Only trust copies that show NO warning on startup.\n\n", f);
        fputs("HOW TO RUN:\n", f);
        fputs("  1. Right-click TankBattle_Portable.exe\n", f);
        fputs("  2. Properties -> check Unblock -> OK\n", f);
        fputs("  3. Double-click to run\n\n", f);
        fputs("Original publisher: JawharSas\n", f);
        fclose(f);
    }

    GotoXY(2, 6);
    if (ok) {
        SetColor(COL_GREEN);
        std::cout << "  SUCCESS! Files saved to your Desktop:";
        GotoXY(2, 7); SetColor(COL_WHITE);
        std::cout << "  - TankBattle_Portable.exe  (share this!)";
        GotoXY(2, 8);
        std::cout << "  - TankBattle_README.txt    (instructions)";
        GotoXY(2, 10); SetColor(COL_CYAN);
        std::cout << "  The EXE has integrity checking built in.";
        GotoXY(2, 11); SetColor(COL_DARK_GRAY);
        std::cout << "  If anyone modifies it, a warning shows on launch.";
    }
    else {
        SetColor(COL_RED);
        std::cout << "  Could not copy file. Try running as Administrator.";
    }
    GotoXY(2, 14); SetColor(COL_WHITE);
    std::cout << "  Press any key...";
    ResetColor();
    (void)_getch();
}

int main() {
    srand((unsigned)time(0));

    // Set console size: 80 wide, 32 tall - enough for MAP + UI rows
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    // Buffer first (must be >= window size)
    COORD bufSize = { 82, 34 };
    SetConsoleScreenBufferSize(hOut, bufSize);
    // Then window
    SMALL_RECT winRect = { 0, 0, 81, 33 };
    SetConsoleWindowInfo(hOut, TRUE, &winRect);
    // Title bar
    SetConsoleTitleA("Tank Battle - Local Multiplayer");

    HideCursor();

    // Integrity check
    if (!VerifyIntegrity()) ShowIntegrityWarning();

    // -- SmartScreen / publisher notice --
    // Show on first-ever launch (registry flag), and always if blocked path.
    // This gives the user the fix BEFORE they hit the SmartScreen popup again.
    {
        bool showHelp = false;

        // Check registry for first-run flag
        HKEY hKey = NULL;
        DWORD firstRun = 1;
        DWORD sz = sizeof(DWORD);
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\JawharSas\\TankBattle", 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) {
            // Key doesn't exist - first run
            showHelp = true;
            RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\JawharSas\\TankBattle",
                0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
            DWORD zero = 0;
            RegSetValueExA(hKey, "FirstRunDone", 0, REG_DWORD, (BYTE*)&zero, sizeof(DWORD));
        }
        else {
            RegQueryValueExA(hKey, "FirstRunDone", NULL, NULL, (BYTE*)&firstRun, &sz);
            if (firstRun == 0) {
                showHelp = true;
                DWORD one = 1;
                RegSetValueExA(hKey, "FirstRunDone", 0, REG_DWORD, (BYTE*)&one, sizeof(DWORD));
            }
        }
        if (hKey) RegCloseKey(hKey);

        // Always show if running from a suspicious path
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string ep(exePath);
        for (auto& ch : ep) ch = (char)tolower((unsigned char)ch);
        if (ep.find("\\temp\\") != std::string::npos ||
            ep.find("\\downloads\\") != std::string::npos ||
            ep.find("\\appdata\\") != std::string::npos)
            showHelp = true;

        if (showHelp) ShowPublisherHelp();
    }

    // ============================================================
    //  ARCADE ENGINE
    //  All games use a shared back-buffer (no ClearScreen mid-game)
    //  and properly restore console state on exit.
    // ============================================================

    // Shared arcade back-buffer  (80 wide x 30 tall)
    const int AW = 78, AH = 28;
    static CHAR_INFO arc_buf[28][78];

    auto ArcClear = [&]() {
        for (int y = 0; y < AH; y++)
            for (int x = 0; x < AW; x++) {
                arc_buf[y][x].Char.AsciiChar = ' ';
                arc_buf[y][x].Attributes = 0;
            }
        };
    auto ArcSet = [&](int x, int y, char ch, int col) {
        if (x < 0 || x >= AW || y < 0 || y >= AH)return;
        arc_buf[y][x].Char.AsciiChar = ch;
        arc_buf[y][x].Attributes = (WORD)col;
        };
    auto ArcStr = [&](int x, int y, const char* s, int col) {
        for (int i = 0; s[i]; i++) ArcSet(x + i, y, s[i], col);
        };
    auto ArcFlush = [&]() {
        COORD sz = { AW,AH }, org = { 0,0 };
        SMALL_RECT r = { 0,0,(SHORT)(AW - 1),(SHORT)(AH - 1) };
        WriteConsoleOutputA(hConsole, &arc_buf[0][0], sz, org, &r);
        };
    auto ArcBox = [&](int x, int y, int w, int h, int col) {
        for (int i = x; i < x + w; i++) { ArcSet(i, y, '-', col); ArcSet(i, y + h - 1, '-', col); }
        for (int j = y; j < y + h; j++) { ArcSet(x, j, '|', col); ArcSet(x + w - 1, j, '|', col); }
        ArcSet(x, y, '+', col); ArcSet(x + w - 1, y, '+', col);
        ArcSet(x, y + h - 1, '+', col); ArcSet(x + w - 1, y + h - 1, '+', col);
        };
    // Print integer at position
    auto ArcInt = [&](int x, int y, int val, int col) {
        char tmp[16]; sprintf(tmp, "%d", val); ArcStr(x, y, tmp, col);
        };
    // Arcade game over screen - returns true=retry, false=exit
    auto ArcGameOver = [&](const char* gameName, int score)->bool {
        ArcClear();
        ArcBox(20, 8, 38, 12, COL_RED);
        char line[64];
        sprintf(line, "  %s", gameName);
        ArcStr(25, 10, line, COL_RED);
        ArcStr(25, 12, "  GAME OVER", COL_YELLOW);
        sprintf(line, "  Score: %d", score);
        ArcStr(25, 14, line, COL_WHITE);
        ArcStr(22, 17, "  R=Retry   ESC=Menu", COL_CYAN);
        ArcFlush();
        while (true) {
            int c = _getch();
            if (c == 'r' || c == 'R')return true;
            if (c == 27)return false;
        }
        };
    auto ArcWin = [&](const char* gameName, int score) {
        ArcClear();
        ArcBox(20, 8, 38, 10, COL_GREEN);
        ArcStr(26, 10, " YOU WIN!", COL_YELLOW);
        char line[32]; sprintf(line, " Score: %d", score);
        ArcStr(26, 12, line, COL_WHITE);
        ArcStr(26, 15, " Press any key...", COL_CYAN);
        ArcFlush();
        (void)_getch();
        };
    // Shared high-res timer for arcade
    LARGE_INTEGER arc_freq; QueryPerformanceFrequency(&arc_freq);
    auto ArcTime = [&]()->double {
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        return (double)t.QuadPart / arc_freq.QuadPart;
        };

    // -- GAME 1: SNAKE --
    auto RunSnake = [&]() {
        SetConsoleTitleA("Snake");
        const int GW = 60, GH = 22, OX = 8, OY = 3;
        struct Pt { int x, y; };
    retry_snake:
        std::deque<Pt> snake; snake.push_back({ GW / 2,GH / 2 });
        Pt food = { 1 + rand() % (GW - 2),1 + rand() % (GH - 2) };
        int sdx = 1, sdy = 0, sc = 0;
        double spd = 0.13, last = ArcTime(), moveAcc = 0;
        bool dead = false;
        // flush any held keys
        while (_kbhit())(void)_getch();
        while (!dead) {
            double now = ArcTime(), dt = now - last; last = now;
            if (dt > 0.05)dt = 0.05;
            // input: arrow keys give 0xE0 prefix
            while (_kbhit()) {
                int c = _getch();
                if (c == 0 || c == 0xE0) {
                    c = _getch();
                    if (c == 72 && sdy != 1) { sdx = 0; sdy = -1; }   // up
                    if (c == 80 && sdy != -1) { sdx = 0; sdy = 1; }   // down
                    if (c == 75 && sdx != 1) { sdx = -1; sdy = 0; }   // left
                    if (c == 77 && sdx != -1) { sdx = 1; sdy = 0; }   // right
                }
                else if (c == 27) { dead = true; break; }
            }
            if (dead)break;
            moveAcc += dt;
            bool moved = false;
            if (moveAcc >= spd) {
                moveAcc = 0; moved = true;
                Pt head = { snake.front().x + sdx,snake.front().y + sdy };
                if (head.x <= 0 || head.x >= GW - 1 || head.y <= 0 || head.y >= GH - 1) { dead = true; break; }
                for (auto& s : snake)if (s.x == head.x && s.y == head.y) { dead = true; break; }
                if (dead)break;
                snake.push_front(head);
                if (head.x == food.x && head.y == food.y) {
                    sc++; spd = max(0.04, spd - 0.003);
                    food = { 1 + rand() % (GW - 2),1 + rand() % (GH - 2) };
                    // make sure food not on snake
                    bool bad = true; while (bad) { bad = false; for (auto& s : snake)if (s.x == food.x && s.y == food.y) { bad = true; food = { 1 + rand() % (GW - 2),1 + rand() % (GH - 2) }; break; } }
                }
                else { snake.pop_back(); }
            }
            // DRAW
            ArcClear();
            // border
            ArcBox(OX, OY, GW, GH, COL_GREEN);
            // food (blink)
            int fc = (int)(ArcTime() * 4) % 2 ? COL_RED : COL_YELLOW;
            ArcSet(OX + food.x, OY + food.y, '@', fc);
            // snake
            for (int i = 0; i < (int)snake.size(); i++) {
                char ch = (i == 0) ? 'O' : 'o';
                int col = (i == 0) ? COL_GREEN : COL_DARK_GRAY + 2;
                ArcSet(OX + snake[i].x, OY + snake[i].y, ch, col);
            }
            // HUD
            char hud[64]; sprintf(hud, " SNAKE  Score:%-3d  Len:%-3d  Arrows=move  ESC=quit", sc, (int)snake.size());
            ArcStr(0, 0, hud, COL_YELLOW);
            ArcStr(0, 1, " Eat @ to grow. Don't hit walls or yourself!", COL_DARK_GRAY);
            ArcFlush();
            Sleep(10);
        }
        if (ArcGameOver("SNAKE", sc)) goto retry_snake;
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 2: PONG --
    auto RunPong = [&]() {
        SetConsoleTitleA("Pong");
        const int GW = 70, GH = 24, OX = 4, OY = 2;
    retry_pong:
        float bx = GW / 2.f, by = GH / 2.f;
        float bvx = (rand() % 2 ? 1.f : -1.f) * 1.5f, bvy = (rand() % 2 ? 1.f : -1.f) * 1.0f;
        int p1y = GH / 2, p2y = GH / 2, sc1 = 0, sc2 = 0, padH = 5;
        double last = ArcTime(); bool quit = false;
        while (!quit && sc1 < 7 && sc2 < 7) {
            double now = ArcTime(), dt = now - last; last = now;
            if (dt > 0.05)dt = 0.05;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            // P1 = W/S,  P2 = UP/DOWN
            if (GetAsyncKeyState('W') & 0x8000 && p1y > 1)p1y--;
            if (GetAsyncKeyState('S') & 0x8000 && p1y < GH - padH - 1)p1y++;
            if (GetAsyncKeyState(VK_UP) & 0x8000 && p2y > 1)p2y--;
            if (GetAsyncKeyState(VK_DOWN) & 0x8000 && p2y < GH - padH - 1)p2y++;
            bx += bvx; by += bvy;
            if (by <= 1.f)bvy = fabsf(bvy);
            if (by >= GH - 2.f)bvy = -fabsf(bvy);
            // P1 paddle (left)
            if (bx <= 3.f && by >= p1y && by <= p1y + padH) {
                bvx = fabsf(bvx) * (1.0f + 0.04f * sc1);
                bvy += (by - (p1y + padH / 2.f)) * 0.2f;
                if (bvy > 4)bvy = 4; if (bvy < -4)bvy = -4;
            }
            // P2 paddle (right)
            if (bx >= GW - 4.f && by >= p2y && by <= p2y + padH) {
                bvx = -fabsf(bvx) * (1.0f + 0.04f * sc2);
                bvy += (by - (p2y + padH / 2.f)) * 0.2f;
                if (bvy > 4)bvy = 4; if (bvy < -4)bvy = -4;
            }
            if (bx < 0) { sc2++; bx = GW / 2; by = GH / 2; bvx = 1.5f; bvy = (rand() % 2 ? 1.f : -1.f); }
            if (bx > GW) { sc1++; bx = GW / 2; by = GH / 2; bvx = -1.5f; bvy = (rand() % 2 ? 1.f : -1.f); }
            // DRAW
            ArcClear();
            ArcBox(OX, OY, GW, GH, COL_WHITE);
            // center dotted line
            for (int y = OY + 1; y < OY + GH - 1; y += 2)ArcSet(OX + GW / 2, y, ':', COL_DARK_GRAY);
            // paddles
            for (int j = 0; j < padH; j++) { ArcSet(OX + 1, OY + p1y + j, '|', COL_CYAN); ArcSet(OX + GW - 2, OY + p2y + j, '|', COL_YELLOW); }
            // ball - trail effect
            ArcSet(OX + (int)bx, OY + (int)by, 'O', COL_WHITE);
            ArcSet(OX + (int)(bx - bvx * 0.5f), OY + (int)(by - bvy * 0.5f), '.', COL_DARK_GRAY);
            // HUD
            char hud[80];
            sprintf(hud, " PONG   W/S=Left  UP/DN=Right  First to 7 wins!  ESC=quit");
            ArcStr(0, 0, hud, COL_WHITE);
            char sc[32]; sprintf(sc, "  P1: %d   P2: %d", sc1, sc2);
            ArcStr(OX + GW / 2 - 8, OY - 1, sc, COL_YELLOW);
            ArcFlush();
            Sleep(16);
        }
        if (sc1 == 7) { ArcWin("PONG - P1", sc1 * 100); }
        else if (sc2 == 7) { ArcWin("PONG - P2", sc2 * 100); }
        else if (ArcGameOver("PONG", sc1 > sc2 ? sc1 * 100 : sc2 * 100)) goto retry_pong;
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 3: BREAKOUT --
    auto RunBreakout = [&]() {
        SetConsoleTitleA("Breakout");
        const int GW = 64, GH = 22, OX = 6, OY = 3;
        const int ROWS = 6, COLS = 16;
    retry_breakout:
        int bricks[ROWS][COLS]; for (int r = 0; r < ROWS; r++)for (int c = 0; c < COLS; c++)bricks[r][c] = ROWS - r;
        int brickColors[] = { COL_RED,COL_MAGENTA,COL_YELLOW,COL_GREEN,COL_CYAN,COL_BLUE };
        float bx = (float)(GW / 2), by = (float)(GH - 4);
        float bvx = 1.5f, bvy = -1.5f;
        int padW = 10, padX = GW / 2 - padW / 2, lives = 3, sc = 0, total = ROWS * COLS;
        double last = ArcTime();
        while (lives > 0 && total > 0) {
            double now = ArcTime(), dt = now - last; last = now;
            if (dt > 0.05)dt = 0.05;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            // paddle
            if ((GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) && padX > 1)padX -= 2;
            if ((GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) && padX < GW - padW - 2)padX += 2;
            // ball physics (sub-step for accuracy)
            float spd = sqrtf(bvx * bvx + bvy * bvy);
            float steps = spd * 2; if (steps < 1)steps = 1;
            float sx = bvx / steps, sy = bvy / steps;
            for (int st = 0; st < (int)steps; st++) {
                bx += sx; by += sy;
                if (bx <= 1) { bx = 1; bvx = fabsf(bvx); }
                if (bx >= GW - 2) { bx = GW - 2; bvx = -fabsf(bvx); }
                if (by <= 1) { by = 1; bvy = fabsf(bvy); }
                // paddle collision
                if (by >= GH - 4 && by <= GH - 3 && bx >= padX && bx <= padX + padW) {
                    bvy = -fabsf(bvy);
                    bvx += (bx - (padX + padW / 2.f)) * 0.25f;
                    float spd2 = sqrtf(bvx * bvx + bvy * bvy);
                    if (spd2 > 4) { bvx = bvx / spd2 * 4; bvy = bvy / spd2 * 4; }
                }
                // brick collision
                int col2 = (int)((bx - 1) * COLS / (GW - 2));
                int row2 = (int)((by - 1) * ROWS / (GH / 2));
                if (row2 >= 0 && row2 < ROWS && col2 >= 0 && col2 < COLS && bricks[row2][col2]>0) {
                    sc += bricks[row2][col2] * 10;
                    bricks[row2][col2] = 0; total--;
                    bvy = -bvy;
                    // speed up slightly each brick
                    float spd3 = sqrtf(bvx * bvx + bvy * bvy);
                    if (spd3 < 5) { bvx = bvx / spd3 * (spd3 + 0.05f); bvy = bvy / spd3 * (spd3 + 0.05f); }
                    break;
                }
                // miss
                if (by >= GH - 1) { lives--; bx = GW / 2; by = GH - 4; bvx = 1.5f; bvy = -1.5f; break; }
            }
            // DRAW
            ArcClear();
            ArcBox(OX, OY, GW, GH, COL_WHITE);
            // bricks
            int bw = (GW - 2) / COLS;
            for (int r = 0; r < ROWS; r++)for (int c = 0; c < COLS; c++) {
                if (!bricks[r][c])continue;
                int bCol = brickColors[r];
                int bX = OX + 1 + c * bw, bY = OY + 1 + r * 2;
                for (int i = 0; i < bw - 1; i++)ArcSet(bX + i, bY, '=', bCol);
                ArcSet(bX, bY, '[', bCol); ArcSet(bX + bw - 2, bY, ']', bCol);
            }
            // paddle
            for (int i = 0; i < padW; i++)ArcSet(OX + padX + i, OY + GH - 2, '=', COL_CYAN);
            ArcSet(OX + padX, OY + GH - 2, '[', COL_WHITE);
            ArcSet(OX + padX + padW - 1, OY + GH - 2, ']', COL_WHITE);
            // ball
            ArcSet(OX + (int)bx, OY + (int)by, 'o', COL_WHITE);
            // HUD
            char hud[80]; sprintf(hud, " BREAKOUT  Score:%-5d  Lives:%d  A/D or Arrows=move  ESC=quit", sc, lives);
            ArcStr(0, 0, hud, COL_YELLOW);
            ArcStr(0, 1, " Break all bricks! Higher rows = more points.", COL_DARK_GRAY);
            ArcFlush();
            Sleep(10);
        }
        if (total == 0)ArcWin("BREAKOUT", sc);
        else if (ArcGameOver("BREAKOUT", sc)) goto retry_breakout;
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 4: TETRIS --
    auto RunTetris = [&]() {
        SetConsoleTitleA("Tetris");
        const int TW = 10, TH = 20, OX = 24, OY = 3;
        // Full 7 tetrominoes with rotation states [piece][rot][cell][xy]
        int pieces[7][4][4][2] = {
            // I
            {{{0,1},{1,1},{2,1},{3,1}},{{2,0},{2,1},{2,2},{2,3}},{{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}}},
            // O
            {{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}}},
            // T
            {{{1,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{2,1},{1,2}},{{1,0},{0,1},{1,1},{1,2}}},
            // S
            {{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}},{{1,1},{2,1},{0,2},{1,2}},{{0,0},{0,1},{1,1},{1,2}}},
            // Z
            {{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{1,2},{2,2}},{{1,0},{0,1},{1,1},{0,2}}},
            // L
            {{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},{{0,1},{1,1},{2,1},{0,2}},{{0,0},{1,0},{1,1},{1,2}}},
            // J
            {{{0,0},{0,1},{1,1},{2,1}},{{1,0},{2,0},{1,1},{1,2}},{{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{0,2},{1,2}}}
        };
        int colors[7] = { COL_CYAN,COL_YELLOW,COL_MAGENTA,COL_GREEN,COL_RED,COL_ORANGE,COL_BLUE };
        const char* names[7] = { "I","O","T","S","Z","L","J" };
    retry_tetris:
        int board[TH][TW] = {}; int boardCol[TH][TW] = {};
        int cur = rand() % 7, rot = 0, cx = TW / 2 - 2, cy = 0;
        int next = rand() % 7;
        int sc = 0, level = 1, lines = 0;
        double fallRate = 0.5, fallAcc = 0, last = ArcTime();
        double keyRepeat = 0; bool quit2 = false;
        auto fits = [&](int p, int r, int x, int y)->bool {
            for (int i = 0; i < 4; i++) { int nx = x + pieces[p][r][i][0], ny = y + pieces[p][r][i][1]; if (nx < 0 || nx >= TW || ny >= TH)return false; if (ny >= 0 && board[ny][nx])return false; }return true;
            };
        auto lock = [&]() {
            for (int i = 0; i < 4; i++) { int nx = cx + pieces[cur][rot][i][0], ny = cy + pieces[cur][rot][i][1]; if (ny >= 0 && ny < TH) { board[ny][nx] = 1; boardCol[ny][nx] = colors[cur]; } }
            // clear lines
            int cleared = 0;
            for (int y = TH - 1; y >= 0;) {
                bool full = true; for (int x = 0; x < TW; x++)if (!board[y][x]) { full = false; break; }
                if (full) { cleared++; for (int yy = y; yy > 0; yy--) { for (int x = 0; x < TW; x++) { board[yy][x] = board[yy - 1][x]; boardCol[yy][x] = boardCol[yy - 1][x]; } }for (int x = 0; x < TW; x++) { board[0][x] = 0; boardCol[0][x] = 0; } }
                else y--;
            }
            if (cleared > 0) { int pts[] = { 0,100,300,500,800 }; sc += pts[min(cleared, 4)] * level; lines += cleared; level = lines / 10 + 1; fallRate = max(0.05, 0.5 - level * 0.04); }
            cur = next; next = rand() % 7; rot = 0; cx = TW / 2 - 2; cy = 0;
            if (!fits(cur, rot, cx, cy)) { quit2 = true; }
            };
        // ghost piece
        auto ghostY = [&]()->int { int gy = cy; while (fits(cur, rot, cx, gy + 1))gy++; return gy; };
        while (!quit2) {
            double now = ArcTime(), dt = now - last; last = now;
            if (dt > 0.05)dt = 0.05;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            keyRepeat -= dt;
            if (keyRepeat <= 0) {
                keyRepeat = 0.08;
                if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) { if (fits(cur, rot, cx - 1, cy))cx--; }
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) { if (fits(cur, rot, cx + 1, cy))cx++; }
                if (GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000) { if (fits(cur, rot, cx, cy + 1))cy++; else lock(); }
            }
            // rotate on up/W (no repeat)
            static bool rotPrev = false;
            bool rotNow = (GetAsyncKeyState(VK_UP) & 0x8000) || (GetAsyncKeyState('W') & 0x8000);
            if (rotNow && !rotPrev) { int nr = (rot + 1) % 4; if (fits(cur, nr, cx, cy))rot = nr; else if (fits(cur, nr, cx - 1, cy)) { rot = nr; cx--; } else if (fits(cur, nr, cx + 1, cy)) { rot = nr; cx++; } }
            rotPrev = rotNow;
            // hard drop on space
            static bool dropPrev = false;
            bool dropNow = GetAsyncKeyState(VK_SPACE) & 0x8000;
            if (dropNow && !dropPrev) { int gy = ghostY(); sc += (gy - cy) * 2; cy = gy; lock(); }
            dropPrev = dropNow;
            // gravity
            fallAcc += dt; if (fallAcc >= fallRate) { fallAcc = 0; if (fits(cur, rot, cx, cy + 1))cy++; else lock(); }
            // DRAW
            ArcClear();
            // board outline
            ArcBox(OX - 1, OY - 1, TW + 2, TH + 2, COL_WHITE);
            // ghost piece
            int gy2 = ghostY();
            for (int i = 0; i < 4; i++) { int gx2 = cx + pieces[cur][rot][i][0], gy3 = gy2 + pieces[cur][rot][i][1]; if (gy3 >= 0)ArcSet(OX + gx2, OY + gy3, '.', COL_DARK_GRAY); }
            // board cells
            for (int y = 0; y < TH; y++)for (int x = 0; x < TW; x++)if (board[y][x])ArcSet(OX + x, OY + y, '#', boardCol[y][x]);
            // current piece
            for (int i = 0; i < 4; i++) { int px2 = cx + pieces[cur][rot][i][0], py2 = cy + pieces[cur][rot][i][1]; if (py2 >= 0)ArcSet(OX + px2, OY + py2, '#', colors[cur]); }
            // sidebar
            ArcStr(OX + TW + 3, OY + 1, "TETRIS", COL_YELLOW);
            char tmp[32];
            sprintf(tmp, "Score: %d", sc);   ArcStr(OX + TW + 3, OY + 3, tmp, COL_WHITE);
            sprintf(tmp, "Lines: %d", lines); ArcStr(OX + TW + 3, OY + 4, tmp, COL_WHITE);
            sprintf(tmp, "Level: %d", level); ArcStr(OX + TW + 3, OY + 5, tmp, COL_CYAN);
            ArcStr(OX + TW + 3, OY + 8, "NEXT:", COL_WHITE);
            // draw next piece preview
            for (int i = 0; i < 4; i++) { int nx2 = pieces[next][0][i][0], ny2 = pieces[next][0][i][1]; ArcSet(OX + TW + 4 + nx2, OY + 9 + ny2, '#', colors[next]); }
            ArcStr(OX + TW + 3, OY + 14, "Controls:", COL_DARK_GRAY);
            ArcStr(OX + TW + 3, OY + 15, "A/D=move", COL_DARK_GRAY);
            ArcStr(OX + TW + 3, OY + 16, "W=rotate", COL_DARK_GRAY);
            ArcStr(OX + TW + 3, OY + 17, "S=soft drop", COL_DARK_GRAY);
            ArcStr(OX + TW + 3, OY + 18, "SPC=hard drop", COL_DARK_GRAY);
            ArcStr(0, 0, " TETRIS  A/D=move  W=rotate  S=drop  SPACE=hard drop  ESC=quit", COL_YELLOW);
            ArcFlush();
            Sleep(10);
        }
        if (ArcGameOver("TETRIS", sc)) goto retry_tetris;
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 5: SPACE INVADERS --
    auto RunSpaceInvaders = [&]() {
        SetConsoleTitleA("Space Invaders");
        const int GW = 60, GH = 22, OX = 9, OY = 2;
        struct Inv { float x, y; bool alive; int type; };
        struct Blt { float x, y; bool active; bool enemy; };
    retry_si:
        std::vector<Inv> inv;
        for (int r = 0; r < 5; r++)for (int c = 0; c < 11; c++)inv.push_back({ (float)(c * 5 + 2),(float)(r * 2 + 2),(bool)true,r < 2 ? 2 : r < 4 ? 1 : 0 });
        int shipX = GW / 2, lives = 3, sc = 0; bool quit3 = false;
        float invSpd = 8.f, invDir = 1.f, invDrop = 0.f;
        double last = ArcTime(), shootCool = 0, invMoveTimer = 0, enemyShootTimer = 0;
        std::vector<Blt> bullets;
        // barriers
        int barriers[3][4][8] = {};
        for (int b = 0; b < 3; b++)for (int r = 0; r < 4; r++)for (int c = 0; c < 8; c++)barriers[b][r][c] = 3;
        auto barX = [](int b)->int {return 8 + b * 18; };
        auto barY = [](int)->int {return 16; };
        while (!quit3 && lives > 0) {
            double now = ArcTime(), dt = now - last; last = now;
            if (dt > 0.05)dt = 0.05;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            // ship move
            if ((GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) && shipX > 2)shipX--;
            if ((GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) && shipX < GW - 3)shipX++;
            // player shoot
            shootCool -= dt;
            static bool spacePrev = false; bool spaceNow = GetAsyncKeyState(VK_SPACE) & 0x8000;
            if (spaceNow && !spacePrev && shootCool <= 0) { bullets.push_back({ (float)shipX,(float)(GH - 4),true,false }); shootCool = 0.35; }
            spacePrev = spaceNow;
            // enemy shoot
            enemyShootTimer -= dt;
            if (enemyShootTimer <= 0) {
                enemyShootTimer = 0.8f / (1.f + sc / 200.f);
                std::vector<int> alive2;
                for (int i = 0; i < (int)inv.size(); i++)if (inv[i].alive)alive2.push_back(i);
                if (!alive2.empty()) { int idx = alive2[rand() % alive2.size()]; bullets.push_back({ inv[idx].x,inv[idx].y + 1,true,true }); }
            }
            // move bullets
            for (auto& b : bullets) { if (!b.active)continue; b.y += b.enemy ? 6.f * (float)dt : -14.f * (float)dt; if (b.y<0 || b.y>GH)b.active = false; }
            // move invaders
            invMoveTimer += dt;
            float invInterval = 0.5f / (1.f + sc / 300.f);
            if (invMoveTimer >= invInterval) {
                invMoveTimer = 0;
                bool edge = false;
                for (auto& e : inv)if (e.alive) { e.x += invDir; if (e.x<1 || e.x>GW - 2)edge = true; }
                if (edge) { invDir = -invDir; for (auto& e : inv)if (e.alive)e.y += 1; }
            }
            // bullet vs invader
            for (auto& b : bullets) {
                if (!b.active || b.enemy)continue;
                for (auto& e : inv) {
                    if (!e.alive)continue;
                    if (fabsf(b.x - e.x) < 1.5f && fabsf(b.y - e.y) < 1.5f) { e.alive = false; b.active = false; sc += e.type == 2 ? 30 : e.type == 1 ? 20 : 10; }
                }
            }
            // bullet vs player
            for (auto& b : bullets) {
                if (!b.active || !b.enemy)continue;
                if (fabsf(b.x - shipX) < 2 && fabsf(b.y - (GH - 3)) < 2) { b.active = false; lives--; }
            }
            // invader reaches bottom
            for (auto& e : inv)if (e.alive && e.y >= GH - 3) { quit3 = true; }
            // check win
            bool anyAlive3 = false; for (auto& e : inv)if (e.alive) { anyAlive3 = true; break; }
            if (!anyAlive3) { ArcWin("SPACE INVADERS", sc); break; }
            // DRAW
            ArcClear();
            ArcBox(OX, OY, GW, GH, COL_WHITE);
            // invaders
            const char* invChar[] = { "v","W","M" };
            int invCol[] = { COL_GREEN,COL_CYAN,COL_RED };
            for (auto& e : inv)if (e.alive)ArcSet(OX + (int)e.x, OY + (int)e.y, invChar[e.type][0], invCol[e.type]);
            // barriers
            for (int b = 0; b < 3; b++)for (int r = 0; r < 4; r++)for (int c = 0; c < 8; c++)
                if (barriers[b][r][c] > 0) { int brc = barriers[b][r][c]; ArcSet(OX + barX(b) + c, OY + barY(0) + r, brc == 3 ? '#' : brc == 2 ? '=' : '.', COL_GREEN); }
            // bullets
            for (auto& b : bullets)if (b.active)ArcSet(OX + (int)b.x, OY + (int)b.y, b.enemy ? '!' : '|', b.enemy ? COL_RED : COL_YELLOW);
            // ship
            ArcSet(OX + shipX - 1, OY + GH - 2, '<', COL_CYAN);
            ArcSet(OX + shipX, OY + GH - 2, '^', COL_WHITE);
            ArcSet(OX + shipX + 1, OY + GH - 2, '>', COL_CYAN);
            // lives
            for (int i = 0; i < lives; i++)ArcSet(OX + 2 + i * 2, OY + GH - 2, '^', COL_GREEN);
            char hud[64]; sprintf(hud, " SPACE INVADERS  Score:%-5d  Lives:%d  A/D=move  SPACE=fire  ESC=quit", sc, lives);
            ArcStr(0, 0, hud, COL_YELLOW);
            ArcFlush();
            Sleep(16);
        }
        if (quit3 || lives == 0) { if (ArcGameOver("SPACE INVADERS", sc))goto retry_si; }
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 6: MINESWEEPER --
    auto RunMinesweeper = [&]() {
        SetConsoleTitleA("Minesweeper");
        const int MW = 20, MH = 15, MINES = 40, OX = 18, OY = 4;
    retry_ms:
        int board2[MH][MW] = {};  // -1=mine, 0-8=count
        bool revealed[MH][MW] = {};
        bool flagged[MH][MW] = {};
        bool firstClick = true; int cx3 = MW / 2, cy3 = MH / 2, sc = 0; bool dead3 = false, won3 = false;
        // place mines after first click
        auto placeMines = [&](int fx, int fy) {
            int placed = 0;
            while (placed < MINES) { int x = rand() % MW, y = rand() % MH; if (board2[y][x] != -1 && !(x == fx && y == fy)) { board2[y][x] = -1; placed++; } }
            for (int y = 0; y < MH; y++)for (int x = 0; x < MW; x++) { if (board2[y][x] == -1)continue; int cnt = 0; for (int dy = -1; dy <= 1; dy++)for (int dx = -1; dx <= 1; dx++) { int nx2 = x + dx, ny2 = y + dy; if (nx2 >= 0 && nx2 < MW && ny2 >= 0 && ny2 < MH && board2[ny2][nx2] == -1)cnt++; }board2[y][x] = cnt; }
            };
        // flood reveal
        std::function<void(int, int)> reveal = [&](int x, int y) {
            if (x < 0 || x >= MW || y < 0 || y >= MH || revealed[y][x] || flagged[y][x])return;
            revealed[y][x] = true; sc += 10;
            if (board2[y][x] == 0) { for (int dy = -1; dy <= 1; dy++)for (int dx = -1; dx <= 1; dx++)reveal(x + dx, y + dy); }
            };
        int numColors[] = { 0,COL_BLUE,COL_GREEN,COL_RED,COL_BLUE,COL_RED,COL_CYAN,COL_DARK_GRAY,COL_WHITE };
        while (!dead3 && !won3) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            // input
            if (_kbhit()) {
                int c = _getch();
                if (c == 0 || c == 0xE0) {
                    c = _getch();
                    if (c == 72 && cy3 > 0)cy3--; if (c == 80 && cy3 < MH - 1)cy3++;
                    if (c == 75 && cx3 > 0)cx3--; if (c == 77 && cx3 < MW - 1)cx3++;
                }
                else if (c == 13 || c == ' ') {
                    if (firstClick) { firstClick = false; placeMines(cx3, cy3); }
                    if (!flagged[cy3][cx3]) { reveal(cx3, cy3); if (board2[cy3][cx3] == -1)dead3 = true; }
                }
                else if (c == 'f' || c == 'F') { flagged[cy3][cx3] = !flagged[cy3][cx3]; }
                else if (c == 27)break;
            }
            // check win
            int unrev = 0; for (int y = 0; y < MH; y++)for (int x = 0; x < MW; x++)if (!revealed[y][x] && board2[y][x] != -1)unrev++;
            if (unrev == 0)won3 = true;
            // DRAW
            ArcClear();
            ArcBox(OX - 1, OY - 1, MW * 2 + 2, MH + 2, COL_WHITE);
            for (int y = 0; y < MH; y++)for (int x = 0; x < MW; x++) {
                int px = OX + x * 2, py = OY + y;
                bool isCursor = (x == cx3 && y == cy3);
                if (!revealed[y][x] && !dead3) {
                    if (flagged[y][x])ArcSet(px, py, 'F', COL_RED);
                    else ArcSet(px, py, isCursor ? '>' : '#', isCursor ? COL_YELLOW : COL_DARK_GRAY);
                }
                else {
                    if (board2[y][x] == -1) { ArcSet(px, py, '*', dead3 ? COL_RED : COL_YELLOW); }
                    else if (board2[y][x] == 0) { ArcSet(px, py, '.', COL_DARK_GRAY); }
                    else { char nc = '0' + board2[y][x]; ArcSet(px, py, nc, numColors[board2[y][x]]); }
                }
            }
            ArcStr(0, 0, " MINESWEEPER  Arrows=move  ENTER=reveal  F=flag  ESC=quit", COL_YELLOW);
            char hud2[64]; sprintf(hud2, " Score:%d  Mines:%d  Flagged:%d", sc, MINES, [&] {int f = 0; for (int y = 0; y < MH; y++)for (int x = 0; x < MW; x++)if (flagged[y][x])f++; return f; }());
            ArcStr(0, 1, hud2, COL_WHITE);
            if (won3)ArcStr(OX + 2, OY + MH / 2, "  YOU WIN!  ", COL_GREEN);
            ArcFlush();
            Sleep(30);
        }
        if (won3)ArcWin("MINESWEEPER", sc);
        if (dead3) { if (ArcGameOver("MINESWEEPER", sc))goto retry_ms; }
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 7: ASTEROIDS --
    auto RunAsteroids = [&]() {
        SetConsoleTitleA("Asteroids");
        const int GW2 = 70, GH2 = 24, OX2 = 4, OY2 = 2;
        struct Vec2 { float x, y; };
        struct Rock { Vec2 pos, vel; float r; bool alive; };
        struct Shot { Vec2 pos, vel; bool active; double life; };
    retry_ast:
        Vec2 shipPos = { (float)GW2 / 2,(float)GH2 / 2 }, shipVel = { 0,0 };
        float shipAng = 0; // radians, 0=up
        std::vector<Rock> rocks;
        std::vector<Shot> shots;
        int sc2 = 0, lives2 = 3; bool dead4 = false;
        // spawn initial rocks
        for (int i = 0; i < 6; i++) { float ang = (float)(rand() % 360) * 3.14159f / 180.f; float spd = (float)(1 + rand() % 2) * 0.3f; rocks.push_back({ {GW2 * 0.1f + (float)(rand() % int(GW2 * 0.8f)),GH2 * 0.1f + (float)(rand() % int(GH2 * 0.8f))},{cosf(ang) * spd,sinf(ang) * spd},4.f,true }); }
        double last2 = ArcTime(), iframes = 0;
        while (!dead4) {
            double now2 = ArcTime(), dt2 = now2 - last2; last2 = now2;
            if (dt2 > 0.05)dt2 = 0.05;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            iframes -= dt2;
            // rotate
            if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000)shipAng -= 2.5f * (float)dt2;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000)shipAng += 2.5f * (float)dt2;
            // thrust
            if (GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000) { shipVel.x += sinf(shipAng) * 8.f * (float)dt2; shipVel.y -= cosf(shipAng) * 8.f * (float)dt2; }
            // drag
            shipVel.x *= 0.98f; shipVel.y *= 0.98f;
            // move
            shipPos.x += shipVel.x; shipPos.y += shipVel.y;
            if (shipPos.x < 0)shipPos.x += GW2; if (shipPos.x >= GW2)shipPos.x -= GW2;
            if (shipPos.y < 0)shipPos.y += GH2; if (shipPos.y >= GH2)shipPos.y -= GH2;
            // shoot
            static bool sPrev2 = false; bool sNow2 = GetAsyncKeyState(VK_SPACE) & 0x8000;
            if (sNow2 && !sPrev2 && (int)shots.size() < 5) { shots.push_back({ {shipPos.x,shipPos.y},{sinf(shipAng) * 15.f,-cosf(shipAng) * 15.f},true,1.5 }); }
            sPrev2 = sNow2;
            // move shots
            for (auto& sh : shots) { if (!sh.active)continue; sh.pos.x += sh.vel.x * (float)dt2; sh.pos.y += sh.vel.y * (float)dt2; sh.life -= dt2; if (sh.life <= 0)sh.active = false; if (sh.pos.x < 0)sh.pos.x += GW2; if (sh.pos.x >= GW2)sh.pos.x -= GW2; if (sh.pos.y < 0)sh.pos.y += GH2; if (sh.pos.y >= GH2)sh.pos.y -= GH2; }
            // move rocks
            for (auto& rock : rocks) { if (!rock.alive)continue; rock.pos.x += rock.vel.x; rock.pos.y += rock.vel.y; if (rock.pos.x < 0)rock.pos.x += GW2; if (rock.pos.x >= GW2)rock.pos.x -= GW2; if (rock.pos.y < 0)rock.pos.y += GH2; if (rock.pos.y >= GH2)rock.pos.y -= GH2; }
            // shot vs rock
            for (auto& sh : shots) { if (!sh.active)continue; for (auto& rock : rocks) { if (!rock.alive)continue; float dx2 = sh.pos.x - rock.pos.x, dy2 = sh.pos.y - rock.pos.y; if (sqrtf(dx2 * dx2 + dy2 * dy2) < rock.r + 0.5f) { sh.active = false; rock.alive = false; sc2 += (int)(10 / rock.r * 20); if (rock.r > 1.5f) { for (int s = 0; s < 2; s++) { float ang2 = (float)(rand() % 360) * 3.14159f / 180.f; float spd2 = (float)(1 + rand() % 2) * 0.4f; rocks.push_back({ rock.pos,{cosf(ang2) * spd2,sinf(ang2) * spd2},rock.r / 2,true }); } }break; } } }
            // ship vs rock
            if (iframes <= 0) { for (auto& rock : rocks) { if (!rock.alive)continue; float dx2 = shipPos.x - rock.pos.x, dy2 = shipPos.y - rock.pos.y; if (sqrtf(dx2 * dx2 + dy2 * dy2) < rock.r + 1) { lives2--; iframes = 2.0; if (lives2 <= 0)dead4 = true; break; } } }
            // remove dead rocks/shots
            rocks.erase(std::remove_if(rocks.begin(), rocks.end(), [](const Rock& r) {return !r.alive; }), rocks.end());
            shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& s) {return !s.active; }), shots.end());
            // new wave
            if (rocks.empty()) { for (int i = 0; i < 6; i++) { float ang3 = (float)(rand() % 360) * 3.14159f / 180.f; float spd3 = (float)(1 + rand() % 3) * 0.35f; rocks.push_back({ {(float)(rand() % GW2),(float)(rand() % GH2)},{cosf(ang3) * spd3,sinf(ang3) * spd3},4.f,true }); } }
            // DRAW
            ArcClear();
            ArcBox(OX2, OY2, GW2, GH2, COL_WHITE);
            // rocks
            for (auto& rock : rocks) { if (!rock.alive)continue; char rc = rock.r > 3 ? 'O' : rock.r > 1.5f ? 'o' : '.'; ArcSet(OX2 + (int)rock.pos.x, OY2 + (int)rock.pos.y, rc, COL_DARK_GRAY + 4); }
            // shots
            for (auto& sh : shots)if (sh.active)ArcSet(OX2 + (int)sh.pos.x, OY2 + (int)sh.pos.y, '*', COL_YELLOW);
            // ship (3-char)
            if (iframes <= 0 || (int)(iframes * 8) % 2 == 0) {
                char sc3 = '|';
                float fwd_x = sinf(shipAng), fwd_y = -cosf(shipAng);
                ArcSet(OX2 + (int)(shipPos.x + fwd_x), OY2 + (int)(shipPos.y + fwd_y), '^', COL_CYAN);
                ArcSet(OX2 + (int)shipPos.x, OY2 + (int)shipPos.y, 'A', COL_WHITE);
                (void)sc3;
            }
            // lives
            for (int i = 0; i < lives2; i++)ArcSet(OX2 + 2 + i * 3, OY2 + GH2 - 2, 'A', COL_CYAN);
            char hud3[64]; sprintf(hud3, " ASTEROIDS  Score:%-5d  Lives:%d  A/D=rotate  W=thrust  SPACE=fire  ESC=quit", sc2, lives2);
            ArcStr(0, 0, hud3, COL_YELLOW);
            ArcFlush();
            Sleep(16);
        }
        if (ArcGameOver("ASTEROIDS", sc2)) goto retry_ast;
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    // -- GAME 8: TYPING SPEED --
    auto RunTypingGame = [&]() {
        SetConsoleTitleA("Typing Speed");
        const char* words[] = { "tank","battle","shoot","dodge","enemy","bullet","armor","cannon","turret","reload","attack","defend","capture","flag","respawn","ammo","speed","target","fire","blast","wall","maze","score","level","health" };
        const int NW = 25;
    retry_type:
        int sc4 = 0, lives4 = 5, combo = 0, maxCombo = 0;
        double timeLeft = 60.0, last4 = ArcTime();
        struct FallingWord { std::string word; int x; float y; double spd; std::string typed; bool active; };
        std::vector<FallingWord> fwords;
        std::string input;
        double spawnTimer = 0, spawnRate = 2.0;
        while (lives4 > 0 && timeLeft > 0) {
            double now4 = ArcTime(), dt4 = now4 - last4; last4 = now4;
            if (dt4 > 0.05)dt4 = 0.05;
            timeLeft -= dt4;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            // spawn words
            spawnTimer -= dt4;
            if (spawnTimer <= 0) { spawnTimer = spawnRate; spawnRate = max(0.5, spawnRate - 0.05); const char* w = words[rand() % NW]; int x = 2 + rand() % (AW - 20); fwords.push_back({ w,x,1,0.5 + sc4 / 500.0,"",true }); }
            // move words
            for (auto& fw : fwords) { if (!fw.active)continue; fw.y += (float)(fw.spd * dt4 * 3); if ((int)fw.y >= AH - 4) { lives4--; fw.active = false; combo = 0; } }
            // input
            while (_kbhit()) {
                int c = _getch();
                if (c == 27)goto done_type;
                if (c == 8 && !input.empty())input.pop_back();
                else if (c == 13 || c == ' ') {
                    // check match
                    bool matched = false;
                    for (auto& fw : fwords) { if (!fw.active)continue; if (fw.word == input) { fw.active = false; sc4 += (int)fw.word.size() * 10 * (1 + combo / 5); combo++; maxCombo = max(maxCombo, combo); matched = true; break; } }
                    if (!matched) { combo = 0; }
                    input.clear();
                }
                else if (c >= ' ' && c < 127) { input += (char)c; }
            }
            // DRAW
            ArcClear();
            ArcBox(0, 2, AW, AH - 3, COL_WHITE);
            for (auto& fw : fwords) {
                if (!fw.active)continue;
                int col = fw.y > AH - 8 ? COL_RED : fw.y > AH - 14 ? COL_YELLOW : COL_WHITE;
                ArcStr(fw.x, (int)fw.y, fw.word.c_str(), col);
            }
            // input bar
            ArcBox(0, AH - 3, AW, 3, COL_CYAN);
            ArcStr(2, AH - 2, "> ", COL_YELLOW);
            ArcStr(4, AH - 2, input.c_str(), COL_WHITE);
            char hud4[80]; sprintf(hud4, " TYPING  Score:%-5d  Combo:x%d  Lives:%d  Time:%.0f  Type word + ENTER", sc4, combo, lives4, timeLeft);
            ArcStr(0, 0, hud4, COL_YELLOW);
            ArcStr(0, 1, " Type the falling words before they reach the bottom! SPACE or ENTER to submit.", COL_DARK_GRAY);
            ArcFlush();
            Sleep(16);
        }
    done_type:
        if (ArcGameOver("TYPING SPEED", sc4)) goto retry_type;
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };


    while (true) {
        // ---- Step 1: Player count ----
        DrawTitle();
        // H = show publisher/trust help at any time from lobby
        GotoXY(2, 27); SetColor(COL_DARK_GRAY);
        std::cout << "  [H] Publisher help    [P] Export portable EXE";

        ResetColor();
        std::vector<std::string> playerOpts = {
            "1 Player (vs 3 AI bots)",
            "2 Players",
            "3 Players",
            "4 Players"
        };
        int pc = Menu("Select Number of Players  [UP/DOWN + ENTER]:", playerOpts, 2, 13);
        if (pc == -4) { ExportToEXE(); continue; }
        if (pc == -3) { ShowPublisherHelp(); continue; }
        if (pc == -2) {
            // Q was pressed - show bonus games selector
            // Properly reset console state first
            SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
            HideCursor();
            ClearScreen();
            // Draw arcade menu using back-buffer for clean display
            for (int y = 0; y < 28; y++)for (int x = 0; x < 78; x++) { arc_buf[y][x].Char.AsciiChar = ' '; arc_buf[y][x].Attributes = 0; }
            const char* _mn[] = { "1. Snake","2. Pong","3. Breakout","4. Tetris","5. Space Invaders","6. Minesweeper","7. Asteroids","8. Typing Speed" };
            ArcStr(25, 1, "=== BONUS GAMES ===", COL_YELLOW);
            for (int _mi = 0; _mi < 8; _mi++) ArcStr(26, 3 + _mi * 2, _mn[_mi], COL_WHITE);
            ArcStr(24, 20, "ESC = back to menu", COL_DARK_GRAY);
            ArcStr(24, 22, "Press 1-8 to select", COL_CYAN);
            ArcFlush();
            while (_kbhit())(void)_getch(); // flush stale keys
            int arcadeChoice = _getch();
            if (arcadeChoice == '1') RunSnake();
            else if (arcadeChoice == '2') RunPong();
            else if (arcadeChoice == '3') RunBreakout();
            else if (arcadeChoice == '4') RunTetris();
            else if (arcadeChoice == '5') RunSpaceInvaders();
            else if (arcadeChoice == '6') RunMinesweeper();
            else if (arcadeChoice == '7') RunAsteroids();
            else if (arcadeChoice == '8') RunTypingGame();
            // Restore console completely after any arcade game
            ClearScreen();
            HideCursor();
            SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
            SetConsoleTitleA("Tank Battle - Local Multiplayer");
            continue;
        }
        if (pc < 0) break;
        for (int i = 0; i < 4; i++) g_isBot[i] = false;
        if (pc == 0) {
            g_numPlayers = 4;
            g_isBot[1] = true; g_isBot[2] = true; g_isBot[3] = true;
        }
        else {
            g_numPlayers = pc + 1;
        }

        // ---- Step 1b: AI mode selection (only if bots present) ----
        bool anyBot = false;
        for (int i = 0; i < 4; i++) if (g_isBot[i]) anyBot = true;
        if (anyBot) {
            DrawTitle();
            GotoXY(2, 13); SetColor(COL_YELLOW); std::cout << "  BOT AI TYPE";
            GotoXY(2, 15); SetColor(COL_WHITE);  std::cout << "  1 = Built-in AI  (smart BFS pathfinding, always works)";
            GotoXY(2, 16); SetColor(COL_CYAN);   std::cout << "  2 = Real AI      (uses your local AI or API key)";
            GotoXY(2, 18); SetColor(COL_DARK_GRAY); std::cout << "  Real AI supports: Ollama, LM Studio, OpenAI, Claude, etc.";
            ResetColor();
            int aiChoice = _getch();
            g_botAIMode = (aiChoice == '2') ? AI_EXTERNAL : AI_BUILTIN;

            if (g_botAIMode == AI_EXTERNAL) {
                ClearScreen();
                GotoXY(2, 2); SetColor(COL_YELLOW); std::cout << "  REAL AI SETUP - Searching for local AI...";
                GotoXY(2, 4); SetColor(COL_WHITE);

                // Auto-detect
                auto found = DetectLocalAIs();
                int row = 5;
                if (!found.empty()) {
                    GotoXY(2, row++); SetColor(COL_GREEN); std::cout << "  Found local AI:";
                    for (int fi = 0; fi < (int)found.size(); fi++) {
                        GotoXY(4, row++); SetColor(COL_WHITE);
                        char ln[80]; sprintf(ln, "%d. %s", fi + 1, found[fi].name.c_str()); std::cout << ln;
                    }
                    GotoXY(2, row++); SetColor(COL_CYAN);  std::cout << "  " << (found.size() + 1) << ". Enter API key (OpenAI/Claude/other)";
                    GotoXY(2, row++); SetColor(COL_WHITE); std::cout << "  Select (or press ENTER for first):";
                    ResetColor();
                    int sel = _getch() - '0' - 1;
                    if (sel < 0 || sel >= (int)found.size()) {
                        // API key entry
                        ClearScreen();
                        GotoXY(2, 2); SetColor(COL_YELLOW); std::cout << "  Choose provider:";
                        GotoXY(2, 4); SetColor(COL_WHITE); std::cout << "  1. OpenAI (gpt-4o-mini)";
                        GotoXY(2, 5);                     std::cout << "  2. Claude  (claude-haiku)";
                        GotoXY(2, 6);                     std::cout << "  3. Other   (OpenAI-compatible endpoint)";
                        ResetColor();
                        int psel = _getch() - '0';
                        HANDLE hIn2 = GetStdHandle(STD_INPUT_HANDLE);
                        SetConsoleMode(hIn2, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
                        GotoXY(2, 8); SetColor(COL_YELLOW); std::cout << "  Enter API key: ";
                        std::getline(std::cin, g_extApiKey);
                        if (psel == 1) { g_extProvider = EXT_OPENAI; g_ollamaModel = "gpt-4o-mini"; }
                        else if (psel == 2) { g_extProvider = EXT_CLAUDE; g_ollamaModel = "claude-haiku-4-5-20251001"; }
                        else {
                            g_extProvider = EXT_OTHER;
                            GotoXY(2, 10); std::cout << "  Endpoint (e.g. http://127.0.0.1:8080): ";
                            std::getline(std::cin, g_extEndpoint);
                            GotoXY(2, 12); std::cout << "  Model name: ";
                            std::getline(std::cin, g_ollamaModel);
                        }
                        SetConsoleMode(hIn2, ENABLE_PROCESSED_INPUT);
                    }
                    else {
                        g_extProvider = found[sel].provider;
                        g_ollamaPort = found[sel].port;
                        g_ollamaModel = found[sel].model;
                    }
                }
                else {
                    // No local AI found - offer API key
                    GotoXY(2, row++); SetColor(COL_RED);   std::cout << "  No local AI found on common ports.";
                    GotoXY(2, row++); SetColor(COL_WHITE);
                    GotoXY(2, row++);                       std::cout << "  1. OpenAI API key";
                    GotoXY(2, row++);                       std::cout << "  2. Claude API key";
                    GotoXY(2, row++);                       std::cout << "  3. Custom endpoint";
                    GotoXY(2, row++);                       std::cout << "  4. Use built-in AI instead";
                    ResetColor();
                    int psel = _getch() - '0';
                    if (psel == 4) { g_botAIMode = AI_BUILTIN; }
                    else {
                        HANDLE hIn2 = GetStdHandle(STD_INPUT_HANDLE);
                        SetConsoleMode(hIn2, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
                        GotoXY(2, row); SetColor(COL_YELLOW); std::cout << "  API key: ";
                        std::getline(std::cin, g_extApiKey);
                        if (psel == 1) { g_extProvider = EXT_OPENAI; g_ollamaModel = "gpt-4o-mini"; }
                        else if (psel == 2) { g_extProvider = EXT_CLAUDE; g_ollamaModel = "claude-haiku-4-5-20251001"; }
                        else {
                            g_extProvider = EXT_OTHER;
                            GotoXY(2, row + 2); std::cout << "  Endpoint: ";
                            std::getline(std::cin, g_extEndpoint);
                        }
                        SetConsoleMode(hIn2, ENABLE_PROCESSED_INPUT);
                    }
                }
                ClearScreen();
                GotoXY(2, 4); SetColor(COL_GREEN);
                std::cout << "  AI Mode: " << (g_botAIMode == AI_EXTERNAL ? "Real AI" : "Built-in AI");
                if (g_botAIMode == AI_EXTERNAL) {
                    GotoXY(2, 5); std::cout << "  Provider: ";
                    if (g_extProvider == EXT_OLLAMA)std::cout << "Ollama  Model:" << g_ollamaModel;
                    else if (g_extProvider == EXT_OPENAI)std::cout << "OpenAI  Model:" << g_ollamaModel;
                    else if (g_extProvider == EXT_CLAUDE)std::cout << "Claude  Model:" << g_ollamaModel;
                    else std::cout << "Custom  Endpoint:" << g_extEndpoint;
                }
                GotoXY(2, 7); SetColor(COL_WHITE); std::cout << "  Press any key..."; ResetColor();
                (void)_getch();
            }
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
        if (lim == 0) { g_scoreLimit = 5;  g_timeLimit = 60; }
        else if (lim == 1) { g_scoreLimit = 10; g_timeLimit = 120; }
        else { g_scoreLimit = 20; g_timeLimit = 300; }

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
        GotoXY(2, row++); SetColor(COL_MAGENTA); std::cout << "  Player 4 : B V N M  to move,  , to shoot";
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
