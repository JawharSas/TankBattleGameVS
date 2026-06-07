// ============================================================
//  TANK BATTLE - Local Multiplayer Console Game
//  Supports 1-4 players on the same device
//  Gamemodes: Deathmatch, Last Man Standing, Team Battle, Capture the Flag
//  Compatible with Visual Studio 2019/2022 (Windows)
// ============================================================


// ============================================================
//  EMBEDDED MANIFEST - tells Windows this is a trusted app
// ============================================================
#pragma comment(linker, "/manifestuac:\"level='asInvoker' uiAccess='false'\"")
#pragma comment(linker, "/MANIFEST:EMBED")

// App description embedded in the binary (shows in Task Manager / Properties)
#pragma comment(exestr, "Tank Battle Game - Local Multiplayer by TankBattle Studios")

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
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
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

// ============================================================
//  CONSTANTS  (must come before any globals that reference them)
// ============================================================

const int MAP_W = 50;
const int MAP_H = 22;
const int UI_TOP = 0;
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
    // Try to open help file on desktop first
    WriteHelpFile();

    ClearScreen();
    SetColor(COL_RED);
    GotoXY(2, 1);  std::cout << "  !! WINDOWS BLOCKED THIS GAME !!";
    SetColor(COL_YELLOW);
    GotoXY(2, 3);  std::cout << "  Windows says: 'No publisher / Application Control policy'";
    GotoXY(2, 4);  std::cout << "  This is normal for games built in Visual Studio without a";
    GotoXY(2, 5);  std::cout << "  paid code-signing certificate. The game is safe to run.";

    SetColor(COL_WHITE);
    GotoXY(2, 7);  std::cout << "  HOW TO FIX IT (pick one):";
    GotoXY(2, 9);  std::cout << "  [1] Unblock the file:";
    SetColor(COL_CYAN);
    GotoXY(2, 10); std::cout << "      Right-click Tank Battle Game.exe";
    GotoXY(2, 11); std::cout << "      Properties -> check 'Unblock' -> OK";

    SetColor(COL_WHITE);
    GotoXY(2, 13); std::cout << "  [2] Run as Administrator:";
    SetColor(COL_CYAN);
    GotoXY(2, 14); std::cout << "      Right-click exe -> 'Run as administrator'";

    SetColor(COL_WHITE);
    GotoXY(2, 16); std::cout << "  [3] Add Defender exclusion (PowerShell as Admin):";
    SetColor(COL_CYAN);
    GotoXY(2, 17); std::cout << "      Add-MpPreference -ExclusionPath [path to folder]";

    SetColor(COL_WHITE);
    GotoXY(2, 19); std::cout << "  [4] Self-sign (permanent fix, PowerShell as Admin):";
    SetColor(COL_CYAN);
    GotoXY(2, 20); std::cout << "      $c = New-SelfSignedCertificate -Type CodeSigningCert";
    GotoXY(2, 21); std::cout << "           -Subject 'CN=Tank Battle' -CertStoreLocation";
    GotoXY(2, 22); std::cout << "           Cert:\\CurrentUser\\My (run in PowerShell)";
    GotoXY(2, 23); std::cout << "      Set-AuthenticodeSignature -FilePath [exe path] -Certificate $c";

    SetColor(COL_GREEN);
    GotoXY(2, 25); std::cout << "  A full guide was saved to your Desktop: HOW_TO_RUN.txt";

    SetColor(COL_DARK_GRAY);
    GotoXY(2, 27); std::cout << "  Press H during the game at any time to see this again.";
    GotoXY(2, 28); std::cout << "  Press any key to continue to the game...";
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

    // Show publisher help if running from a path that commonly gets blocked
    // or if user runs the game for the first time
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string ep(exePath);
        for (auto& ch : ep) ch = (char)tolower((unsigned char)ch);
        bool suspicious =
            ep.find("\\temp\\") != std::string::npos ||
            ep.find("\\downloads\\") != std::string::npos ||
            ep.find("\\appdata\\") != std::string::npos;
        if (suspicious) ShowPublisherHelp();
    }

    // ============================================================
    //  SECRET ARCADE  (Q in main menu)
    // ============================================================
    auto RunSnake = [&]() {
        ClearScreen();
        // Snake game
        const int SW = 40, SH = 20, STOP = 4 + SH;
        struct Pt { int x, y; };
        std::deque<Pt> snake; snake.push_back({ SW / 2,SH / 2 });
        Pt food = { rand() % SW,rand() % SH };
        int sdx = 1, sdy = 0; bool alive2 = true; int sScore = 0;
        SetConsoleTitleA("SNAKE - Arrow keys, ESC=quit");
        while (alive2) {
            // draw border
            for (int x = 0; x < SW + 2; x++) { GotoXY(x, MAP_TOP - 1); SetColor(COL_GREEN); std::cout << '-'; }
            for (int x = 0; x < SW + 2; x++) { GotoXY(x, MAP_TOP + SH); SetColor(COL_GREEN); std::cout << '-'; }
            for (int y = MAP_TOP - 1; y <= MAP_TOP + SH; y++) { GotoXY(0, y); SetColor(COL_GREEN); std::cout << '|'; GotoXY(SW + 1, y); std::cout << '|'; }
            // food
            GotoXY(food.x + 1, food.y + MAP_TOP); SetColor(COL_RED); std::cout << '*';
            // snake
            for (auto& s : snake) { GotoXY(s.x + 1, s.y + MAP_TOP); SetColor(COL_GREEN); std::cout << 'O'; }
            // score
            GotoXY(0, 0); SetColor(COL_YELLOW); std::cout << "SNAKE  Score:" << sScore << "  ESC=quit  Arrows=move  ";
            Sleep(120);
            // input
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            if (GetAsyncKeyState(VK_UP) & 0x8000 && sdy != 1) { sdx = 0; sdy = -1; }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000 && sdy != -1) { sdx = 0; sdy = 1; }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000 && sdx != 1) { sdx = -1; sdy = 0; }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000 && sdx != -1) { sdx = 1; sdy = 0; }
            Pt head = { snake.front().x + sdx,snake.front().y + sdy };
            if (head.x < 0 || head.x >= SW || head.y < 0 || head.y >= SH) { alive2 = false; break; }
            for (auto& s : snake)if (s.x == head.x && s.y == head.y) { alive2 = false; break; }
            snake.push_front(head);
            if (head.x == food.x && head.y == food.y) {
                sScore++;
                food = { rand() % SW,rand() % SH };
            }
            else {
                GotoXY(snake.back().x + 1, snake.back().y + MAP_TOP); SetColor(0); std::cout << ' ';
                snake.pop_back();
            }
        }
        ClearScreen(); GotoXY(2, 4); SetColor(COL_YELLOW); std::cout << "GAME OVER! Score: " << sScore;
        GotoXY(2, 6); SetColor(COL_WHITE); std::cout << "Press any key..."; (void)_getch();
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    auto RunPong = [&]() {
        ClearScreen();
        const int PW = 60, PH = 22;
        float bx = PW / 2.f, by = PH / 2.f, bvx = 1.2f, bvy = 0.8f;
        int p1y = PH / 2, p2y = PH / 2, sc1 = 0, sc2 = 0;
        SetConsoleTitleA("PONG - W/S=P1  UP/DOWN=P2  ESC=quit");
        LARGE_INTEGER pf, pt, pp; QueryPerformanceFrequency(&pf); QueryPerformanceCounter(&pp);
        while (true) {
            QueryPerformanceCounter(&pt);
            double pdt = ((double)(pt.QuadPart - pp.QuadPart) / pf.QuadPart); pp = pt;
            if (pdt > 0.05)pdt = 0.05;
            // input
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            if (GetAsyncKeyState('W') & 0x8000 && p1y > 1)p1y--;
            if (GetAsyncKeyState('S') & 0x8000 && p1y < PH - 2)p1y++;
            if (GetAsyncKeyState(VK_UP) & 0x8000 && p2y > 1)p2y--;
            if (GetAsyncKeyState(VK_DOWN) & 0x8000 && p2y < PH - 2)p2y++;
            bx += bvx; by += bvy;
            if (by <= 0 || by >= PH - 1)bvy = -bvy;
            if ((int)bx == 2 && abs((int)by - p1y) <= 1) { bvx = fabsf(bvx); bvx *= 1.02f; }
            if ((int)bx == PW - 3 && abs((int)by - p2y) <= 1) { bvx = -fabsf(bvx); bvx *= 1.02f; if (bvx < -3)bvx = -3; }
            if (bx < 0) { sc2++; bx = PW / 2; by = PH / 2; bvx = 1.2f; bvy = 0.8f; }
            if (bx >= PW) { sc1++; bx = PW / 2; by = PH / 2; bvx = -1.2f; bvy = 0.8f; }
            // draw
            for (int y = 0; y < PH; y++) {
                GotoXY(0, y + 1); SetColor(0); std::cout << std::string(PW + 2, ' ');
                GotoXY(0, y + 1); SetColor(COL_WHITE); std::cout << '|';
                GotoXY(PW + 1, y + 1); std::cout << '|';
            }
            for (int yy = -1; yy <= 1; yy++) { int py = p1y + yy; if (py >= 0 && py < PH) { GotoXY(2, py + 1); SetColor(COL_CYAN); std::cout << '|'; } }
            for (int yy = -1; yy <= 1; yy++) { int py = p2y + yy; if (py >= 0 && py < PH) { GotoXY(PW - 1, py + 1); SetColor(COL_YELLOW); std::cout << '|'; } }
            GotoXY((int)bx, (int)by + 1); SetColor(COL_RED); std::cout << 'O';
            GotoXY(0, 0); SetColor(COL_WHITE);
            char sc[64]; sprintf(sc, "PONG  P1:%d  P2:%d  W/S vs UP/DN  ESC=quit", sc1, sc2); std::cout << sc;
            Sleep(16);
        }
        SetConsoleTitleA("Tank Battle - Local Multiplayer"); ClearScreen();
        };

    auto RunBreakout = [&]() {
        ClearScreen();
        const int BW = 50, BH = 20, ROWS = 5, COLS = 12;
        bool bricks[ROWS][COLS]; for (int r = 0; r < ROWS; r++)for (int c = 0; c < COLS; c++)bricks[r][c] = true;
        float bx = BW / 2.f, by = BH - 3.f, bvx = 1.f, bvy = -1.f;
        int padx = BW / 2, padW = 8, lives = 3, bsc = 0;
        int totalBricks = ROWS * COLS;
        SetConsoleTitleA("BREAKOUT - LEFT/RIGHT=paddle  ESC=quit");
        while (lives > 0 && totalBricks > 0) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            if (GetAsyncKeyState(VK_LEFT) & 0x8000 && padx > 1)padx--;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000 && padx < BW - padW - 1)padx++;
            bx += bvx; by += bvy;
            if (bx <= 0 || bx >= BW - 1)bvx = -bvx;
            if (by <= 0)bvy = fabsf(bvy);
            // paddle
            if ((int)by == BH - 3 && (int)bx >= padx && (int)bx <= padx + padW) { bvy = -fabsf(bvy); bvx += (bx - (padx + padW / 2.f)) * 0.1f; }
            if (by >= BH - 1) { lives--; bx = BW / 2; by = BH - 3; bvx = 1; bvy = -1; }
            // brick collision
            int br = (int)(by / 2) - 1, bc = (int)(bx * COLS / BW);
            if (br >= 0 && br < ROWS && bc >= 0 && bc < COLS && bricks[br][bc]) { bricks[br][bc] = false; bvy = -bvy; bsc += 10; totalBricks--; }
            // draw
            ClearScreen();
            GotoXY(0, 0); SetColor(COL_YELLOW); char hdr[64]; sprintf(hdr, "BREAKOUT  Score:%d  Lives:%d  ESC=quit", bsc, lives); std::cout << hdr;
            int colors[] = { COL_RED,COL_MAGENTA,COL_YELLOW,COL_GREEN,COL_CYAN };
            for (int r = 0; r < ROWS; r++)for (int c = 0; c < COLS; c++)if (bricks[r][c]) {
                GotoXY(c * (BW / COLS) + 1, r * 2 + 2); SetColor(colors[r]);
                std::cout << "===";
            }
            GotoXY((int)bx, MAP_TOP + (int)by); SetColor(COL_WHITE); std::cout << 'O';
            for (int px = padx; px < padx + padW; px++) { GotoXY(px, MAP_TOP + BH - 2); SetColor(COL_CYAN); std::cout << '='; }
            Sleep(30);
        }
        ClearScreen(); GotoXY(2, 4); SetColor(COL_YELLOW);
        std::cout << (totalBricks == 0 ? "YOU WIN! " : "GAME OVER  ") << "Score:" << bsc;
        GotoXY(2, 6); SetColor(COL_WHITE); std::cout << "Press any key..."; (void)_getch();
        SetConsoleTitleA("Tank Battle - Local Multiplayer");
        };

    auto RunTetris = [&]() {
        ClearScreen();
        const int TW = 10, TH = 20;
        int board[TH][TW] = {};
        // 7 tetrominoes (each is 4 cells relative to pivot)
        int pieces[7][4][2] = {
            {{0,0},{1,0},{-1,0},{2,0}},  // I
            {{0,0},{1,0},{0,-1},{1,-1}}, // O
            {{0,0},{-1,0},{1,0},{0,-1}}, // T
            {{0,0},{-1,0},{0,-1},{-1,-1 + 1}}, // S (approx)
            {{0,0},{1,0},{0,-1},{1,-1 - 1 + 1}}, // Z
            {{0,0},{-1,0},{1,0},{1,-1}}, // L
            {{0,0},{-1,0},{1,0},{-1,-1}},// J
        };
        int pieceColors[] = { COL_CYAN,COL_YELLOW,COL_MAGENTA,COL_GREEN,COL_RED,COL_ORANGE,COL_BLUE };
        int cur = rand() % 7, cx = TW / 2, cy = 2, rot = 0;
        int tScore = 0; double fall = 0, fallRate = 0.5;
        LARGE_INTEGER tf, tt2, tp2; QueryPerformanceFrequency(&tf); QueryPerformanceCounter(&tp2);
        auto canPlace = [&](int px, int py, int pc)->bool {
            for (int i = 0; i < 4; i++) { int nx = px + pieces[pc][i][0], ny = py + pieces[pc][i][1]; if (nx < 0 || nx >= TW || ny >= TH)return false; if (ny >= 0 && board[ny][nx])return false; }return true;
            };
        auto lockPiece = [&]() {
            for (int i = 0; i < 4; i++) { int nx = cx + pieces[cur][i][0], ny = cy + pieces[cur][i][1]; if (ny >= 0 && ny < TH && nx >= 0 && nx < TW)board[ny][nx] = pieceColors[cur] + 1; }
            // clear lines
            for (int y = TH - 1; y >= 0; y--) { bool full = true; for (int x = 0; x < TW; x++)if (!board[y][x]) { full = false; break; }if (full) { for (int yy = y; yy > 0; yy--)for (int xx = 0; xx < TW; xx++)board[yy][xx] = board[yy - 1][xx]; tScore += 100; y++; } }
            cur = rand() % 7; cx = TW / 2; cy = 2;
            if (!canPlace(cx, cy, cur)) { ClearScreen(); GotoXY(4, 8); SetColor(COL_RED); std::cout << "GAME OVER Score:" << tScore; GotoXY(4, 10); SetColor(COL_WHITE); std::cout << "Press any key"; (void)_getch(); cy = -99; }
            };
        SetConsoleTitleA("TETRIS - A/D=move  W=rotate  S=drop  ESC=quit");
        while (cy != -99) {
            QueryPerformanceCounter(&tt2); double tdt = ((double)(tt2.QuadPart - tp2.QuadPart) / tf.QuadPart); tp2 = tt2;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            static double kd = 0; kd -= tdt;
            if (kd <= 0) {
                kd = 0.12;
                if (GetAsyncKeyState('A') & 0x8000 && canPlace(cx - 1, cy, cur))cx--;
                if (GetAsyncKeyState('D') & 0x8000 && canPlace(cx + 1, cy, cur))cx++;
                if (GetAsyncKeyState('S') & 0x8000 && canPlace(cx, cy + 1, cur))cy++;
            }
            if (GetAsyncKeyState('W') & 0x8000) { Sleep(100); } // placeholder rotate
            fall += tdt; if (fall >= fallRate) { fall = 0; if (canPlace(cx, cy + 1, cur))cy++; else lockPiece(); }
            // draw board
            int ox = 2, oy = 1;
            for (int y = 0; y < TH; y++) { GotoXY(ox, oy + y); SetColor(COL_DARK_GRAY); std::cout << '|'; for (int x = 0; x < TW; x++) { int c = board[y][x]; if (c) { SetColor(c - 1); std::cout << '#'; } else { SetColor(0); std::cout << ' '; } }SetColor(COL_DARK_GRAY); std::cout << '|'; }
            GotoXY(ox, oy + TH); SetColor(COL_DARK_GRAY); std::cout << std::string(TW + 2, '-');
            // draw piece
            for (int i = 0; i < 4; i++) { int px = cx + pieces[cur][i][0], py = cy + pieces[cur][i][1]; if (py >= 0) { GotoXY(ox + 1 + px, oy + py); SetColor(pieceColors[cur]); std::cout << '#'; } }
            GotoXY(TW + 6, oy + 2); SetColor(COL_YELLOW); char ts[32]; sprintf(ts, "Score:%d", tScore); std::cout << ts;
            GotoXY(TW + 6, oy + 4); SetColor(COL_WHITE); std::cout << "A/D=move";
            GotoXY(TW + 6, oy + 5); std::cout << "S=drop  ";
            GotoXY(TW + 6, oy + 6); std::cout << "ESC=quit";
            GotoXY(0, 0); SetColor(COL_YELLOW); std::cout << "TETRIS  Score:" << tScore << "  ";
            Sleep(16);
        }
        SetConsoleTitleA("Tank Battle - Local Multiplayer"); ClearScreen();
        };

    auto RunSpaceInvaders = [&]() {
        ClearScreen();
        const int SW2 = 50, SH2 = 22;
        struct Inv { int x, y; bool alive; };
        std::vector<Inv> invaders;
        for (int r = 0; r < 4; r++)for (int c = 0; c < 10; c++)invaders.push_back({ c * 4 + 5,r * 2 + 3,true });
        int shipX = SW2 / 2, siScore = 0, siLives = 3;
        struct SBullet { float x, y; bool active; };
        std::vector<SBullet> sbullets;
        double invDir = 0.3, invTimer = 0;
        int invMoveDir = 1;
        SetConsoleTitleA("SPACE INVADERS - LEFT/RIGHT=move  SPACE=shoot  ESC=quit");
        LARGE_INTEGER sif, sit, sip; QueryPerformanceFrequency(&sif); QueryPerformanceCounter(&sip);
        double shootCool = 0;
        while (siLives > 0) {
            QueryPerformanceCounter(&sit); double sdt = ((double)(sit.QuadPart - sip.QuadPart) / sif.QuadPart); sip = sit;
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)break;
            if (GetAsyncKeyState(VK_LEFT) & 0x8000 && shipX > 1)shipX--;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000 && shipX < SW2 - 2)shipX++;
            shootCool -= sdt;
            if (GetAsyncKeyState(VK_SPACE) & 0x8000 && shootCool <= 0) { sbullets.push_back({ (float)shipX,(float)(SH2 - 4),true }); shootCool = 0.4; }
            // move bullets
            for (auto& b : sbullets) { if (b.active) { b.y -= 12.f * (float)sdt; if (b.y < 1)b.active = false; } }
            // move invaders
            invTimer -= sdt;
            if (invTimer <= 0) {
                invTimer = invDir;
                bool hitEdge = false;
                for (auto& inv : invaders)if (inv.alive) { inv.x += invMoveDir; if (inv.x <= 0 || inv.x >= SW2 - 1)hitEdge = true; }
                if (hitEdge) { invMoveDir = -invMoveDir; for (auto& inv : invaders)if (inv.alive)inv.y++; invDir = max(0.05, invDir * 0.95); }
            }
            // collision
            for (auto& b : sbullets) { if (!b.active)continue; for (auto& inv : invaders) { if (inv.alive && (int)b.x == inv.x && abs((int)b.y - inv.y) <= 1) { inv.alive = false; b.active = false; siScore += 10; } } }
            // invader reach bottom
            for (auto& inv : invaders)if (inv.alive && inv.y >= SH2 - 3) { siLives = 0; }
            // check win
            bool anyAlive = false; for (auto& inv : invaders)if (inv.alive)anyAlive = true;
            if (!anyAlive) { ClearScreen(); GotoXY(4, 8); SetColor(COL_YELLOW); std::cout << "YOU WIN! Score:" << siScore; GotoXY(4, 10); std::cout << "Press any key"; (void)_getch(); break; }
            // draw
            ClearScreen();
            GotoXY(0, 0); SetColor(COL_YELLOW); char si_hdr[64]; sprintf(si_hdr, "SPACE INVADERS  Score:%d  Lives:%d", siScore, siLives); std::cout << si_hdr;
            for (auto& inv : invaders)if (inv.alive) { GotoXY(inv.x, inv.y); SetColor(COL_GREEN); std::cout << 'W'; }
            for (auto& b : sbullets)if (b.active) { GotoXY((int)b.x, (int)b.y); SetColor(COL_WHITE); std::cout << '|'; }
            GotoXY(shipX, SH2 - 2); SetColor(COL_CYAN); std::cout << '^';
            Sleep(16);
        }
        if (siLives == 0) { ClearScreen(); GotoXY(4, 8); SetColor(COL_RED); std::cout << "GAME OVER! Score:" << siScore; GotoXY(4, 10); SetColor(COL_WHITE); std::cout << "Press any key..."; (void)_getch(); }
        SetConsoleTitleA("Tank Battle - Local Multiplayer"); ClearScreen();
        };

    while (true) {
        // ---- Step 1: Player count ----
        DrawTitle();
        // H = show publisher/trust help at any time from lobby
        GotoXY(2, 28); SetColor(COL_DARK_GRAY);
        std::cout << "  [H] How to run if Windows blocks this game";
        ResetColor();
        std::vector<std::string> playerOpts = {
            "1 Player (vs 3 AI bots)",
            "2 Players",
            "3 Players",
            "4 Players"
        };
        int pc = Menu("Select Number of Players  [UP/DOWN + ENTER]  Q=more:", playerOpts, 2, 13);
        if (pc == -3) { ShowPublisherHelp(); continue; }
        if (pc == -2) {
            // Q was pressed - show bonus games selector
            ClearScreen();
            const char* _mn[] = { "Snake","Pong","Breakout","Tetris","Space Invaders" };
            GotoXY(2, 2); SetColor(COL_YELLOW); std::cout << "  --- Bonus Games ---";
            for (int _mi = 0; _mi < 5; _mi++) { GotoXY(2, 4 + _mi); SetColor(COL_WHITE); std::cout << "  " << (_mi + 1) << ". " << _mn[_mi]; }
            GotoXY(2, 10); SetColor(COL_DARK_GRAY); std::cout << "  ESC = back to menu";
            ResetColor();
            int arcadeChoice = _getch();
            if (arcadeChoice == '1') RunSnake();
            else if (arcadeChoice == '2') RunPong();
            else if (arcadeChoice == '3') RunBreakout();
            else if (arcadeChoice == '4') RunTetris();
            else if (arcadeChoice == '5') RunSpaceInvaders();
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
