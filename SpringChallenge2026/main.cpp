// Spring Challenge 2026 — bot
// Layout:
//   Layer 1: Enums & constants
//   Layer 2: Game state (POD structs)
//   Layer 3: I/O (parsing + action serialization)
//   Layer 4: Pathfinding (added later)
//   Layer 5: Strategy (interchangeable brain)
//   Layer 6: Main loop

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <sstream>
#include <chrono>
#include <functional>
#include <queue>

using namespace std;

// Global silence flag: when true, DBG output is suppressed. Used by
// SearchStrategy's continuation roll-outs so we don't flood stderr.
static bool g_silent_dbg = false;
#define DBG(x) do { if (!g_silent_dbg) { cerr << x << endl; } } while (0)

// =============================================================
// Layer 1 — Enums & constants
// =============================================================

enum class Terrain : uint8_t {
    GRASS,
    SHACK_OWN,
    SHACK_OPP,
    WATER,
    ROCK,
    IRON_ORE,   // mined via MINE when adjacent; not walkable.
};

enum class FruitType : uint8_t {
    PLUM = 0, LEMON = 1, APPLE = 2, BANANA = 3,
};

// Resource = fruit + raw materials (iron, wood) carried/stored in inventory.
enum class ResourceType : uint8_t {
    PLUM = 0, LEMON = 1, APPLE = 2, BANANA = 3, IRON = 4, WOOD = 5,
};

[[maybe_unused]] constexpr int FRUIT_COUNT = 4;
constexpr int RES_COUNT = 6;

enum class ActionKind : uint8_t {
    MOVE, HARVEST, DROP, WAIT, MSG,
    PLANT, PICK, TRAIN,
    CHOP, MINE,
};

static const char* fruitName(FruitType f) {
    switch (f) {
        case FruitType::PLUM:  return "PLUM";
        case FruitType::LEMON: return "LEMON";
        case FruitType::APPLE: return "APPLE";
        case FruitType::BANANA:return "BANANA";
    }
    return "PLUM";
}

static FruitType parseFruit(const string& s) {
    if (s == "PLUM")   return FruitType::PLUM;
    if (s == "LEMON")  return FruitType::LEMON;
    if (s == "APPLE")  return FruitType::APPLE;
    return FruitType::BANANA;
}

// =============================================================
// Layer 2 — Game state
// =============================================================

struct Inventory {
    array<int, RES_COUNT> r{};
    int& operator[](ResourceType t)       { return r[(int)t]; }
    int  operator[](ResourceType t) const { return r[(int)t]; }
    int total()       const { int s=0; for (int v : r) s += v; return s; }
    int totalFruits() const { return r[0] + r[1] + r[2] + r[3]; }
};

struct Tree {
    FruitType type;
    int x, y;
    int size;
    int health;
    int fruits;
    int cooldown;
};

struct Troll {
    int id;
    int owner;       // 0 = me, 1 = opponent
    int x, y;
    int movSpeed;
    int carryCap;
    int harvest;
    int chop;
    Inventory carry;
    int carriedTotal() const { return carry.total(); }
    int carriedFree(const Inventory& cap_unused) const { return carryCap - carry.total(); }
};

struct GridMap {
    int w = 0, h = 0;
    vector<Terrain> cells;
    Terrain at(int x, int y) const { return cells[y*w + x]; }
    void    set(int x, int y, Terrain t) { cells[y*w + x] = t; }
    bool    inBounds(int x, int y) const { return x>=0 && x<w && y>=0 && y<h; }
};

struct GameState {
    int turn = 0;
    GridMap map;
    Inventory shack[2];          // [0] = me, [1] = opp
    int shackPos[2][2] = {{-1,-1},{-1,-1}};   // [player][0=x,1=y]
    vector<Tree>  trees;
    vector<Troll> trolls;
};

// =============================================================
// Layer 3 — I/O
// =============================================================

struct Action {
    ActionKind kind = ActionKind::WAIT;
    int trollId = -1;
    int x = 0, y = 0;
    FruitType fruit = FruitType::PLUM;
    int trainAttrs[4] = {0, 0, 0, 0};   // move, carry, harvest, chop
    string msg;

    static Action Move(int id, int x, int y)     { Action a; a.kind=ActionKind::MOVE;    a.trollId=id; a.x=x; a.y=y; return a; }
    static Action Harvest(int id)                { Action a; a.kind=ActionKind::HARVEST; a.trollId=id; return a; }
    static Action Drop(int id)                   { Action a; a.kind=ActionKind::DROP;    a.trollId=id; return a; }
    static Action Wait()                         { Action a; a.kind=ActionKind::WAIT;    return a; }
    static Action Msg(const string& m)           { Action a; a.kind=ActionKind::MSG;     a.msg=m; return a; }
    static Action Plant(int id, FruitType f)     { Action a; a.kind=ActionKind::PLANT;   a.trollId=id; a.fruit=f; return a; }
    static Action Pick(int id, FruitType f)      { Action a; a.kind=ActionKind::PICK;    a.trollId=id; a.fruit=f; return a; }
    static Action Chop(int id)                   { Action a; a.kind=ActionKind::CHOP;    a.trollId=id; return a; }
    static Action Mine(int id)                   { Action a; a.kind=ActionKind::MINE;    a.trollId=id; return a; }
    static Action Train(int mv, int cap, int hv, int ch) {
        Action a; a.kind=ActionKind::TRAIN;
        a.trainAttrs[0]=mv; a.trainAttrs[1]=cap; a.trainAttrs[2]=hv; a.trainAttrs[3]=ch;
        return a;
    }

    string toString() const {
        ostringstream os;
        switch (kind) {
            case ActionKind::MOVE:    os << "MOVE "    << trollId << ' ' << x << ' ' << y; break;
            case ActionKind::HARVEST: os << "HARVEST " << trollId; break;
            case ActionKind::DROP:    os << "DROP "    << trollId; break;
            case ActionKind::WAIT:    os << "WAIT"; break;
            case ActionKind::MSG:     os << "MSG "     << msg; break;
            case ActionKind::PLANT:   os << "PLANT "   << trollId << ' ' << fruitName(fruit); break;
            case ActionKind::PICK:    os << "PICK "    << trollId << ' ' << fruitName(fruit); break;
            case ActionKind::CHOP:    os << "CHOP "    << trollId; break;
            case ActionKind::MINE:    os << "MINE "    << trollId; break;
            case ActionKind::TRAIN:   os << "TRAIN "
                                         << trainAttrs[0] << ' ' << trainAttrs[1] << ' '
                                         << trainAttrs[2] << ' ' << trainAttrs[3]; break;
        }
        return os.str();
    }
};

static void readInitial(GameState& gs) {
    cin >> gs.map.w >> gs.map.h; cin.ignore();
    gs.map.cells.assign(gs.map.w * gs.map.h, Terrain::GRASS);
    for (int y = 0; y < gs.map.h; y++) {
        string line;
        getline(cin, line);
        for (int x = 0; x < gs.map.w; x++) {
            char c = line[x];
            Terrain t = Terrain::GRASS;
            switch (c) {
                case '.': t = Terrain::GRASS; break;
                case '~': t = Terrain::WATER; break;
                case '#': t = Terrain::ROCK; break;
                case '+': t = Terrain::IRON_ORE; break;
                case '0': t = Terrain::SHACK_OWN; gs.shackPos[0][0]=x; gs.shackPos[0][1]=y; break;
                case '1': t = Terrain::SHACK_OPP; gs.shackPos[1][0]=x; gs.shackPos[1][1]=y; break;
                default:  t = Terrain::GRASS; break;  // unknown → treat as grass
            }
            gs.map.set(x, y, t);
        }
    }
    DBG("init: map " << gs.map.w << "x" << gs.map.h
        << " my_shack=(" << gs.shackPos[0][0] << "," << gs.shackPos[0][1] << ")"
        << " opp_shack=(" << gs.shackPos[1][0] << "," << gs.shackPos[1][1] << ")");
}

static void readTurn(GameState& gs) {
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < RES_COUNT; i++) cin >> gs.shack[p].r[i];
        cin.ignore();
    }

    int treeCount; cin >> treeCount; cin.ignore();
    gs.trees.clear();
    gs.trees.reserve(treeCount);
    for (int i = 0; i < treeCount; i++) {
        string ts; Tree t;
        cin >> ts >> t.x >> t.y >> t.size >> t.health >> t.fruits >> t.cooldown;
        cin.ignore();
        t.type = parseFruit(ts);
        gs.trees.push_back(t);
    }

    int trollCount; cin >> trollCount; cin.ignore();
    gs.trolls.clear();
    gs.trolls.reserve(trollCount);
    for (int i = 0; i < trollCount; i++) {
        Troll t;
        cin >> t.id >> t.owner >> t.x >> t.y
            >> t.movSpeed >> t.carryCap >> t.harvest >> t.chop
            >> t.carry.r[0] >> t.carry.r[1] >> t.carry.r[2] >> t.carry.r[3]
            >> t.carry.r[4] >> t.carry.r[5];
        cin.ignore();
        gs.trolls.push_back(t);
    }
}

static void emit(const vector<Action>& actions) {
    if (actions.empty()) { cout << "WAIT" << '\n' << flush; return; }
    for (size_t i = 0; i < actions.size(); i++) {
        if (i) cout << ';';
        cout << actions[i].toString();
    }
    cout << '\n' << flush;
}

// =============================================================
// Layer 4 — Pathfinding
// =============================================================

struct PathInfo {
    int w = 0, h = 0;
    vector<int> dist;   // -1 = unreachable
    int at(int x, int y) const { return dist[y*w + x]; }
};

// 4-connected BFS. Walkable predicate decides which cells can be entered.
// The source cell is always reachable (dist=0) even if not walkable, so
// trolls spawned on a shack can BFS outward correctly.
static PathInfo bfs(const GridMap& m, int sx, int sy,
                    const function<bool(int,int)>& walkable) {
    PathInfo p; p.w = m.w; p.h = m.h;
    p.dist.assign(m.w * m.h, -1);
    if (!m.inBounds(sx, sy)) return p;

    p.dist[sy*m.w + sx] = 0;
    queue<pair<int,int>> q;
    q.push({sx, sy});

    static constexpr int DX[4] = {1,-1,0,0};
    static constexpr int DY[4] = {0,0,1,-1};

    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        int cd = p.dist[cy*m.w + cx];
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (!m.inBounds(nx, ny)) continue;
            int& nd = p.dist[ny*m.w + nx];
            if (nd != -1) continue;
            if (!walkable(nx, ny)) continue;
            nd = cd + 1;
            q.push({nx, ny});
        }
    }
    return p;
}

// Plain grass-only walkability. Useful as a base predicate for strategies or
// pathfinding tests that don't care about dynamic blockers (trolls, reserved
// cells). Kept for forward-compatibility.
[[maybe_unused]] static auto defaultWalkable(const GridMap& m) {
    return [&m](int x, int y) {
        return m.at(x, y) == Terrain::GRASS;
    };
}

static int manhattan(int ax, int ay, int bx, int by) {
    return abs(ax - bx) + abs(ay - by);
}

// =============================================================
// Layer 4b — Static lookup tables (Phase B1)
// =============================================================
//
// Precomputed once at game init from the static map (terrain only,
// trees/trolls excluded — they're dynamic state). Used by future
// macro-action generation and MCTS heuristic to avoid running BFS
// inside hot loops.
//
// FarmStrategy still uses its own dynamic-predicate BFS for movement
// because its walkable check includes ally blockers and reservations,
// which these static tables can't represent. That's intentional —
// these tables answer "is X reachable from Y on an empty board" only.

constexpr int MAX_MAP_W = 22;
constexpr int MAX_MAP_H = 11;

// bfs_dist_lookup[sy][sx][ty][tx] = BFS distance (Manhattan-respecting,
// 4-connected) on grass + shack cells. -1 = unreachable.
// Size: 22*11*22*11*sizeof(int) = ~234 KB. Fits comfortably.
[[maybe_unused]] static int bfs_dist_lookup[MAX_MAP_H][MAX_MAP_W][MAX_MAP_H][MAX_MAP_W];

// nextStepMask[sy][sx][ty][tx] = bitmask of cardinal directions that step
// from (sx,sy) to a cell exactly 1-closer to (tx,ty). Direction encoding
// matches STEP_DXS/DYS below: bit 0=+x, 1=-x, 2=+y, 3=-y. Lets a future
// moveToward walk greedily in O(speed) instead of re-running BFS.
[[maybe_unused]] static uint8_t nextStepMask[MAX_MAP_H][MAX_MAP_W][MAX_MAP_H][MAX_MAP_W];
[[maybe_unused]] static constexpr int STEP_DXS[4] = {1, -1, 0, 0};
[[maybe_unused]] static constexpr int STEP_DYS[4] = {0, 0, 1, -1};

// Per-cell "is 4-adjacent to a water tile" flag.
[[maybe_unused]] static bool nearWaterLookup[MAX_MAP_H][MAX_MAP_W];

// Per-player caches keyed off the shack position. Shacks never move.
// [0] = us, [1] = opp.
[[maybe_unused]] static vector<pair<int,int>> shackAdjCells[2];             // walkable cells 4-adj to shack
[[maybe_unused]] static vector<pair<int,int>> plantableCellsAroundShack[2]; // walkable cells with BFS dist ≤ 2 of shack
[[maybe_unused]] static vector<pair<int,int>> mineAdjCells;                 // walkable cells 4-adj to ANY iron mine
[[maybe_unused]] static bool g_lookupsReady = false;

// Build bfs_dist_lookup + nextStepMask. Walkability: GRASS and SHACK_OWN
// /SHACK_OPP cells. Trees + iron + water + rock are non-walkable for
// distance purposes (trees are dynamic; for the rare case of walking
// onto a planted tree the macro layer will fall back to per-cell BFS).
[[maybe_unused]] static void buildBfsLookup(const GridMap& m) {
    auto walkable = [&](int x, int y) -> bool {
        Terrain c = m.at(x, y);
        return c == Terrain::GRASS
            || c == Terrain::SHACK_OWN
            || c == Terrain::SHACK_OPP;
    };

    // Initialize to -1.
    for (int sy = 0; sy < m.h; sy++)
        for (int sx = 0; sx < m.w; sx++)
            for (int ty = 0; ty < m.h; ty++)
                for (int tx = 0; tx < m.w; tx++)
                    bfs_dist_lookup[sy][sx][ty][tx] = -1;

    // BFS from every walkable source cell.
    static constexpr int DX[4] = {1, -1, 0, 0};
    static constexpr int DY[4] = {0, 0, 1, -1};
    for (int sy = 0; sy < m.h; sy++) {
        for (int sx = 0; sx < m.w; sx++) {
            if (!walkable(sx, sy)) continue;
            bfs_dist_lookup[sy][sx][sy][sx] = 0;
            queue<pair<int,int>> q;
            q.push({sx, sy});
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                int cd = bfs_dist_lookup[sy][sx][cy][cx];
                for (int k = 0; k < 4; k++) {
                    int nx = cx + DX[k], ny = cy + DY[k];
                    if (!m.inBounds(nx, ny)) continue;
                    if (!walkable(nx, ny)) continue;
                    if (bfs_dist_lookup[sy][sx][ny][nx] != -1) continue;
                    bfs_dist_lookup[sy][sx][ny][nx] = cd + 1;
                    q.push({nx, ny});
                }
            }
        }
    }

    // Derive nextStepMask from the completed distance table.
    for (int sy = 0; sy < m.h; sy++) {
        for (int sx = 0; sx < m.w; sx++) {
            for (int ty = 0; ty < m.h; ty++) {
                for (int tx = 0; tx < m.w; tx++) {
                    uint8_t mask = 0;
                    int d = bfs_dist_lookup[sy][sx][ty][tx];
                    if (d > 0) {
                        for (int k = 0; k < 4; k++) {
                            int nx = sx + STEP_DXS[k], ny = sy + STEP_DYS[k];
                            if (!m.inBounds(nx, ny)) continue;
                            if (!walkable(nx, ny)) continue;
                            if (bfs_dist_lookup[ny][nx][ty][tx] == d - 1)
                                mask |= (uint8_t)(1 << k);
                        }
                    }
                    nextStepMask[sy][sx][ty][tx] = mask;
                }
            }
        }
    }
}

// Per-cell water-adjacency. Once cached, isWaterAdjacent can be a O(1)
// lookup instead of 4 cell reads on every harvest/cooldown computation.
[[maybe_unused]] static void buildNearWaterLookup(const GridMap& m) {
    static constexpr int DX[4] = {1, -1, 0, 0};
    static constexpr int DY[4] = {0, 0, 1, -1};
    for (int y = 0; y < m.h; y++) {
        for (int x = 0; x < m.w; x++) {
            bool near = false;
            for (int k = 0; k < 4; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (!m.inBounds(nx, ny)) continue;
                if (m.at(nx, ny) == Terrain::WATER) { near = true; break; }
            }
            nearWaterLookup[y][x] = near;
        }
    }
}

// Walkable cells adjacent to each iron-ore tile (dedup across mines).
// These are the "MINE source" cells — a troll must be 4-adj to ore.
[[maybe_unused]] static void buildMineAdjCells(const GridMap& m) {
    mineAdjCells.clear();
    static constexpr int DX[4] = {1, -1, 0, 0};
    static constexpr int DY[4] = {0, 0, 1, -1};
    vector<vector<bool>> seen(m.h, vector<bool>(m.w, false));
    for (int y = 0; y < m.h; y++) {
        for (int x = 0; x < m.w; x++) {
            if (m.at(x, y) != Terrain::IRON_ORE) continue;
            for (int k = 0; k < 4; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (!m.inBounds(nx, ny)) continue;
                if (seen[ny][nx]) continue;
                if (m.at(nx, ny) != Terrain::GRASS) continue;
                seen[ny][nx] = true;
                mineAdjCells.push_back({nx, ny});
            }
        }
    }
}

// Per-player shack-adjacent walkable cells + plantable cells (BFS dist ≤ 2).
// Requires bfs_dist_lookup already built.
[[maybe_unused]] static void buildShackCaches(const GridMap& m,
                                              int myX, int myY,
                                              int oppX, int oppY) {
    static constexpr int DX[4] = {1, -1, 0, 0};
    static constexpr int DY[4] = {0, 0, 1, -1};
    int sx[2] = {myX, oppX};
    int sy[2] = {myY, oppY};
    for (int p = 0; p < 2; p++) {
        shackAdjCells[p].clear();
        plantableCellsAroundShack[p].clear();
        // Direct neighbours of the shack cell that are walkable grass.
        for (int k = 0; k < 4; k++) {
            int nx = sx[p] + DX[k], ny = sy[p] + DY[k];
            if (!m.inBounds(nx, ny)) continue;
            if (m.at(nx, ny) != Terrain::GRASS) continue;
            shackAdjCells[p].push_back({nx, ny});
        }
        // All grass cells with BFS dist ≤ 2 of the shack.
        for (int y = 0; y < m.h; y++) {
            for (int x = 0; x < m.w; x++) {
                if (m.at(x, y) != Terrain::GRASS) continue;
                int d = bfs_dist_lookup[sy[p]][sx[p]][y][x];
                if (d < 0 || d > 2) continue;
                plantableCellsAroundShack[p].push_back({x, y});
            }
        }
    }
}

// Top-level builder. Call after readInitial AND once shacks are known
// (first readTurn finishes parsing). Idempotent — re-calling is cheap
// but the lookups don't change so it's wasteful.
[[maybe_unused]] static void buildStaticLookups(const GridMap& m,
                                                int myShackX, int myShackY,
                                                int oppShackX, int oppShackY) {
    buildBfsLookup(m);
    buildNearWaterLookup(m);
    buildMineAdjCells(m);
    buildShackCaches(m, myShackX, myShackY, oppShackX, oppShackY);
    g_lookupsReady = true;
}

// =============================================================
// Layer 5 — Strategy
// =============================================================

struct IStrategy {
    virtual vector<Action> decide(const GameState&) = 0;
    virtual ~IStrategy() = default;
};

// FarmStrategy — implements the "build a small farm next to shack and
// stay there" pattern observed in tsukammo's (rank 1) arena play.
//
// Phase machine (game-wide):
//   SETUP_PLANT   farm is missing one of P/L/A/B within FARM_RADIUS
//                  → PICK from shack, walk near shack, PLANT
//   SETUP_CHOP    farm has all 4 types planted but no fruits ready yet
//                  → all trolls chop the 2 nearest non-farm trees for wood
//                    while waiting for the farm to mature
//   HARVEST       farm has any tree with fruits
//                  → harvest farm trees → drop → repeat. Never chop farm.
//   WINDDOWN      turn > 240 → drop carry, stop planting/chopping
//
// Per-troll priority order:
//   1. PLANT   on near-shack grass with a fruit type missing from farm
//   2. HARVEST on a farm tree with fruits and free cap
//   3. CHOP    on a non-farm tree with health (not WINDDOWN)
//   4. DROP    adj-shack with any carry
//   5. PICK    adj-shack, empty hands, farm missing a fruit type in shack
//   6. MOVE    toward the phase-appropriate target

enum class FarmPhase : uint8_t { SETUP_PLANT, SETUP_CHOP, HARVEST, WINDDOWN };

static const char* phaseName(FarmPhase p) {
    switch (p) {
        case FarmPhase::SETUP_PLANT: return "SETUP_PLANT";
        case FarmPhase::SETUP_CHOP:  return "SETUP_CHOP";
        case FarmPhase::HARVEST:     return "HARVEST";
        case FarmPhase::WINDDOWN:    return "WINDDOWN";
    }
    return "?";
}

// Forward decls — defined inside the Layer 7 (Simulator) block below.
static int  treeIndexAt(const GameState& gs, int x, int y);

struct FarmStrategy : IStrategy {
    // The farm is the cluster of trees we keep next to our shack.
    // Anything within FARM_RADIUS (Manhattan) of the shack counts as
    // "farm tree" and is *never* chopped — only harvested.
    //
    // Radius 2 (not 3) was a deliberate cut after seeing arena play:
    // we moved 555 times vs tsukammo's 260 for the same productive
    // output. tsukammo's farm fits in a 2×2 box; planting beyond
    // that just means more travel per harvest cycle. Plant priority
    // is biased toward Manhattan-distance-1 cells first so the four
    // orthogonal neighbours of the shack get filled before anything
    // at distance 2.
    static constexpr int FARM_RADIUS    = 2;
    static constexpr int kTrollCap      = 4;
    static constexpr int kWinddownTurn  = 240;

    // Tier table for TRAIN. Ordered strongest → weakest by priority.
    struct Tier { int mv, cap, hv, ch; const char* tag; };
    // Indices into the tiers[] array below. Update both when adding tiers.
    static constexpr int IDX_C = 6;
    static constexpr int IDX_F = 9;

    vector<Action> decide(const GameState& gs) override {
        const int W  = gs.map.w, H = gs.map.h;
        const int sx = gs.shackPos[0][0], sy = gs.shackPos[0][1];

        int myTrollCount = 0;
        for (const auto& t : gs.trolls) if (t.owner == 0) myTrollCount++;

        // ── Inventory shorthand ──
        const int havePlum  = gs.shack[0][ResourceType::PLUM];
        const int haveLemon = gs.shack[0][ResourceType::LEMON];
        const int haveApple = gs.shack[0][ResourceType::APPLE];
        const int haveBan   = gs.shack[0][ResourceType::BANANA];
        const int haveIron  = gs.shack[0][ResourceType::IRON];
        const int haveWood  = gs.shack[0][ResourceType::WOOD];

        // ── Identify farm trees (within FARM_RADIUS of shack) ──
        vector<int> farmTreeIdx;
        bool farmHas[FRUIT_COUNT] = {false, false, false, false};
        int  farmCount[FRUIT_COUNT] = {0, 0, 0, 0};
        bool farmHasFruit = false;
        for (size_t i = 0; i < gs.trees.size(); i++) {
            const auto& tr = gs.trees[i];
            int d = manhattan(tr.x, tr.y, sx, sy);
            if (d <= FARM_RADIUS) {
                farmTreeIdx.push_back((int)i);
                farmHas[(int)tr.type] = true;
                farmCount[(int)tr.type]++;
                if (tr.fruits > 0) farmHasFruit = true;
            }
        }

        // Pre-compute: is there any FREE near-shack grass cell where we
        // could plant ANOTHER tree? This is the safety check that broke
        // the last aggression attempt — without it, trolls PICK fruit
        // hoping to plant, but find no slot and get stuck holding it.
        auto hasFreePlantSlot = [&]() -> bool {
            for (int yy = 0; yy < gs.map.h; yy++) {
                for (int xx = 0; xx < gs.map.w; xx++) {
                    int d = manhattan(xx, yy, sx, sy);
                    if (d < 1 || d > FARM_RADIUS) continue;
                    if (gs.map.at(xx, yy) != Terrain::GRASS) continue;
                    if (treeIndexAt(gs, xx, yy) >= 0) continue;
                    return true;
                }
            }
            return false;
        };

        // Aggressive parametric replant. Plant another tree of type f when:
        //   - Farm has NONE of that type (critical, must replant), OR
        //   - Farm has 1 tree AND it's BANANA AND a slot exists AND
        //     shack has it. Banana has no train cost — pure chop fodder
        //     for wood points. Limiting redundancy to banana mirrors
        //     tsukammo (18 banana plants out of 29 total) and avoids
        //     draining plum/lemon/apple that we need for training.
        const bool freeSlot = hasFreePlantSlot();
        auto wantPlantMore = [&](int f) -> bool {
            // freeSlot REQUIRED even for urgent replant. Without this
            // guard, the bot picks a fruit it cannot plant anywhere
            // (no reachable free grass slot) and enters a PICK→DROP
            // loop at adj-shack: silver_logs showed troll 0 stuck in
            // this loop for ~180 turns with banana.
            //
            // Redundancy is now LEMON (was BANANA). Boss-game replay
            // (22x11 map) showed lemon stuck at L=7 for 100+ turns
            // because 1 lemon farm tree can't keep up with training
            // demand (tier B/C cost L=5-6; tier S1/S2 needs L=12). A
            // 2nd lemon tree doubles supply rate, unlocking cap=3
            // tiers (S1 `1322` / S2 `1312`) mid-game. Cap=3 wood-
            // haulers are required to keep pace with Boss's cap=4
            // trolls (Boss does 8 trips × 4 wood = 32 wood from cap=4
            // alone; we maxed out at cap=2 → 2 wood per trip).
            if (!freeSlot) return false;
            if (farmCount[f] >= 2) return false;            // saturated
            if (farmCount[f] == 0) return true;             // urgent replant
            if (f != (int)FruitType::LEMON) return false;   // only lemon for redundancy
            // Removed `shack[f] > 0` check — P5 PICK separately verifies
            // shack stock before picking. Keeping it here caused a flip-
            // flop bug: after PICK, shack[f] becomes 0, so DROP-refusal's
            // wantPlantMore returns false on the very next turn → the
            // troll drops the just-picked fruit back. Infinite PICK→DROP
            // loop. P5 already gates picking; once carried, the bot
            // should commit to planting.
            return true;
        };

        // ── Determine game phase ──
        FarmPhase phase;
        if (gs.turn > kWinddownTurn) {
            phase = FarmPhase::WINDDOWN;
        } else if (!(farmHas[0] && farmHas[1] && farmHas[2] && farmHas[3])) {
            phase = FarmPhase::SETUP_PLANT;
        } else if (!farmHasFruit) {
            phase = FarmPhase::SETUP_CHOP;
        } else {
            phase = FarmPhase::HARVEST;
        }

        // ── Per-turn DBG line ──
        DBG("T" << gs.turn << " " << phaseName(phase)
            << " trolls=" << myTrollCount
            << " farmPLAB=" << farmHas[0] << farmHas[1] << farmHas[2] << farmHas[3]
            << " trees=" << gs.trees.size()
            << " inv=P" << havePlum << " L" << haveLemon
            << " A" << haveApple << " B" << haveBan
            << " I" << haveIron << " W" << haveWood
            << " sc=" << (gs.shack[0].totalFruits() + 4*haveWood)
            << "/"   << (gs.shack[1].totalFruits() + 4*gs.shack[1][ResourceType::WOOD]));

        vector<Action> out;

        // ── TRAIN ──
        // T0: accept any affordable tier.
        // T1..T200: require tier C+ (no cheap fillers).
        // T201+: anything (desperation).
        // OPP-MATCH override: if opp has more trolls than us, train a
        //   troll with the *exact same stats* as their latest troll —
        //   matches user rule "the other player should not have more
        //   trolls than us, the created troll by us should have the
        //   same characteristics as the troll created by the other
        //   player". This also raises our effective troll cap so we
        //   stay even with opp.
        if (phase != FarmPhase::WINDDOWN) {
            static const Tier tiers[] = {
                {4, 3, 0, 2, "Y:4302"},     // 0  super-saboteur (mv=4)
                {3, 2, 2, 2, "X:3222"},     // 1  fast balanced (mv=3)
                {2, 2, 2, 2, "A:2222"},     // 2
                {2, 2, 1, 2, "B:2212"},     // 3
                {1, 3, 2, 2, "S1:1322"},    // 4
                {1, 3, 1, 2, "S2:1312"},    // 5
                {1, 2, 1, 2, "C:1212"},     // 6  ← IDX_C
                {1, 1, 1, 2, "D:1112"},     // 7
                {1, 1, 1, 1, "E:1111"},     // 8
                {1, 1, 1, 0, "F:1110"},     // 9  ← IDX_F
            };

            const int N = myTrollCount;
            auto costOf = [&](int a) { return N + a*a; };

            // Count opp trolls and find the newest one (highest id).
            int oppCount = 0;
            const Troll* latestOpp = nullptr;
            for (const Troll& tr : gs.trolls) {
                if (tr.owner != 1) continue;
                oppCount++;
                if (!latestOpp || tr.id > latestOpp->id) latestOpp = &tr;
            }

            // Effective cap: at least kTrollCap, but rises if opp has more
            // trolls than that — we must keep parity.
            const int effectiveTrollCap = std::max((int)kTrollCap, oppCount);

            bool emitted = false;

            // OPP-MATCH branch: opp has more trolls than us → train the
            // exact tier opp's latest troll has, IF we can afford it AND
            // the matched tier has chop ≥ 2. Boss often opens with a
            // harvester (chop=0 or 1) — mirroring it gives us a
            // train-resource-drained, low-wood-output troll. silver_logs
            // (22x11 map) showed MATCH of (1,2,2,1) cost A=5 L=5, then we
            // spent the rest of the game iron-starved with one weak
            // chopper. If opp's troll is chop<2, fall through to the
            // tier ladder below — we'll pick a proper chopper (C or
            // better) when our own resources allow it.
            if (myTrollCount < effectiveTrollCap
                && oppCount > myTrollCount
                && latestOpp != nullptr
                && latestOpp->chop >= 2) {
                const int mv  = latestOpp->movSpeed;
                const int cap = latestOpp->carryCap;
                const int hv  = latestOpp->harvest;
                const int ch  = latestOpp->chop;
                if (havePlum  >= costOf(mv) &&
                    haveLemon >= costOf(cap) &&
                    haveApple >= costOf(hv) &&
                    haveIron  >= costOf(ch)) {
                    out.push_back(Action::Train(mv, cap, hv, ch));
                    ostringstream tag;
                    tag << "MATCH" << (myTrollCount + 1)
                        << " " << mv << cap << hv << ch;
                    out.push_back(Action::Msg(tag.str()));
                    DBG("strategy: MATCH opp's latest troll (mv=" << mv
                        << " cap=" << cap << " hv=" << hv << " ch=" << ch
                        << ", N=" << N << ", oppCount=" << oppCount << ")");
                    emitted = true;
                }
            }

            // Fall back to the normal tier ladder (priority by index) when
            // (a) opp doesn't have more trolls, or (b) we couldn't afford
            // the exact match. Capped at kTrollCap unless opp pushed it.
            //
            // Early-game (turn ≤ 200) requires tier C or stronger. Cheap
            // tiers (D/E/F: cap=1, weak attrs) are *worse than no train*
            // because they consume apples/lemons/iron we need for the
            // next *real* train. Silver_logs showed T0 trained tier D
            // (1112), drained our 2 apples to 0, and we never trained
            // again all game (38 vs 213 loss). Skipping T0 when only
            // D/E/F are affordable matches tsukammo's pattern: harvest
            // & plant first 30-50 turns, train tier A later.
            if (!emitted && myTrollCount < effectiveTrollCap) {
                const size_t lastTier =
                    (gs.turn > 200)  ? (IDX_F + 1) :
                                       (IDX_C + 1);

                for (size_t i = 0; i < lastTier; i++) {
                    const auto& t = tiers[i];
                    if (havePlum  >= costOf(t.mv) &&
                        haveLemon >= costOf(t.cap) &&
                        haveApple >= costOf(t.hv) &&
                        haveIron  >= costOf(t.ch)) {
                        out.push_back(Action::Train(t.mv, t.cap, t.hv, t.ch));
                        ostringstream tag;
                        tag << "TRAIN" << (myTrollCount + 1) << " " << t.tag;
                        out.push_back(Action::Msg(tag.str()));
                        DBG("strategy: " << t.tag << " (N=" << N << ") — TRAIN "
                            << t.mv << ' ' << t.cap << ' ' << t.hv << ' ' << t.ch);
                        break;
                    }
                }
            }
        }

        // ── Identify chop targets for SETUP_CHOP: nearest 2 non-farm trees ──
        vector<int> chopTreeIdx;
        if (phase == FarmPhase::SETUP_CHOP || phase == FarmPhase::HARVEST) {
            vector<pair<int,int>> cands;
            for (size_t i = 0; i < gs.trees.size(); i++) {
                const auto& tr = gs.trees[i];
                int d = manhattan(tr.x, tr.y, sx, sy);
                if (d > FARM_RADIUS && tr.health > 0) {
                    cands.push_back({d, (int)i});
                }
            }
            std::sort(cands.begin(), cands.end());
            const size_t k = (phase == FarmPhase::SETUP_CHOP) ? 2 : 4;
            for (size_t i = 0; i < std::min(k, cands.size()); i++) {
                chopTreeIdx.push_back(cands[i].second);
            }
        }

        // ── Reservations: avoid 2 trolls planting same type or same cell ──
        bool willPlantType[FRUIT_COUNT] = {false, false, false, false};
        vector<char> reservedCell(W*H, 0);
        vector<char> blockedByAlly(W*H, 0);
        for (const auto& tr : gs.trolls) {
            if (tr.owner == 0) blockedByAlly[tr.y*W + tr.x] = 1;
        }

        // ── Iron need check.
        //    Mine iron while below tier-C cost (6 for N=2). Once iron hits 6,
        //    the would-be miner converts to far-chopper via role fall-through.
        //    Keeping it simple — earlier attempts at smarter checks (target
        //    iron based on affordable tier) caused under-mining loops.
        const bool needIron = (haveIron < 6)
                              && (myTrollCount < kTrollCap)
                              && (phase != FarmPhase::WINDDOWN);

        // ── Role assignment by TROLL STATS, not id order.
        //    User's rule: "The bot should be able to train a troll a high
        //    possible speed and carryCapacity, so he can attack the plants
        //    near the other player inventory". So the FASTEST troll (highest
        //    movSpeed, then highest carryCap as tiebreak) gets the SABOTEUR
        //    role — they can reach opp shack quickest.
        //    Remaining roles:
        //      MINER     = highest-id chop>0 troll not already assigned
        //                  saboteur, while iron is needed.
        //      FAR-CHOPPER = remaining chop>0 troll.
        //    starter (id 0) stays HARVESTER by default.
        int minerId = -1, farChopId = -1, saboteurId = -1;

        // Collect non-starter chop>0 trolls
        vector<int> trainedChoppers;
        for (const auto& tr : gs.trolls) {
            if (tr.owner != 0 || tr.id == 0 || tr.chop <= 0) continue;
            trainedChoppers.push_back(tr.id);
        }

        // Helper: lookup troll stats by id
        auto stats = [&](int id) -> const Troll* {
            for (const auto& tr : gs.trolls) if (tr.id == id) return &tr;
            return nullptr;
        };

        // SABOTEUR — fastest chopper, ASSIGNED ALWAYS (≥1 trained troll).
        // User feedback: "they do chop but after 80 turns, which is long
        // enough for the enemy to have enough resources to train more
        // trolls". Previously gated on `myTrollCount >= 3` so the 1st-
        // trained troll didn't sabotage until the 3rd train fired (T70+).
        // Now: same troll dual-roles — miner when iron is needed, saboteur
        // the rest of the time. Per-troll resolution below picks miner
        // priority when needIron.
        {
            int bestKey = -1;
            for (int id : trainedChoppers) {
                const Troll* tr = stats(id);
                if (!tr) continue;
                int key = tr->movSpeed * 100 + tr->carryCap;
                if (key > bestKey) { bestKey = key; saboteurId = id; }
            }
        }

        // MINER = highest-id chopper when iron is needed. Can overlap
        // with saboteurId; the per-troll role check below resolves it
        // (miner wins while needIron is true).
        //
        // Fallback to starter (id=0) if NO trained choppers exist yet.
        // Boss-game silver_logs (map 22x11 start I=3) showed nobody
        // mined for the first 5+ turns because trainedChoppers was empty
        // and starter was barred from miner role by id==0 filter. Iron
        // stayed at I=1-3 for 194 turns, blocking all further trains.
        if (needIron) {
            int bestId = -1;
            for (int id : trainedChoppers) {
                if (id > bestId) { bestId = id; }
            }
            if (bestId < 0) {
                // No trained choppers — use starter as miner fallback if
                // it can chop (always true: starter has chop=1).
                const Troll* st = stats(0);
                if (st && st->chop > 0) bestId = 0;
            }
            minerId = bestId;
        }

        // FAR-CHOPPER = any remaining chopper (not saboteur, not miner).
        for (int id : trainedChoppers) {
            if (id == saboteurId || id == minerId) continue;
            farChopId = id; break;
        }

        // ── Identify chop targets, partitioned by "whose side of the map":
        //    farChopIdx   = non-farm trees on OUR side (closer to my shack)
        //    enemyFarmIdx = trees on OPP side (closer to opp shack)
        //                    Saboteur attacks these to deny tsukammo harvest.
        const int oppX = gs.shackPos[1][0];
        const int oppY = gs.shackPos[1][1];
        vector<int> farChopIdx;
        vector<int> enemyFarmIdx;
        for (size_t i = 0; i < gs.trees.size(); i++) {
            const auto& tr = gs.trees[i];
            if (tr.health <= 0) continue;
            int dMy  = manhattan(tr.x, tr.y, sx, sy);
            int dOpp = manhattan(tr.x, tr.y, oppX, oppY);
            if (dMy <= FARM_RADIUS) continue;  // our farm — protected
            if (dOpp < dMy)         enemyFarmIdx.push_back((int)i);  // opp-side
            else                    farChopIdx.push_back((int)i);    // our-side
        }
        // Sort by distance from MY shack (ascending) so we chop closer trees first
        std::sort(farChopIdx.begin(), farChopIdx.end(),
            [&](int a, int b) {
                return manhattan(gs.trees[a].x, gs.trees[a].y, sx, sy)
                     < manhattan(gs.trees[b].x, gs.trees[b].y, sx, sy);
            });
        // Saboteur should also work depth-first toward enemy: sort by
        // distance from OPP shack ascending so we kill their nearby trees first.
        std::sort(enemyFarmIdx.begin(), enemyFarmIdx.end(),
            [&](int a, int b) {
                return manhattan(gs.trees[a].x, gs.trees[a].y, oppX, oppY)
                     < manhattan(gs.trees[b].x, gs.trees[b].y, oppX, oppY);
            });

        // ── DEFENDER: detect opp troll about to chop one of OUR farm trees.
        //   The simulator awards wood to the troll whose chop drops the tree
        //   to health<=0 — process order is by troll id ascending. So if
        //   opp is going to kill our tree, we race them: our fastest +
        //   chop troll walks to that same cell. Whoever lands the final
        //   blow first gets the wood. Per user rule: "if a troll is chopping
        //   a tree that we harvest, the troll with the highest speed and
        //   chop power should go and chop with that enemy troll the same
        //   tree."
        int defendTreeIdx = -1;
        {
            int bestUrgency = INT32_MAX;  // lower = more urgent (less health left)
            for (int idx : farmTreeIdx) {
                const Tree& tr = gs.trees[idx];
                if (tr.health <= 0) continue;
                bool oppCanChop = false;
                for (const Troll& opp : gs.trolls) {
                    if (opp.owner != 1) continue;
                    if (opp.chop <= 0) continue;
                    int d = manhattan(opp.x, opp.y, tr.x, tr.y);
                    if (d <= 1) { oppCanChop = true; break; }
                }
                if (oppCanChop && tr.health < bestUrgency) {
                    bestUrgency = tr.health;
                    defendTreeIdx = idx;
                }
            }
        }
        // Defender = our highest movSpeed * 100 + chop troll. May be the same
        // troll as the saboteur — defender takes priority since the wood is
        // about to be lost RIGHT NOW.
        int defenderId = -1;
        if (defendTreeIdx >= 0) {
            int bestKey = -1;
            for (const Troll& tr : gs.trolls) {
                if (tr.owner != 0 || tr.chop <= 0) continue;
                int key = tr.movSpeed * 100 + tr.chop;
                if (key > bestKey) { bestKey = key; defenderId = tr.id; }
            }
        }

        // ── Per-troll priority rules ──
        for (const auto& t : gs.trolls) {
            if (t.owner != 0) continue;
            const int carry        = t.carry.total();
            const int carryFruits  = t.carry.totalFruits();
            const int freeCap      = t.carryCap - carry;
            const int distShack    = manhattan(t.x, t.y, sx, sy);
            const bool adjShack    = (distShack == 1);
            const bool onShack     = (distShack == 0);

            // Tree at troll's cell?
            int onTreeIdx = -1;
            for (size_t i = 0; i < gs.trees.size(); i++) {
                if (gs.trees[i].x == t.x && gs.trees[i].y == t.y) {
                    onTreeIdx = (int)i; break;
                }
            }
            const Tree* onTree = (onTreeIdx >= 0) ? &gs.trees[onTreeIdx] : nullptr;
            const bool onFarmTree = (onTree && distShack <= FARM_RADIUS);

            // ── P1: PLANT ── on near-shack grass with a fruit we're
            //                  carrying AND `wantPlantMore(f)` (which
            //                  covers both urgent replant of missing
            //                  types and aggressive redundancy when we
            //                  have surplus + slot).
            if (phase != FarmPhase::WINDDOWN
                && carryFruits > 0
                && distShack >= 1 && distShack <= FARM_RADIUS
                && gs.map.at(t.x, t.y) == Terrain::GRASS
                && onTreeIdx < 0) {
                int plantType = -1;
                for (int f = 0; f < FRUIT_COUNT; f++) {
                    if (t.carry.r[f] > 0 && wantPlantMore(f)
                        && !willPlantType[f]) {
                        plantType = f; break;
                    }
                }
                if (plantType >= 0) {
                    out.push_back(Action::Plant(t.id, (FruitType)plantType));
                    willPlantType[plantType] = true;
                    farmHas[plantType] = true;
                    farmCount[plantType]++;
                    continue;
                }
            }

            // ── P2: HARVEST ── on a farm tree with fruits + free cap.
            //                   Miner/far-chopper/saboteur/defender skip
            //                   harvest so they stay on their respective
            //                   missions instead of getting distracted.
            // Roles resolve in priority order (miner > saboteur > farchop)
            // so when a troll is both saboteur and miner (single trained
            // troll case), needIron flips it between the two each turn:
            // iron < 6 → mine, iron >= 6 → walk to opp side and chop.
            const bool isMinerRole    = (t.id == minerId && needIron);
            const bool isSaboteurRole = (t.id == saboteurId && !isMinerRole);
            const bool isFarChopRole  = (t.id == farChopId
                                         && !isMinerRole && !isSaboteurRole);
            const bool isDefender     = (t.id == defenderId && defendTreeIdx >= 0);
            const bool isSpecialist   = isMinerRole || isFarChopRole
                                      || isSaboteurRole || isDefender;
            if (onTree && onFarmTree && onTree->fruits > 0
                && t.harvest > 0 && freeCap > 0
                && !isSpecialist) {
                out.push_back(Action::Harvest(t.id));
                continue;
            }

            // ── P2b: HARVEST a NON-farm tree if our shack stockpile of
            //         that type is 0. Boss-game showed apple=0 from T100
            //         onward (farm apple tree alone can't refill fast
            //         enough to support both training drain and harvest
            //         cycle). The bot now also taps wild trees when the
            //         shack runs dry, even if a farm tree of that type
            //         still exists.
            if (onTree && !onFarmTree && onTree->fruits > 0
                && t.harvest > 0 && freeCap > 0
                && phase != FarmPhase::WINDDOWN
                && !(t.id == minerId && needIron)) {
                int f = (int)onTree->type;
                if (gs.shack[0].r[f] == 0 && !willPlantType[f]) {
                    out.push_back(Action::Harvest(t.id));
                    continue;
                }
            }

            // ── P3: CHOP ── on a non-farm tree with HP.
            //               Kept active in WINDDOWN: wood scores 4× per
            //               unit, every chop is points.
            //               EXCEPTION: defender chops the defended farm
            //               tree too — racing opp for the killing-blow wood.
            const bool choppingDefendedTree =
                isDefender && onTreeIdx == defendTreeIdx;
            if (onTree && onTree->health > 0 && t.chop > 0 && freeCap > 0
                && (!onFarmTree || choppingDefendedTree)) {
                out.push_back(Action::Chop(t.id));
                continue;
            }

            // ── P3b: MINE ── adj to an IRON_ORE tile, chop>0, free cap,
            //                  AND we actually need iron. Without the
            //                  needIron gate, trolls opportunistically
            //                  mined as they walked past iron tiles —
            //                  silver_logs showed I=180 (5 used, 175 wasted)
            //                  because the bot kept stockpiling iron we'd
            //                  never spend.
            if (t.chop > 0 && freeCap > 0 && needIron
                && phase != FarmPhase::WINDDOWN) {
                bool adjIron = false;
                static const int DX[4] = {1,-1,0,0};
                static const int DY[4] = {0,0,1,-1};
                for (int k = 0; k < 4; k++) {
                    int nx = t.x + DX[k], ny = t.y + DY[k];
                    if (gs.map.inBounds(nx, ny)
                        && gs.map.at(nx, ny) == Terrain::IRON_ORE) {
                        adjIron = true; break;
                    }
                }
                if (adjIron) {
                    out.push_back(Action::Mine(t.id));
                    continue;
                }
            }

            // ── P4: DROP ── adj-shack with carry.
            //               Skip if we're carrying a fruit type the farm
            //               still needs — otherwise we PICK→DROP loop
            //               (drop is higher priority than plant, so a troll
            //               on a planted tree drops its just-picked seed
            //               back into the shack). Hold the fruit; the MOVE
            //               branch will route to a free plant slot.
            if (carry > 0 && adjShack) {
                // Refuse DROP if we're carrying a fruit we *can plant*
                // somewhere (wantPlantMore returns true → free slot
                // exists AND we have surplus). Otherwise drop. The
                // freeSlot check inside wantPlantMore prevents the
                // stuck-troll loop that broke the previous attempt.
                bool carryingPlantable = false;
                if (phase != FarmPhase::WINDDOWN) {
                    for (int f = 0; f < FRUIT_COUNT; f++) {
                        if (t.carry.r[f] > 0 && wantPlantMore(f)
                            && !willPlantType[f]) {
                            carryingPlantable = true; break;
                        }
                    }
                }
                if (!carryingPlantable) {
                    out.push_back(Action::Drop(t.id));
                    continue;
                }
            }

            // ── P5: PICK from shack ── adj-shack, empty hands, farm wants
            //                           more of a type that's in shack.
            //                           Aggressive: also picks when farm
            //                           already has 1 of the type but
            //                           wantPlantMore returns true (slot
            //                           available + surplus fruit).
            //
            // Solvency gate: only PICK if shack has > kReplantReserve of
            // that fruit. On close-shacks maps (silver_logs 20x10 boss
            // game, my_shack=(8,5) opp=(11,4)) Boss raced over and
            // chopped our farm trees repeatedly. The urgent-replant rule
            // had no off switch — we replanted 9 PLUMs and 9 LEMONs,
            // draining shack to 0, never trained, lost 61 vs 275. The
            // reserve threshold (4) means we keep ≥4 of the fruit in
            // shack post-pick — enough for a tier C/B train later
            // instead of feeding endless replant cycles to Boss.
            //
            // Gate is at PICK (not inside wantPlantMore) so DROP-refusal
            // stays stable across the PICK→next-turn transition (shack
            // drops by 1 but wantPlantMore doesn't flip-flop).
            constexpr int kReplantReserve = 4;
            if (carry == 0 && adjShack && phase != FarmPhase::WINDDOWN) {
                int pickType = -1;
                for (int f = 0; f < FRUIT_COUNT; f++) {
                    if (!wantPlantMore(f) || willPlantType[f]) continue;
                    if (gs.shack[0].r[f] > kReplantReserve) { pickType = f; break; }
                }
                if (pickType >= 0) {
                    out.push_back(Action::Pick(t.id, (FruitType)pickType));
                    willPlantType[pickType] = true;
                    continue;
                }
            }

            // ── P6: MOVE ── pick target by role + fall-through.
            // Walk lambda: miner avoids tree cells (don't get distracted
            // mid-trip to iron). Far-chopper and saboteur WANT to land
            // on trees to chop, so they include them.
            auto walk = [&](int x, int y) -> bool {
                if (gs.map.at(x, y) != Terrain::GRASS) return false;
                int idx = y*W + x;
                if (blockedByAlly[idx] && !(x == t.x && y == t.y)) return false;
                if (reservedCell[idx]) return false;
                if (isMinerRole && treeIndexAt(gs, x, y) >= 0
                    && !(x == t.x && y == t.y)) return false;
                return true;
            };
            PathInfo p = bfs(gs.map, t.x, t.y, walk);

            int tx = sx, ty = sy;
            bool targetSet = false;

            // Helper: find nearest reachable cell from a list of tree indices.
            auto pickNearestTree = [&](const vector<int>& idxs,
                                       bool requireFruit) -> int {
                int bestD = INT32_MAX, bestIdx = -1;
                for (int idx : idxs) {
                    const auto& tr = gs.trees[idx];
                    if (requireFruit && tr.fruits <= 0) continue;
                    if (reservedCell[tr.y*W + tr.x]) continue;
                    int dist = p.at(tr.x, tr.y);
                    if (dist < 0) continue;
                    if (dist < bestD) { bestD = dist; bestIdx = idx; }
                }
                return bestIdx;
            };

            // (1) Carrying a plantable fruit → walk to near-shack grass slot.
            //     Priority: Manhattan dist=1 (the four orthogonal neighbours
            //     of shack) FIRST. Only fall back to dist=2 if all four
            //     dist=1 slots are occupied. Tight farm = short cycles.
            //
            // Use wantPlantMore(f) not !farmHas[f] — they diverge when the
            // farm has 1 banana and we want a 2nd for redundancy. Old code
            // PICKed a redundant banana but the MOVE-to-plant target never
            // resolved (because farmHas[BANANA]=true blocked `desired`),
            // so the troll dropped it next turn and looped forever.
            if (!targetSet && carryFruits > 0 && phase != FarmPhase::WINDDOWN) {
                int desired = -1;
                for (int f = 0; f < FRUIT_COUNT; f++) {
                    if (t.carry.r[f] > 0 && wantPlantMore(f)) { desired = f; break; }
                }
                if (desired >= 0) {
                    int bestKey = INT32_MAX, bestX = -1, bestY = -1;
                    for (int y = 0; y < H; y++) {
                        for (int x = 0; x < W; x++) {
                            int d = manhattan(x, y, sx, sy);
                            if (d < 1 || d > FARM_RADIUS) continue;
                            if (gs.map.at(x, y) != Terrain::GRASS) continue;
                            if (treeIndexAt(gs, x, y) >= 0) continue;
                            if (reservedCell[y*W + x]) continue;
                            int dist = p.at(x, y);
                            if (dist < 0) continue;
                            // Composite key: distance-from-shack dominant
                            // (bias toward d=1), BFS-dist-from-troll tie-break.
                            int key = d * 1024 + dist;
                            if (key < bestKey) {
                                bestKey = key; bestX = x; bestY = y;
                            }
                        }
                    }
                    if (bestX >= 0) {
                        tx = bestX; ty = bestY;
                        reservedCell[bestY*W + bestX] = 1;
                        targetSet = true;
                    }
                }
            }

            // (2) Carrying wood/iron/non-plantable → go adj-shack to drop
            //     (target stays at shack; shack-redirect below picks adj cell)

            // (3) Empty hands — role-based primary target, with fall-through.
            //     We REMOVED the `phase != WINDDOWN` gate here on purpose:
            //     WINDDOWN trolls without carry should still walk to farm
            //     trees to harvest, otherwise they idle for 60 turns and
            //     the action stream emits empty lines (observed at T242+
            //     in silver_logs test).
            if (!targetSet && carry == 0) {

                // (3def) DEFENDER → walk to the farm tree under attack.
                //         Takes precedence over every other role: the wood
                //         is about to be lost. P3 CHOP fires once we arrive,
                //         racing opp for the killing blow.
                if (!targetSet && isDefender) {
                    const Tree& tr = gs.trees[defendTreeIdx];
                    tx = tr.x; ty = tr.y;
                    reservedCell[ty*W + tx] = 1;
                    targetSet = true;
                }

                // (3a) MINER → nearest iron-adjacent grass cell
                if (!targetSet && isMinerRole) {
                    int bestD = INT32_MAX, bestX = -1, bestY = -1;
                    static const int DX[4] = {1,-1,0,0};
                    static const int DY[4] = {0,0,1,-1};
                    for (int y = 0; y < H; y++) {
                        for (int x = 0; x < W; x++) {
                            if (gs.map.at(x, y) != Terrain::GRASS) continue;
                            bool adj = false;
                            for (int k = 0; k < 4; k++) {
                                int nx = x + DX[k], ny = y + DY[k];
                                if (gs.map.inBounds(nx, ny)
                                    && gs.map.at(nx, ny) == Terrain::IRON_ORE) {
                                    adj = true; break;
                                }
                            }
                            if (!adj) continue;
                            if (reservedCell[y*W + x]) continue;
                            int dist = p.at(x, y);
                            if (dist < 0) continue;
                            if (dist < bestD) { bestD = dist; bestX = x; bestY = y; }
                        }
                    }
                    if (bestX >= 0) {
                        tx = bestX; ty = bestY;
                        reservedCell[bestY*W + bestX] = 1;
                        targetSet = true;
                    }
                }

                // (3b) SABOTEUR → nearest tree on opp-side (closer to opp).
                //      Sorted by distance from OPP shack so we attack their
                //      farm first. Saboteur does NOT fall through to our-side
                //      chop targets — that would just compete with the
                //      farChopper. If no enemy-side trees exist, saboteur
                //      converts to a far-chopper via the fall-through below.
                if (!targetSet && isSaboteurRole && !enemyFarmIdx.empty()) {
                    int idx = pickNearestTree(enemyFarmIdx, /*requireFruit=*/false);
                    if (idx >= 0) {
                        tx = gs.trees[idx].x; ty = gs.trees[idx].y;
                        reservedCell[ty*W + tx] = 1;
                        targetSet = true;
                    }
                }

                // (3c) FAR-CHOPPER → nearest non-farm tree (our side first)
                if (!targetSet && isFarChopRole && !farChopIdx.empty()) {
                    int idx = pickNearestTree(farChopIdx, /*requireFruit=*/false);
                    if (idx >= 0) {
                        tx = gs.trees[idx].x; ty = gs.trees[idx].y;
                        reservedCell[ty*W + tx] = 1;
                        targetSet = true;
                    }
                }

                // (3d) Shack stockpile of some fruit is 0 → walk to a
                //      non-farm tree of that type. Drop the old SETUP_PLANT-
                //      and-!farmHas gates so this also fires in HARVEST phase
                //      when our farm tree alone can't refill apple fast
                //      enough (the Boss-game pattern: A=0 from T100 onward).
                if (!targetSet && phase != FarmPhase::WINDDOWN) {
                    int needType = -1;
                    for (int f = 0; f < FRUIT_COUNT; f++) {
                        if (willPlantType[f]) continue;
                        if (gs.shack[0].r[f] > 0) continue;
                        needType = f; break;
                    }
                    if (needType >= 0) {
                        int bestD = INT32_MAX, bestTx = -1, bestTy = -1;
                        for (size_t i = 0; i < gs.trees.size(); i++) {
                            const auto& tr = gs.trees[i];
                            if ((int)tr.type != needType) continue;
                            if (tr.fruits <= 0) continue;
                            if (reservedCell[tr.y*W + tr.x]) continue;
                            int dist = p.at(tr.x, tr.y);
                            if (dist < 0) continue;
                            if (dist < bestD) {
                                bestD = dist; bestTx = tr.x; bestTy = tr.y;
                            }
                        }
                        if (bestTx >= 0) {
                            tx = bestTx; ty = bestTy;
                            reservedCell[bestTy*W + bestTx] = 1;
                            targetSet = true;
                        }
                    }
                }

                // (3e) HARVESTER role (default) → farm tree with fruits,
                //      preferring trees of fruit types our shack has LEAST of
                //      so the harvester rotates instead of fixating on one
                //      tree. silver_logs symptom: troll 0 harvested PLUM
                //      (4,7) repeatedly → P=69 while L=0 for all 300 turns.
                if (!targetSet && !isSpecialist) {
                    int bestKey = INT32_MAX, bestTx = -1, bestTy = -1;
                    for (int idx : farmTreeIdx) {
                        const auto& tr = gs.trees[idx];
                        if (tr.fruits <= 0) continue;
                        if (reservedCell[tr.y*W + tr.x]) continue;
                        int dist = p.at(tr.x, tr.y);
                        if (dist < 0) continue;
                        // Composite key: shack stock of this fruit type
                        // (lower = more urgent), tie-break by BFS distance.
                        int stock = gs.shack[0].r[(int)tr.type];
                        int key = stock * 64 + dist;
                        if (key < bestKey) {
                            bestKey = key; bestTx = tr.x; bestTy = tr.y;
                        }
                    }
                    if (bestTx >= 0) {
                        tx = bestTx; ty = bestTy;
                        reservedCell[bestTy*W + bestTx] = 1;
                        targetSet = true;
                    }
                }

                // (3f) FALL-THROUGH (specialist with nothing to do):
                //       try any non-farm tree → enemy-farm tree → farm tree.
                if (!targetSet && t.chop > 0) {
                    int idx = pickNearestTree(farChopIdx, /*requireFruit=*/false);
                    if (idx < 0) {
                        idx = pickNearestTree(enemyFarmIdx, /*requireFruit=*/false);
                    }
                    if (idx >= 0) {
                        tx = gs.trees[idx].x; ty = gs.trees[idx].y;
                        reservedCell[ty*W + tx] = 1;
                        targetSet = true;
                    }
                }
                // (3g) Final fallback: farm tree with fruits (any troll)
                if (!targetSet) {
                    int idx = pickNearestTree(farmTreeIdx, /*requireFruit=*/true);
                    if (idx >= 0) {
                        tx = gs.trees[idx].x; ty = gs.trees[idx].y;
                        reservedCell[ty*W + tx] = 1;
                        targetSet = true;
                    }
                }
            }
            // (d) SETUP_PLANT, empty hands → walk adj-shack (next turn PICK)
            //     OR onShack at T0 (TRAIN needs shack cell vacated by the
            //     Move phase that runs BEFORE Train in the simulator).
            //     Redirect target from shack-itself to a shack-adjacent
            //     grass cell so the troll actually moves.
            if (onShack && tx == sx && ty == sy) {
                static const int DX[4] = {1,-1,0,0};
                static const int DY[4] = {0,0,1,-1};
                int bestD = INT32_MAX, bestX = -1, bestY = -1;
                for (int k = 0; k < 4; k++) {
                    int nx = sx + DX[k], ny = sy + DY[k];
                    if (!gs.map.inBounds(nx, ny)) continue;
                    if (gs.map.at(nx, ny) != Terrain::GRASS) continue;
                    if (blockedByAlly[ny*W + nx]) continue;
                    if (reservedCell[ny*W + nx]) continue;
                    // Prefer cells closer to a tree (any tree) for SETUP_CHOP/HARVEST
                    int dt = INT32_MAX;
                    for (const auto& tr : gs.trees) {
                        int dd = std::abs(tr.x - nx) + std::abs(tr.y - ny);
                        if (dd < dt) dt = dd;
                    }
                    if (dt < bestD) { bestD = dt; bestX = nx; bestY = ny; }
                }
                if (bestX >= 0) { tx = bestX; ty = bestY; }
            }

            // If the chosen target IS the shack cell itself (non-walkable),
            // redirect to the nearest reachable shack-adjacent grass cell.
            // Otherwise we'd emit MOVE x sx sy every turn and the troll
            // would oscillate around the shack — saw this in silver_logs
            // (286 × "MOVE 2 6 4" wasted).
            if (tx == sx && ty == sy) {
                int bestD = INT32_MAX, bestX = -1, bestY = -1;
                static const int DX[4] = {1,-1,0,0};
                static const int DY[4] = {0,0,1,-1};
                for (int k = 0; k < 4; k++) {
                    int nx = sx + DX[k], ny = sy + DY[k];
                    if (!gs.map.inBounds(nx, ny)) continue;
                    if (gs.map.at(nx, ny) != Terrain::GRASS) continue;
                    if (reservedCell[ny*W + nx]) continue;
                    int dist = p.at(nx, ny);
                    if (dist < 0) continue;
                    if (dist < bestD) { bestD = dist; bestX = nx; bestY = ny; }
                }
                if (bestX >= 0) { tx = bestX; ty = bestY; }
            }

            // Skip emitting a MOVE that goes nowhere
            if (t.x == tx && t.y == ty) continue;

            // Never emit a MOVE whose target is the shack cell itself
            // (non-walkable). When all adj-shack grass cells are reserved
            // or blocked, the redirect above can't find a fallback, so
            // we just skip the MOVE rather than spam an impossible target.
            // The engine treats missing-troll-action as WAIT, which is fine.
            if (tx == sx && ty == sy) continue;

            out.push_back(Action::Move(t.id, tx, ty));
        }

        // Per-troll fill: any troll without an action gets WAIT (rare)
        // — we just emit what we collected; the engine accepts a single
        // command per troll per turn but tolerates omissions.
        return out;
    }
};
//
// Mirrors the engine's turn-order resolution so that strategies can do
// lookahead search. Phase 1 (this version) implements `Simulator::step`
// — apply one turn of actions and return the resulting state. Used by
// future layers (evaluator + search). GreedyStrategy is unaware of it.
//
// Turn-order (from expert rules):
//   1. Move trolls
//   2. Harvest
//   3. Plant
//   4. Chop
//   5. Pick
//   6. Train
//   7. Drop
//   8. Mine
//   9. Grow trees
//
// Tables: tree HP per size, cooldown base + water-adjacent.

// HP by (FruitType index, size-1). PLUM/LEMON share. APPLE highest. BANANA lowest.
static constexpr int kTreeHP[4][4] = {
    {  6,  8, 10, 12 },   // PLUM
    {  6,  8, 10, 12 },   // LEMON
    { 11, 14, 17, 20 },   // APPLE
    {  3,  4,  5,  6 },   // BANANA
};
// Cooldown by FruitType: [base, near-water].
static constexpr int kTreeCooldown[4][2] = {
    { 8, 3 },   // PLUM
    { 8, 3 },   // LEMON
    { 9, 2 },   // APPLE
    { 6, 4 },   // BANANA
};

[[maybe_unused]] static int treeMaxHP(FruitType type, int size) {
    return kTreeHP[(int)type][std::max(1, std::min(4, size)) - 1];
}
[[maybe_unused]] static int treeCooldown(FruitType type, bool nearWater) {
    return kTreeCooldown[(int)type][nearWater ? 1 : 0];
}

// Is (x,y) 4-adjacent to a WATER tile?
[[maybe_unused]] static bool isWaterAdjacent(const GridMap& m, int x, int y) {
    static const int DX[4] = {1,-1,0,0};
    static const int DY[4] = {0,0,1,-1};
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (m.inBounds(nx, ny) && m.at(nx, ny) == Terrain::WATER) return true;
    }
    return false;
}

// Is (x,y) 4-adjacent to (sx,sy)?
[[maybe_unused]] static bool isAdj4(int x, int y, int sx, int sy) {
    return std::abs(x - sx) + std::abs(y - sy) == 1;
}

// Find the index of the tree currently at (x,y), or -1.
[[maybe_unused]] static int treeIndexAt(const GameState& gs, int x, int y) {
    for (size_t i = 0; i < gs.trees.size(); i++) {
        if (gs.trees[i].x == x && gs.trees[i].y == y) return (int)i;
    }
    return -1;
}

// Find the index of a troll by id, or -1.
[[maybe_unused]] static int trollIndexById(const GameState& gs, int id) {
    for (size_t i = 0; i < gs.trolls.size(); i++) {
        if (gs.trolls[i].id == id) return (int)i;
    }
    return -1;
}

struct Simulator {
    // Apply both players' actions for one turn, return the resulting state.
    static GameState step(GameState state,
                          const vector<Action>& myActions,
                          const vector<Action>& oppActions) {
        // Combined list for per-troll phases (action.trollId routes to owner).
        vector<Action> all;
        all.reserve(myActions.size() + oppActions.size());
        for (const auto& a : myActions)  all.push_back(a);
        for (const auto& a : oppActions) all.push_back(a);

        applyMove   (state, all);
        applyHarvest(state, all);
        applyPlant  (state, all);
        applyChop   (state, all);
        applyPick   (state, all);
        applyTrain  (state, myActions, oppActions);
        applyDrop   (state, all);
        applyMine   (state, all);
        applyGrow   (state);

        state.turn += 1;
        return state;
    }

    // ---- Phase implementations ----------------------------------------

    // Phase 1: Move
    // For each troll with a MOVE action, BFS-route up to movementSpeed cells
    // toward the target. Process in troll-id order; same-team trolls already
    // moved this turn count as blockers.
    static void applyMove(GameState& s, const vector<Action>& actions) {
        // Build current-position blocker set per team.
        const int W = s.map.w, H = s.map.h;
        vector<char> blockedTeam0(W*H, 0);
        vector<char> blockedTeam1(W*H, 0);
        for (const auto& t : s.trolls) {
            (t.owner == 0 ? blockedTeam0 : blockedTeam1)[t.y*W + t.x] = 1;
        }

        // Build a map of troll.id → action (only MOVE actions).
        // Iterate trolls in id order so resolution is stable.
        vector<Troll*> ordered;
        for (auto& t : s.trolls) ordered.push_back(&t);
        std::sort(ordered.begin(), ordered.end(),
                  [](const Troll* a, const Troll* b) { return a->id < b->id; });

        for (Troll* tp : ordered) {
            Troll& t = *tp;
            // Find a MOVE action for this troll.
            const Action* mv = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::MOVE && a.trollId == t.id) { mv = &a; break; }
            }
            if (!mv) continue;

            auto& blocked = (t.owner == 0 ? blockedTeam0 : blockedTeam1);
            // Walkability: GRASS only, not blocked by own-team troll (except self).
            auto walk = [&](int x, int y) -> bool {
                if (s.map.at(x, y) != Terrain::GRASS) return false;
                int idx = y*W + x;
                if (blocked[idx] && !(x == t.x && y == t.y)) return false;
                return true;
            };

            // BFS to find distances.
            PathInfo p = bfs(s.map, t.x, t.y, walk);

            // Find the nearest reachable cell to (mv->x, mv->y) within
            // movementSpeed steps. If destination itself is reachable, use it
            // (or stop at movementSpeed steps along path).
            int targetX = mv->x, targetY = mv->y;
            // Walk back from target along BFS tree until reachable cell ≤ speed.
            // Simpler approach: scan all cells, pick one minimizing
            //   distance(troll, cell) ≤ speed AND distance(cell, target)
            int bestStep = -1;
            int bestToTarget = INT32_MAX;
            int bestStepDist = INT32_MAX;
            int newX = t.x, newY = t.y;
            for (int yy = 0; yy < H; yy++) {
                for (int xx = 0; xx < W; xx++) {
                    int d = p.at(xx, yy);
                    if (d < 0 || d > t.movSpeed) continue;
                    int dt = std::abs(xx - targetX) + std::abs(yy - targetY);
                    // Prefer: closer to target; tiebreak: more steps taken (move farther).
                    if (dt < bestToTarget ||
                        (dt == bestToTarget && d > bestStepDist)) {
                        bestToTarget = dt;
                        bestStepDist = d;
                        bestStep = d;
                        newX = xx; newY = yy;
                    }
                }
            }
            (void)bestStep;

            // Move the troll. Update blocked set.
            if (newX != t.x || newY != t.y) {
                blocked[t.y*W + t.x] = 0;
                t.x = newX; t.y = newY;
                blocked[t.y*W + t.x] = 1;
            }
        }
    }

    // Phase 2: Harvest
    // For each troll with HARVEST, take fruits from the tree on its cell.
    // Two-troll sharing: not fully modeled in Phase 1 — we apply per-troll
    // sequentially. Refine in Phase 2 if it matters for validation.
    static void applyHarvest(GameState& s, const vector<Action>& actions) {
        for (auto& t : s.trolls) {
            // Find HARVEST for this troll.
            const Action* h = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::HARVEST && a.trollId == t.id) { h = &a; break; }
            }
            if (!h) continue;
            if (t.harvest <= 0) continue;
            int treeIdx = treeIndexAt(s, t.x, t.y);
            if (treeIdx < 0) continue;
            Tree& tr = s.trees[treeIdx];
            if (tr.fruits <= 0) continue;
            int freeCap = t.carryCap - t.carry.total();
            int take = std::min({freeCap, t.harvest, tr.fruits});
            if (take <= 0) continue;
            tr.fruits -= take;
            t.carry.r[(int)tr.type] += take;
        }
    }

    // Phase 3: Plant
    // Place a new size-1 tree on troll's cell if conditions met.
    static void applyPlant(GameState& s, const vector<Action>& actions) {
        for (auto& t : s.trolls) {
            const Action* p = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::PLANT && a.trollId == t.id) { p = &a; break; }
            }
            if (!p) continue;
            // Need ≥1 fruit of that type in carry.
            if (t.carry.r[(int)p->fruit] <= 0) continue;
            // Can't plant on existing tree.
            if (treeIndexAt(s, t.x, t.y) >= 0) continue;
            // Must be on GRASS.
            if (s.map.at(t.x, t.y) != Terrain::GRASS) continue;

            t.carry.r[(int)p->fruit] -= 1;
            Tree nt;
            nt.type = p->fruit;
            nt.x = t.x; nt.y = t.y;
            nt.size = 1;
            nt.health = treeMaxHP(p->fruit, 1);
            nt.fruits = 0;
            nt.cooldown = treeCooldown(p->fruit, isWaterAdjacent(s.map, t.x, t.y));
            s.trees.push_back(nt);
        }
    }

    // Phase 4: Chop
    static void applyChop(GameState& s, const vector<Action>& actions) {
        // We process trolls in id order. Track fallen trees to remove later.
        vector<int> toRemove;
        for (auto& t : s.trolls) {
            const Action* c = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::CHOP && a.trollId == t.id) { c = &a; break; }
            }
            if (!c) continue;
            if (t.chop <= 0) continue;
            int treeIdx = treeIndexAt(s, t.x, t.y);
            if (treeIdx < 0) continue;
            Tree& tr = s.trees[treeIdx];
            if (tr.health <= 0) continue;

            int freeCap = t.carryCap - t.carry.total();
            if (freeCap <= 0) continue;   // engine says wood vanishes if full; we'll not collect

            tr.health -= t.chop;
            if (tr.health <= 0) {
                // Tree dies, troll collects wood = size (capped by free cap).
                int wood = std::min(freeCap, tr.size);
                t.carry.r[(int)ResourceType::WOOD] += wood;
                toRemove.push_back(treeIdx);
            }
        }
        // Remove dead trees (in descending order to keep indices valid).
        std::sort(toRemove.rbegin(), toRemove.rend());
        toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
        for (int idx : toRemove) {
            s.trees.erase(s.trees.begin() + idx);
        }
    }

    // Phase 5: Pick
    static void applyPick(GameState& s, const vector<Action>& actions) {
        for (auto& t : s.trolls) {
            const Action* p = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::PICK && a.trollId == t.id) { p = &a; break; }
            }
            if (!p) continue;
            // Must be adjacent to OWN shack.
            const int sx = s.shackPos[t.owner][0];
            const int sy = s.shackPos[t.owner][1];
            if (!isAdj4(t.x, t.y, sx, sy)) continue;
            int freeCap = t.carryCap - t.carry.total();
            if (freeCap <= 0) continue;
            if (s.shack[t.owner].r[(int)p->fruit] <= 0) continue;
            s.shack[t.owner].r[(int)p->fruit] -= 1;
            t.carry.r[(int)p->fruit] += 1;
        }
    }

    // Phase 6: Train
    // Team-level. Per spec one TRAIN per player per turn. Cost per attribute
    // a = (N + a²) of that resource, where N = current owned troll count.
    static void applyTrain(GameState& s,
                           const vector<Action>& myA,
                           const vector<Action>& oppA) {
        auto applyForPlayer = [&](int player, const vector<Action>& acts) {
            for (const auto& a : acts) {
                if (a.kind != ActionKind::TRAIN) continue;
                // Compute existing troll count
                int N = 0;
                for (const auto& t : s.trolls) if (t.owner == player) N++;
                const int mv = a.trainAttrs[0];
                const int cap = a.trainAttrs[1];
                const int hv = a.trainAttrs[2];
                const int ch = a.trainAttrs[3];
                const int costP = N + mv * mv;
                const int costL = N + cap * cap;
                const int costA = N + hv * hv;
                const int costI = N + ch * ch;
                auto& sh = s.shack[player];
                if (sh.r[(int)FruitType::PLUM]  < costP ||
                    sh.r[(int)FruitType::LEMON] < costL ||
                    sh.r[(int)FruitType::APPLE] < costA ||
                    sh[ResourceType::IRON]      < costI) continue;
                // Check the spawn cell (shack) isn't blocked by an own troll.
                const int sx = s.shackPos[player][0];
                const int sy = s.shackPos[player][1];
                bool blocked = false;
                for (const auto& t : s.trolls) {
                    if (t.owner == player && t.x == sx && t.y == sy) {
                        blocked = true; break;
                    }
                }
                if (blocked) continue;
                // Deduct cost
                sh.r[(int)FruitType::PLUM]  -= costP;
                sh.r[(int)FruitType::LEMON] -= costL;
                sh.r[(int)FruitType::APPLE] -= costA;
                sh[ResourceType::IRON]      -= costI;
                // Spawn new troll. Pick an id higher than any existing one.
                int maxId = -1;
                for (const auto& t : s.trolls) maxId = std::max(maxId, t.id);
                Troll nt;
                nt.id = maxId + 1;
                nt.owner = player;
                nt.x = sx; nt.y = sy;
                nt.movSpeed = mv;
                nt.carryCap = cap;
                nt.harvest = hv;
                nt.chop = ch;
                s.trolls.push_back(nt);
                break;  // only one train per player per turn
            }
        };
        applyForPlayer(0, myA);
        applyForPlayer(1, oppA);
    }

    // Phase 7: Drop
    static void applyDrop(GameState& s, const vector<Action>& actions) {
        for (auto& t : s.trolls) {
            const Action* d = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::DROP && a.trollId == t.id) { d = &a; break; }
            }
            if (!d) continue;
            const int sx = s.shackPos[t.owner][0];
            const int sy = s.shackPos[t.owner][1];
            if (!isAdj4(t.x, t.y, sx, sy)) continue;
            for (int i = 0; i < RES_COUNT; i++) {
                s.shack[t.owner].r[i] += t.carry.r[i];
                t.carry.r[i] = 0;
            }
        }
    }

    // Phase 8: Mine
    static void applyMine(GameState& s, const vector<Action>& actions) {
        for (auto& t : s.trolls) {
            const Action* m = nullptr;
            for (const auto& a : actions) {
                if (a.kind == ActionKind::MINE && a.trollId == t.id) { m = &a; break; }
            }
            if (!m) continue;
            if (t.chop <= 0) continue;
            // Must be 4-adjacent to an IRON tile.
            bool adj = false;
            static const int DX[4] = {1,-1,0,0};
            static const int DY[4] = {0,0,1,-1};
            for (int k = 0; k < 4; k++) {
                int nx = t.x + DX[k], ny = t.y + DY[k];
                if (s.map.inBounds(nx, ny) && s.map.at(nx, ny) == Terrain::IRON_ORE) {
                    adj = true; break;
                }
            }
            if (!adj) continue;
            int freeCap = t.carryCap - t.carry.total();
            int gain = std::min(t.chop, freeCap);
            if (gain <= 0) continue;
            t.carry.r[(int)ResourceType::IRON] += gain;
        }
    }

    // Phase 9: Grow trees
    static void applyGrow(GameState& s) {
        for (auto& tr : s.trees) {
            if (tr.cooldown > 0) {
                tr.cooldown -= 1;
                if (tr.cooldown == 0) {
                    // Action: grow size or produce fruit.
                    const bool nearWater = isWaterAdjacent(s.map, tr.x, tr.y);
                    if (tr.size < 4) {
                        int oldMax = treeMaxHP(tr.type, tr.size);
                        tr.size += 1;
                        int newMax = treeMaxHP(tr.type, tr.size);
                        tr.health += (newMax - oldMax);
                    } else if (tr.fruits < 3) {
                        tr.fruits += 1;
                    }
                    tr.cooldown = treeCooldown(tr.type, nearWater);
                }
            }
        }
    }
};

// =============================================================
// Layer 7 — Macro Actions (Phase B2)
// =============================================================
//
// A MacroAction is a multi-turn intention: "walk to (x,y) and then
// HARVEST/CHOP/PLANT/PICK/DROP/MINE", plus the two team-level
// primitives TRAIN and WAIT. Each macro emits one primitive Action
// per turn until completion.
//
// The macro layer is additive — FarmStrategy keeps running as the
// live brain. These types feed the DUCT-MCTS search landing in B3.

enum class MacroKind : uint8_t {
    NONE,
    MOVE_AND_HARVEST,   // walk to (x,y) then HARVEST once
    MOVE_AND_CHOP,      // walk to (x,y) then CHOP until the tree dies
    MOVE_AND_PLANT,     // walk to (x,y) then PLANT `fruit` (consumed from carry)
    MOVE_AND_PICK,      // walk to (x,y) [adj-shack] then PICK `fruit` from shack
    MOVE_AND_DROP,      // walk to (x,y) [adj-shack] then DROP all carry
    MOVE_AND_MINE,      // walk to (x,y) [adj-iron] then MINE once
    PRIM_TRAIN,         // team-level — no troll, no movement
    PRIM_WAIT,          // primitive WAIT (a single no-op turn)
};

struct MacroAction {
    MacroKind kind = MacroKind::NONE;
    int trollId = -1;
    int x = 0, y = 0;
    FruitType fruit = FruitType::PLUM;
    int trainAttrs[4] = {0, 0, 0, 0};  // mv, cap, hv, ch — for PRIM_TRAIN
    bool finished = false;
    int stepsTaken = 0;

    // Factory functions ------------------------------------------------
    static MacroAction MoveAndHarvest(int id, int x, int y) {
        MacroAction m; m.kind = MacroKind::MOVE_AND_HARVEST;
        m.trollId = id; m.x = x; m.y = y; return m;
    }
    static MacroAction MoveAndChop(int id, int x, int y) {
        MacroAction m; m.kind = MacroKind::MOVE_AND_CHOP;
        m.trollId = id; m.x = x; m.y = y; return m;
    }
    static MacroAction MoveAndPlant(int id, int x, int y, FruitType f) {
        MacroAction m; m.kind = MacroKind::MOVE_AND_PLANT;
        m.trollId = id; m.x = x; m.y = y; m.fruit = f; return m;
    }
    static MacroAction MoveAndPick(int id, int x, int y, FruitType f) {
        MacroAction m; m.kind = MacroKind::MOVE_AND_PICK;
        m.trollId = id; m.x = x; m.y = y; m.fruit = f; return m;
    }
    static MacroAction MoveAndDrop(int id, int x, int y) {
        MacroAction m; m.kind = MacroKind::MOVE_AND_DROP;
        m.trollId = id; m.x = x; m.y = y; return m;
    }
    static MacroAction MoveAndMine(int id, int x, int y) {
        MacroAction m; m.kind = MacroKind::MOVE_AND_MINE;
        m.trollId = id; m.x = x; m.y = y; return m;
    }
    static MacroAction Train(int mv, int cap, int hv, int ch) {
        MacroAction m; m.kind = MacroKind::PRIM_TRAIN;
        m.trainAttrs[0] = mv; m.trainAttrs[1] = cap;
        m.trainAttrs[2] = hv; m.trainAttrs[3] = ch; return m;
    }
    static MacroAction Wait() {
        MacroAction m; m.kind = MacroKind::PRIM_WAIT; return m;
    }
};

// Safety cap on how many primitives a single macro will emit before
// being forced to finish. Sized to accommodate a MOVE_AND_CHOP of a
// max-HP tree at chop=1 plus the walk to it.
[[maybe_unused]] constexpr int kMaxMacroTurns = 40;

// Emit one primitive Action for this macro this turn. Marks `finished`
// for all kinds EXCEPT MOVE_AND_CHOP, which is finalized in stepMacros
// after the Simulator step (we can only confirm tree death post-step).
[[maybe_unused]] static Action macroPrimitive(const GameState& gs, MacroAction& m) {
    if (m.finished) return Action::Wait();  // defensive

    if (m.kind == MacroKind::PRIM_TRAIN) {
        m.finished = true;
        return Action::Train(m.trainAttrs[0], m.trainAttrs[1],
                             m.trainAttrs[2], m.trainAttrs[3]);
    }
    if (m.kind == MacroKind::PRIM_WAIT) {
        m.finished = true;
        return Action::Wait();
    }

    int idx = trollIndexById(gs, m.trollId);
    if (idx < 0) { m.finished = true; return Action::Wait(); }
    const Troll& t = gs.trolls[idx];

    // Still in transit — emit MOVE toward target. The simulator's MOVE
    // handler steps the troll up to movSpeed cells along a BFS-optimal
    // path. We keep emitting the same target each turn until arrival.
    if (t.x != m.x || t.y != m.y) {
        return Action::Move(m.trollId, m.x, m.y);
    }

    // At target — emit terminal primitive.
    switch (m.kind) {
        case MacroKind::MOVE_AND_HARVEST:
            m.finished = true;
            return Action::Harvest(m.trollId);
        case MacroKind::MOVE_AND_CHOP:
            // Don't mark finished here — wait for post-step tree check.
            return Action::Chop(m.trollId);
        case MacroKind::MOVE_AND_PLANT:
            m.finished = true;
            return Action::Plant(m.trollId, m.fruit);
        case MacroKind::MOVE_AND_PICK:
            m.finished = true;
            return Action::Pick(m.trollId, m.fruit);
        case MacroKind::MOVE_AND_DROP:
            m.finished = true;
            return Action::Drop(m.trollId);
        case MacroKind::MOVE_AND_MINE:
            m.finished = true;
            return Action::Mine(m.trollId);
        default:
            m.finished = true;
            return Action::Wait();
    }
}

// Step the simulator under a set of macros (both sides), looping until
// AT LEAST ONE macro that was active at entry has just finished. This
// is the MCTS search granularity: every node in the tree corresponds
// to a fresh joint-macro decision, so we stop as soon as one decision
// completes.
//
// Pre-check: a MOVE_AND_CHOP whose target tree is already gone
// finishes immediately. Same applies to PLANT/PICK/DROP/MINE if the
// pre-conditions can't be met, but those edge cases are rare and
// handled by the Simulator silently no-op-ing.
//
// Returns the new state; macros are mutated in-place to reflect their
// completion status (`finished = true` for done).
[[maybe_unused]] static GameState stepMacros(GameState gs,
                                             vector<MacroAction>& myMacros,
                                             vector<MacroAction>& oppMacros) {
    // Pre-check: dead-tree MOVE_AND_CHOP. The Simulator wouldn't
    // crash on this — chop on no-tree is a no-op — but marking
    // finished early lets the search recognise that this macro can't
    // make progress, so it doesn't waste a search depth on it.
    auto markDeadChops = [&](vector<MacroAction>& macros) {
        for (auto& m : macros) {
            if (m.kind != MacroKind::MOVE_AND_CHOP || m.finished) continue;
            bool alive = false;
            for (const auto& tr : gs.trees) {
                if (tr.x == m.x && tr.y == m.y && tr.health > 0) {
                    alive = true; break;
                }
            }
            if (!alive) m.finished = true;
        }
    };
    markDeadChops(myMacros);
    markDeadChops(oppMacros);

    // Snapshot which macros were active at entry. We stop as soon as
    // any of those becomes finished — pre-finished ones don't count.
    const int Nm = (int)myMacros.size();
    const int No = (int)oppMacros.size();
    vector<bool> myActiveAtEntry(Nm), oppActiveAtEntry(No);
    for (int i = 0; i < Nm; i++) myActiveAtEntry[i] = !myMacros[i].finished;
    for (int i = 0; i < No; i++) oppActiveAtEntry[i] = !oppMacros[i].finished;

    // If nothing was active to begin with, advance one turn with an
    // empty action set so the simulator still ticks (tree growth etc.).
    bool anyActive = false;
    for (int i = 0; i < Nm; i++) if (myActiveAtEntry[i]) { anyActive = true; break; }
    if (!anyActive)
        for (int i = 0; i < No; i++) if (oppActiveAtEntry[i]) { anyActive = true; break; }
    if (!anyActive) {
        return Simulator::step(gs, {}, {});
    }

    for (int turn = 0; turn < kMaxMacroTurns; turn++) {
        // Build per-side primitive lists from currently-unfinished macros.
        vector<Action> myPrim, oppPrim;
        myPrim.reserve(Nm);
        oppPrim.reserve(No);
        for (auto& m : myMacros) {
            if (m.finished) continue;
            m.stepsTaken++;
            if (m.stepsTaken >= kMaxMacroTurns) {
                m.finished = true;
                continue;
            }
            myPrim.push_back(macroPrimitive(gs, m));
        }
        for (auto& m : oppMacros) {
            if (m.finished) continue;
            m.stepsTaken++;
            if (m.stepsTaken >= kMaxMacroTurns) {
                m.finished = true;
                continue;
            }
            oppPrim.push_back(macroPrimitive(gs, m));
        }

        // Advance one game turn.
        gs = Simulator::step(gs, myPrim, oppPrim);

        // Post-step: finalize MOVE_AND_CHOP based on tree state.
        auto finalizeChops = [&](vector<MacroAction>& macros) {
            for (auto& m : macros) {
                if (m.finished || m.kind != MacroKind::MOVE_AND_CHOP) continue;
                bool alive = false;
                for (const auto& tr : gs.trees) {
                    if (tr.x == m.x && tr.y == m.y && tr.health > 0) {
                        alive = true; break;
                    }
                }
                if (!alive) m.finished = true;
            }
        };
        finalizeChops(myMacros);
        finalizeChops(oppMacros);

        // Stop as soon as any macro that was active at entry has finished.
        bool anyDone = false;
        for (int i = 0; i < Nm && !anyDone; i++)
            if (myActiveAtEntry[i] && myMacros[i].finished) anyDone = true;
        for (int i = 0; i < No && !anyDone; i++)
            if (oppActiveAtEntry[i] && oppMacros[i].finished) anyDone = true;
        if (anyDone) return gs;
    }

    return gs;
}

// Generate a candidate macro list for one troll on the current state.
// This is the seed set for MCTS expansion — the search picks one of
// these as the troll's commitment for the next macro block.
//
// Lean baseline: emits the obvious moves per role. We'll refine in
// B3 to mirror the rival's per-phase prioritisation (chop fodder
// PICK order, etc.). For now this is enough to exercise the macro
// pipeline end-to-end in tests.
[[maybe_unused]] static vector<MacroAction> generateTrollMacroActions(
    const GameState& gs, const Troll& t)
{
    vector<MacroAction> out;
    const int sx = gs.shackPos[t.owner][0];
    const int sy = gs.shackPos[t.owner][1];
    const int free = t.carryCap - t.carry.total();

    // MOVE_AND_HARVEST — nearest tree of each fruit type with fruits.
    if (t.harvest > 0 && free > 0) {
        for (int f = 0; f < FRUIT_COUNT; f++) {
            int bestD = -1, bestIdx = -1;
            for (size_t i = 0; i < gs.trees.size(); i++) {
                const Tree& tr = gs.trees[i];
                if ((int)tr.type != f) continue;
                if (tr.fruits <= 0) continue;
                int d = g_lookupsReady
                    ? bfs_dist_lookup[sy][sx][tr.y][tr.x]
                    : manhattan(sx, sy, tr.x, tr.y);
                if (d < 0) continue;
                if (bestD == -1 || d < bestD) { bestD = d; bestIdx = (int)i; }
            }
            if (bestIdx >= 0) {
                out.push_back(MacroAction::MoveAndHarvest(
                    t.id, gs.trees[bestIdx].x, gs.trees[bestIdx].y));
            }
        }
    }

    // MOVE_AND_CHOP — nearest live tree (any type).
    if (t.chop > 0 && free > 0) {
        int bestD = -1, bestIdx = -1;
        for (size_t i = 0; i < gs.trees.size(); i++) {
            const Tree& tr = gs.trees[i];
            if (tr.health <= 0) continue;
            int d = g_lookupsReady
                ? bfs_dist_lookup[t.y][t.x][tr.y][tr.x]
                : manhattan(t.x, t.y, tr.x, tr.y);
            if (d < 0) continue;
            if (bestD == -1 || d < bestD) { bestD = d; bestIdx = (int)i; }
        }
        if (bestIdx >= 0) {
            out.push_back(MacroAction::MoveAndChop(
                t.id, gs.trees[bestIdx].x, gs.trees[bestIdx].y));
        }
    }

    // MOVE_AND_PLANT — for each fruit carried, nearest free plantable
    // cell (BFS dist ≤ 2 of own shack, no tree on it).
    if (g_lookupsReady && (t.carry.r[0] > 0 || t.carry.r[1] > 0
                        || t.carry.r[2] > 0 || t.carry.r[3] > 0)) {
        int bestD = -1, bestX = -1, bestY = -1;
        for (const auto& [cx, cy] : plantableCellsAroundShack[t.owner]) {
            // skip occupied cells
            bool occupied = false;
            for (const auto& tr : gs.trees) {
                if (tr.x == cx && tr.y == cy) { occupied = true; break; }
            }
            if (occupied) continue;
            int d = bfs_dist_lookup[t.y][t.x][cy][cx];
            if (d < 0) continue;
            if (bestD == -1 || d < bestD) { bestD = d; bestX = cx; bestY = cy; }
        }
        if (bestX >= 0) {
            for (int f = 0; f < FRUIT_COUNT; f++) {
                if (t.carry.r[f] > 0)
                    out.push_back(MacroAction::MoveAndPlant(
                        t.id, bestX, bestY, (FruitType)f));
            }
        }
    }

    // MOVE_AND_PICK / MOVE_AND_DROP — nearest shack-adjacent walkable cell.
    if (g_lookupsReady) {
        int bestD = -1, bestX = -1, bestY = -1;
        for (const auto& [cx, cy] : shackAdjCells[t.owner]) {
            int d = bfs_dist_lookup[t.y][t.x][cy][cx];
            if (d < 0) continue;
            if (bestD == -1 || d < bestD) { bestD = d; bestX = cx; bestY = cy; }
        }
        if (bestX >= 0) {
            // PICK: only if empty-handed and shack has the fruit.
            if (t.carry.total() == 0) {
                for (int f = 0; f < FRUIT_COUNT; f++) {
                    if (gs.shack[t.owner].r[f] > 0)
                        out.push_back(MacroAction::MoveAndPick(
                            t.id, bestX, bestY, (FruitType)f));
                }
            }
            // DROP: only if carrying something.
            if (t.carry.total() > 0)
                out.push_back(MacroAction::MoveAndDrop(t.id, bestX, bestY));
        }
    }

    // MOVE_AND_MINE — nearest mine-adjacent walkable cell.
    if (g_lookupsReady && t.chop > 0 && free > 0) {
        int bestD = -1, bestX = -1, bestY = -1;
        for (const auto& [cx, cy] : mineAdjCells) {
            int d = bfs_dist_lookup[t.y][t.x][cy][cx];
            if (d < 0) continue;
            if (bestD == -1 || d < bestD) { bestD = d; bestX = cx; bestY = cy; }
        }
        if (bestX >= 0) {
            out.push_back(MacroAction::MoveAndMine(t.id, bestX, bestY));
        }
    }

    // Always include WAIT as a fallback — search needs at least one
    // option per troll, even on edge-case states where no other macro
    // is generable.
    if (out.empty()) out.push_back(MacroAction::Wait());

    return out;
}

// =============================================================
// Layer 7b — DUCT-MCTS (Phase B3)
// =============================================================
//
// Decoupled UCT over the macro-action layer. Instead of enumerating
// the joint cartesian product of per-troll macros as children, we
// keep per-troll bandits and lazily allocate child nodes keyed by
// the packed joint action.
//
// Heuristic (lean for B3) — game points + per-troll bonus + train-
// readiness. Top-K trees and chop-progress shaping land in Phase B5.
//
// This is additive: FarmStrategy stays the live brain in main(). The
// new MctsStrategy class compiles but isn't wired into main() until
// we've verified it side-by-side.

namespace mcts {

// ----- heuristic (Phase B5) ----------------------------------------

[[maybe_unused]] static int g_rootTurn = 0;

// Phase flags mirroring the rival's two-flag model. We're "still
// training" until we have 4 my-trolls AND turn < 220. We're "chop
// phase" once training is done OR turn ≥ 200. These overlap by 20
// turns (200-220) — both flags can be true together, allowing the
// bot to chop while still trying to train one more troll.
[[maybe_unused]] static constexpr int kChopPhaseStart = 200;
[[maybe_unused]] static constexpr int kTrainingEnd    = 220;

[[maybe_unused]] static bool shouldTrainMoreTroll(const GameState& s) {
    int myCount = 0;
    for (const Troll& t : s.trolls) if (t.owner == 0) myCount++;
    return myCount < 4 && s.turn < kTrainingEnd;
}
[[maybe_unused]] static bool isChopPhase(const GameState& s) {
    return !shouldTrainMoreTroll(s) || s.turn >= kChopPhaseStart;
}

// Tree max-HP table (matches Simulator's kTreeHP). Used for the chop-
// progress reward in the chop-phase branch of the heuristic.
[[maybe_unused]] static int treeMaxHpFor(FruitType type, int size) {
    static constexpr int hp[4][4] = {
        {  6,  8, 10, 12 },   // PLUM
        {  6,  8, 10, 12 },   // LEMON
        { 11, 14, 17, 20 },   // APPLE
        {  3,  4,  5,  6 },   // BANANA
    };
    int s = std::max(1, std::min(4, size)) - 1;
    return hp[(int)type][s];
}

// Multi-component heuristic. All weights are in raw-game-score units so
// the trunk (fruits + 4*wood) and the shaping terms compose naturally.
//
// Components:
//   Trunk:                fruits (banana capped at 20) + 4*wood
//   Per-troll bonus:      +30 each (≈ "future score this troll brings")
//   Carry-in-flight:      +2 per wood (drop later realises +4 → net +2 gain)
//   Train-readiness:      0..30 linear toward tier-A cost (only pre-chop)
//   Top-K=2 tree prox:    pre-chop: +(treeValue/dist) for top-2 PLUM/LEMON/
//                         APPLE trees per side. treeValue=60 if water-adj
//                         else 40. Banana excluded (chop-only).
//   Chop-progress reward: chop-phase: per-tree +(1.5 + bananaBonus + prog)
//                         / dist where prog = (max_hp - hp)/max_hp.
//                         Encourages finishing partial chops.
//   Opp decay:            opp side gets +0.1/elapsed-turn penalty to
//                         discourage plans that assume idle opp.
[[maybe_unused]] static double heuristic(const GameState& s) {
    const bool chopPhase   = isChopPhase(s);
    const bool stillTrain  = shouldTrainMoreTroll(s);

    auto sideScore = [&](int p) -> double {
        const Inventory& inv = s.shack[p];
        // Trunk: fruits with banana capped at 20 (rival's pattern), +4*wood.
        double pts = inv[ResourceType::PLUM]
                   + inv[ResourceType::LEMON]
                   + inv[ResourceType::APPLE]
                   + std::min(20, inv[ResourceType::BANANA])
                   + 4.0 * inv[ResourceType::WOOD];

        int trollCount = 0;
        for (const Troll& t : s.trolls) {
            if (t.owner != p) continue;
            trollCount++;
            pts += 50.0;  // per-troll bonus — drives MCTS to value training
            // Carry-in-flight: only wood (the PICK-side asymmetry experiment
            // in v-farm-strategy-stable showed crediting carried fruits
            // discourages PICK-to-plant flows).
            pts += 2.0 * t.carry[ResourceType::WOOD];
        }

        // Train-readiness 0..30: linear ratio of how close we are to the
        // next tier-A train (cost N+a² with a=2 → N+4 per fruit/iron).
        // Only meaningful while we're still trying to train.
        if (stillTrain && trollCount < 4) {
            const double cost = trollCount + 4.0;
            double ready = (std::min((double)inv[ResourceType::PLUM],  cost)
                          + std::min((double)inv[ResourceType::LEMON], cost)
                          + std::min((double)inv[ResourceType::APPLE], cost)
                          + std::min((double)inv[ResourceType::IRON],  cost))
                         / (4.0 * cost);
            pts += 40.0 * ready;
        }
        return pts;
    };

    double my = sideScore(0);
    double op = sideScore(1);

    // Tree-proximity bonuses. Two modes depending on chop phase.
    if (!chopPhase) {
        // Pre-chop: reward top-2 nearest PLUM/LEMON/APPLE trees per side
        // (banana excluded — it's chop fodder, not train fuel). Tree
        // value is doubled (40→60) for water-adjacent trees because they
        // grow ~3× faster, producing more harvest cycles.
        constexpr int NUM_FRUITS = 3;
        constexpr int TOP_K = 2;
        const FruitType kinds[NUM_FRUITS] = {FruitType::PLUM, FruitType::LEMON, FruitType::APPLE};

        double bestMy[NUM_FRUITS][TOP_K] = {{0,0},{0,0},{0,0}};
        double bestOp[NUM_FRUITS][TOP_K] = {{0,0},{0,0},{0,0}};
        auto insertTopK = [](double (&top)[TOP_K], double v) {
            if (v <= top[TOP_K - 1]) return;
            int k = TOP_K - 1;
            while (k > 0 && v > top[k - 1]) { top[k] = top[k - 1]; k--; }
            top[k] = v;
        };

        const int myX = s.shackPos[0][0], myY = s.shackPos[0][1];
        const int opX = s.shackPos[1][0], opY = s.shackPos[1][1];
        for (const Tree& tr : s.trees) {
            int idx = -1;
            for (int i = 0; i < NUM_FRUITS; i++)
                if (tr.type == kinds[i]) { idx = i; break; }
            if (idx < 0) continue;

            int dMy = g_lookupsReady
                ? bfs_dist_lookup[myY][myX][tr.y][tr.x]
                : manhattan(myX, myY, tr.x, tr.y);
            int dOp = g_lookupsReady
                ? bfs_dist_lookup[opY][opX][tr.y][tr.x]
                : manhattan(opX, opY, tr.x, tr.y);
            if (dMy <= 0) dMy = 1;
            if (dOp <= 0) dOp = 1;

            const bool water = (g_lookupsReady && nearWaterLookup[tr.y][tr.x]);
            // Reduced from 60/40 (rival's scale) to 25/15 — the rival is
            // on a /4000 scale, we're raw-game-score. Top-K total budget
            // capped to ~150 (6 slots × 25) so the trunk and per-troll
            // bonus aren't drowned out by static tree-proximity.
            const double treeVal = water ? 25.0 : 15.0;
            insertTopK(bestMy[idx], treeVal / (double)dMy);
            insertTopK(bestOp[idx], treeVal / (double)dOp);
        }
        for (int i = 0; i < NUM_FRUITS; i++)
            for (int k = 0; k < TOP_K; k++) { my += bestMy[i][k]; op += bestOp[i][k]; }
    } else {
        // Chop-phase: per-tree finish-the-chop reward. Banana gets a
        // small bonus because it's the cheapest tree to clear for wood.
        const int myX = s.shackPos[0][0], myY = s.shackPos[0][1];
        const int opX = s.shackPos[1][0], opY = s.shackPos[1][1];
        for (const Tree& tr : s.trees) {
            if (tr.health <= 0) continue;
            int dMy = g_lookupsReady
                ? bfs_dist_lookup[myY][myX][tr.y][tr.x]
                : manhattan(myX, myY, tr.x, tr.y);
            int dOp = g_lookupsReady
                ? bfs_dist_lookup[opY][opX][tr.y][tr.x]
                : manhattan(opX, opY, tr.x, tr.y);
            if (dMy <= 0) dMy = 1;
            if (dOp <= 0) dOp = 1;

            int maxHp = treeMaxHpFor(tr.type, tr.size);
            double progress = (double)(maxHp - tr.health) / (double)std::max(1, maxHp);
            double bananaBonus = (tr.type == FruitType::BANANA) ? 1.0 : 0.0;
            double v = 1.5 + bananaBonus + progress;
            my += v / (double)dMy;
            op += v / (double)dOp;
        }
    }

    // Opp-decay: penalise opp side proportional to simulation depth.
    // Without it MCTS would value plans that depend on opp doing nothing
    // for many turns. Scaled to our raw-game-score units.
    int turnsElapsed = std::max(0, s.turn - g_rootTurn);
    op += 0.1 * turnsElapsed;

    return my - op;
}

// ----- node pool ---------------------------------------------------

constexpr int MAX_NODES = 8192;
constexpr int MAX_TROLLS_PER_SIDE = 8;

struct TrollActionStat {
    int visits = 0;
    double score = 0.0;
};

struct Node {
    GameState state;
    vector<MacroAction> baseMy;   // unfinished my-macros inherited from parent
    vector<MacroAction> baseOpp;  // unfinished opp-macros inherited from parent
    bool initialized = false;
    bool canTrain = false;
    int trainStats[4] = {0, 0, 0, 0};  // mv, cap, hv, ch — set when canTrain

    vector<vector<MacroAction>>     perTrollActions; // [trollIdx][actionIdx]
    vector<vector<TrollActionStat>> perTrollStats;   // [trollIdx][actionIdx]

    vector<pair<uint64_t, Node*>> children;  // jointKey -> child

    int visits = 0;
    double score = 0.0;
};

static Node nodePool[MAX_NODES];
static int nodeCount = 0;

[[maybe_unused]] static Node* allocNode() {
    if (nodeCount >= MAX_NODES - 1) return nullptr;
    Node* n = &nodePool[nodeCount++];
    n->baseMy.clear();
    n->baseOpp.clear();
    n->initialized = false;
    n->canTrain = false;
    n->perTrollActions.clear();
    n->perTrollStats.clear();
    n->children.clear();
    n->visits = 0;
    n->score = 0.0;
    return n;
}

[[maybe_unused]] static void resetNodePool() { nodeCount = 0; }

// ----- train recipe (Phase B6) ------------------------------------
//
// Hardcoded tier sequence mirroring the rival's design — proven to
// produce the cap=3/cap=4 wood-haulers we've been losing to. Avoids
// having MCTS rediscover the optimal train ladder each turn (and
// occasionally getting it wrong, which was a B5 variance source).
//
//   Troll 1 → tier A   (2, 2, 2, 2)
//   Troll 2 → super-S2 (2, 3, 2, 2)  — cap=3 wood hauler
//   Troll 3 → super-Z  (3, 4, 1, 2)  — cap=4, mv=3 late-game finisher
//
// After turn ≥ kChopPhaseStart, the rigid recipe falls back to "best
// affordable per attribute" so we squeeze in one more troll if
// resources are short (chooseBestStatsForLastTroll).

// Count my-trolls (player 0).
[[maybe_unused]] static int myTrollCount(const GameState& s) {
    int n = 0;
    for (const auto& t : s.trolls) if (t.owner == 0) n++;
    return n;
}

// Per-attribute cost: N + a² of (PLUM, LEMON, APPLE, IRON) for
// (mv, cap, hv, ch) respectively.
[[maybe_unused]] static bool canAffordTrain(const GameState& s,
                                             int ms, int cc, int hv, int cp) {
    const int n = myTrollCount(s);
    const Inventory& inv = s.shack[0];
    return inv[ResourceType::PLUM]  >= n + ms*ms
        && inv[ResourceType::LEMON] >= n + cc*cc
        && inv[ResourceType::APPLE] >= n + hv*hv
        && inv[ResourceType::IRON]  >= n + cp*cp;
}

// Fallback for late game: pick the highest affordable value per
// attribute, top-down. Returns false if the result is degenerate
// (no movement, or zero harvest AND zero chop).
[[maybe_unused]] static bool chooseBestStatsForLastTroll(const GameState& s,
                                                          int& ms, int& cc,
                                                          int& hv, int& cp) {
    const int n = myTrollCount(s);
    const Inventory& inv = s.shack[0];

    ms = 0;
    for (int v = 3; v >= 1; v--)
        if (inv[ResourceType::PLUM] >= n + v*v) { ms = v; break; }
    cc = 0;
    for (int v = 4; v >= 1; v--)
        if (inv[ResourceType::LEMON] >= n + v*v) { cc = v; break; }
    hv = 0;
    for (int v = 1; v >= 0; v--)
        if (inv[ResourceType::APPLE] >= n + v*v) { hv = v; break; }
    cp = 0;
    for (int v = 3; v >= 1; v--)
        if (inv[ResourceType::IRON] >= n + v*v) { cp = v; break; }

    if (ms == 0 || cc == 0 || (hv == 0 && cp == 0)) return false;
    return true;
}

// Resolve next-train stats based on current state. Sets ms/cc/hv/cp
// and returns true iff a train is wanted AND affordable. Late game
// (turn ≥ kChopPhaseStart) falls back to per-attribute best.
[[maybe_unused]] static bool nextTrollTrainStats(const GameState& s,
                                                  int& ms, int& cc,
                                                  int& hv, int& cp) {
    if (!shouldTrainMoreTroll(s)) return false;

    if (s.turn >= kChopPhaseStart) {
        if (!chooseBestStatsForLastTroll(s, ms, cc, hv, cp)) return false;
        return canAffordTrain(s, ms, cc, hv, cp);
    }

    const int n = myTrollCount(s);
    switch (n) {
        case 1: ms = 2; cc = 2; hv = 2; cp = 2; break;
        case 2: ms = 2; cc = 3; hv = 2; cp = 2; break;
        case 3: ms = 3; cc = 4; hv = 1; cp = 2; break;
        default: return false;
    }
    return canAffordTrain(s, ms, cc, hv, cp);
}

// ----- joint-action machinery -------------------------------------

// Pack each troll's candidate-action index into 8 bits.
[[maybe_unused]] static uint64_t packJointKey(const vector<int>& idx) {
    uint64_t key = 0;
    for (size_t t = 0; t < idx.size(); t++)
        key |= ((uint64_t)(idx[t] & 0xFF)) << (t * 8);
    return key;
}

// Two macros conflict if both target the same cell with a "targeted"
// macro type (HARVEST/CHOP/PLANT/MINE). PICK/DROP are intentionally
// allowed to coexist because multiple shack-adj cells are valid drop
// points.
[[maybe_unused]] static bool isTargetedMacro(const MacroAction& m) {
    switch (m.kind) {
        case MacroKind::MOVE_AND_HARVEST:
        case MacroKind::MOVE_AND_CHOP:
        case MacroKind::MOVE_AND_PLANT:
        case MacroKind::MOVE_AND_MINE:
            return true;
        default:
            return false;
    }
}
[[maybe_unused]] static bool conflictsWith(const MacroAction& a, const MacroAction& b) {
    if (!isTargetedMacro(a) || !isTargetedMacro(b)) return false;
    return a.x == b.x && a.y == b.y;
}

// Look up an existing child by packed joint key.
[[maybe_unused]] static Node* findChild(const Node* n, uint64_t key) {
    for (const auto& p : n->children)
        if (p.first == key) return p.second;
    return nullptr;
}

// Lazily build per-troll candidate lists + bandit slots on first visit.
[[maybe_unused]] static void initializeNode(Node* node) {
    // Only generate candidates for OUR trolls (player 0). Opp is modeled
    // as static for now — we don't simulate their decisions in this
    // single-player MCTS variant. The decay term in heuristic absorbs
    // the modeling error.
    node->perTrollActions.clear();
    node->perTrollStats.clear();
    int myTrolls = 0;
    for (const auto& t : node->state.trolls) if (t.owner == 0) myTrolls++;

    node->perTrollActions.reserve(myTrolls);
    node->perTrollStats.reserve(myTrolls);
    for (const auto& t : node->state.trolls) {
        if (t.owner != 0) continue;
        // Honour any kept-macro from parent (a macro still in progress).
        const MacroAction* kept = nullptr;
        for (const auto& m : node->baseMy)
            if (m.trollId == t.id && !m.finished) { kept = &m; break; }
        if (kept) {
            node->perTrollActions.push_back({*kept});
        } else {
            node->perTrollActions.push_back(generateTrollMacroActions(node->state, t));
        }
        node->perTrollStats.emplace_back();
        node->perTrollStats.back().assign(node->perTrollActions.back().size(), {});
    }

    // Resolve train stats via the recipe (B6). Stores ms/cc/hv/cp for
    // jointToPlan to consume; canTrain flips iff the recipe both wants
    // a train AND we can afford it.
    int ms, cc, hv, cp;
    if (nextTrollTrainStats(node->state, ms, cc, hv, cp)) {
        node->canTrain = true;
        node->trainStats[0] = ms;
        node->trainStats[1] = cc;
        node->trainStats[2] = hv;
        node->trainStats[3] = cp;
    } else {
        node->canTrain = false;
    }

    node->initialized = true;
}

// DUCT selection: per-troll bandits, masking out conflicting picks.
[[maybe_unused]] static void selectJointAction(Node* node,
                                               vector<int>& outIdx,
                                               uint64_t& outKey) {
    int M = (int)node->perTrollActions.size();
    outIdx.assign(M, 0);
    outKey = 0;
    if (M == 0) return;

    double logN = node->visits > 0 ? std::log((double)node->visits) : 0.0;
    constexpr double UCT_C = 1.41421356;

    vector<int> chosenSoFar;
    chosenSoFar.reserve(M);
    for (int t = 0; t < M; t++) {
        const auto& acts = node->perTrollActions[t];
        const auto& sts  = node->perTrollStats[t];
        int K = (int)acts.size();

        // Admissible: doesn't target-conflict with earlier picks.
        vector<int> admissible;
        admissible.reserve(K);
        for (int i = 0; i < K; i++) {
            bool ok = true;
            for (int prevT = 0; prevT < t; prevT++) {
                if (conflictsWith(acts[i],
                                  node->perTrollActions[prevT][outIdx[prevT]])) {
                    ok = false; break;
                }
            }
            if (ok) admissible.push_back(i);
        }
        if (admissible.empty()) for (int i = 0; i < K; i++) admissible.push_back(i);

        // Untried first.
        vector<int> untried;
        for (int i : admissible) if (sts[i].visits == 0) untried.push_back(i);

        int pick;
        if (!untried.empty()) {
            pick = untried[rand() % untried.size()];
        } else {
            int best = admissible[0];
            double bestUct = -1e30;
            for (int i : admissible) {
                double exploit = sts[i].score / (double)sts[i].visits;
                double explore = UCT_C * std::sqrt(logN / (double)sts[i].visits);
                double uct = exploit + explore;
                if (uct > bestUct) { bestUct = uct; best = i; }
            }
            pick = best;
        }

        outIdx[t] = pick;
        outKey |= ((uint64_t)(pick & 0xFF)) << (t * 8);
    }
}

// Build the full macro plan (per-troll picks + optional TRAIN).
[[maybe_unused]] static void jointToPlan(Node* node,
                                         const vector<int>& idx,
                                         vector<MacroAction>& outMy) {
    outMy.clear();
    outMy.reserve(idx.size() + 1);
    for (size_t t = 0; t < idx.size(); t++)
        outMy.push_back(node->perTrollActions[t][idx[t]]);
    if (node->canTrain)
        outMy.push_back(MacroAction::Train(
            node->trainStats[0], node->trainStats[1],
            node->trainStats[2], node->trainStats[3]));
}

// Apply a joint action via stepMacros and return (or create) the child.
[[maybe_unused]] static Node* expand(Node* node, const vector<int>& idx, uint64_t key) {
    if (Node* existing = findChild(node, key)) return existing;

    Node* child = allocNode();
    if (!child) return node;  // pool exhausted — re-evaluate at this node

    child->state = node->state;
    // Build my plan (with optional train) and opp's empty plan.
    vector<MacroAction> myPlan, oppPlan;
    jointToPlan(node, idx, myPlan);
    child->state = stepMacros(child->state, myPlan, oppPlan);

    // Carry unfinished macros forward into the child's base.
    for (const auto& m : myPlan) if (!m.finished) child->baseMy.push_back(m);
    for (const auto& m : oppPlan) if (!m.finished) child->baseOpp.push_back(m);

    node->children.emplace_back(key, child);
    return child;
}

// Is this state terminal for scoring purposes?
[[maybe_unused]] static bool isEndGame(const GameState& s) {
    return s.turn >= 300;
}

// One MCTS iteration: select -> expand -> evaluate -> backprop.
[[maybe_unused]] static double mctsIter(Node* node) {
    if (isEndGame(node->state)) {
        double v = heuristic(node->state);
        node->visits++;
        node->score += v;
        return v;
    }
    if (!node->initialized) initializeNode(node);
    if (node->perTrollActions.empty()) {
        // No my-trolls. Evaluate.
        double v = heuristic(node->state);
        node->visits++;
        node->score += v;
        return v;
    }

    vector<int> idx;
    uint64_t key;
    selectJointAction(node, idx, key);

    bool newChild = (findChild(node, key) == nullptr);
    Node* child = expand(node, idx, key);
    double childValue;
    if (newChild || isEndGame(child->state)) {
        childValue = heuristic(child->state);
        child->visits++;
        child->score += childValue;
    } else {
        childValue = mctsIter(child);
    }

    // Backprop into per-troll bandits + node total.
    for (size_t t = 0; t < idx.size(); t++) {
        node->perTrollStats[t][idx[t]].visits++;
        node->perTrollStats[t][idx[t]].score += childValue;
    }
    node->visits++;
    node->score += childValue;
    return childValue;
}

// After search, pick the most-visited candidate per troll (ties broken
// by higher avg score), respecting target conflicts greedily by id.
[[maybe_unused]] static vector<int> getBestPerTrollIndices(Node* node) {
    int T = (int)node->perTrollActions.size();
    vector<int> best(T, 0);
    for (int t = 0; t < T; t++) {
        const auto& acts = node->perTrollActions[t];
        const auto& sts  = node->perTrollStats[t];
        int bestIdx = -1, bestVisits = -1;
        double bestAvg = -1e30;
        for (int i = 0; i < (int)sts.size(); i++) {
            // skip if conflicts with already-chosen earlier troll
            bool conflict = false;
            for (int p = 0; p < t; p++)
                if (conflictsWith(acts[i], node->perTrollActions[p][best[p]])) {
                    conflict = true; break;
                }
            if (conflict) continue;
            double avg = sts[i].visits > 0 ? sts[i].score / sts[i].visits : -1e30;
            if (sts[i].visits > bestVisits
                || (sts[i].visits == bestVisits && avg > bestAvg)) {
                bestVisits = sts[i].visits;
                bestAvg = avg;
                bestIdx = i;
            }
        }
        best[t] = bestIdx >= 0 ? bestIdx : 0;
    }
    return best;
}

// ----- cross-turn warm start (Phase B4) ---------------------------
//
// At the end of each turn, snapshot the per-troll bandit stats of the
// child node we committed to. At the start of the next turn, copy
// matching action stats onto the new root's bandits. Matching is by
// stable macro key (kind + target + fruit/train-attrs), so a freshly-
// generated candidate that's the same conceptual macro inherits its
// visit count and average score from the prior turn's search.
//
// Effect: ~2× effective iterations from the same budget, because the
// second turn doesn't start every bandit at visits=0.

struct TrollPriorSnapshot {
    int trollId;
    vector<MacroAction>     actions;
    vector<TrollActionStat> stats;
};
[[maybe_unused]] static vector<TrollPriorSnapshot> g_priorTrolls;
[[maybe_unused]] static bool g_priorAvailable = false;

// Stable equality for matching macros across turns. Ignores trollId
// (the troll context is implicit — these macros are stored in the
// bandit for a specific troll). Ignores `finished` and `stepsTaken`
// (those are runtime state, not identity).
[[maybe_unused]] static bool macrosMatch(const MacroAction& a, const MacroAction& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case MacroKind::PRIM_TRAIN:
            return a.trainAttrs[0] == b.trainAttrs[0]
                && a.trainAttrs[1] == b.trainAttrs[1]
                && a.trainAttrs[2] == b.trainAttrs[2]
                && a.trainAttrs[3] == b.trainAttrs[3];
        case MacroKind::PRIM_WAIT:
        case MacroKind::NONE:
            return true;
        case MacroKind::MOVE_AND_PLANT:
        case MacroKind::MOVE_AND_PICK:
            return a.x == b.x && a.y == b.y && a.fruit == b.fruit;
        default:
            return a.x == b.x && a.y == b.y;
    }
}

// Save the root's per-troll bandit stats at end of turn. We snapshot
// the ROOT (not the committed child) because:
//   1. The root is always initialized (initializeNode runs at the start
//      of runMCTS), so we always have non-empty data to save.
//   2. The root's state matches what NEXT turn's root will be (modulo
//      one CodinGame primitive's worth of advancement) — so the per-
//      troll candidate macros tend to overlap, making macrosMatch
//      productive.
//   3. The committed-child's state is ~5-10 game turns ahead (multi-
//      turn macros), so its candidate macros would target stale cells.
// Indexed by the troll id so it survives troll-count changes between
// turns (a fresh train spawning a new troll has no prior).
[[maybe_unused]] static void snapshotRootBandits(Node* root,
                                                 const vector<int>& /*bestIdx*/) {
    g_priorTrolls.clear();
    g_priorAvailable = false;

    if (!root->initialized) return;

    // Map perTrollActions slot t -> troll id (iteration order matches
    // initializeNode's owner==0 filter).
    vector<int> trollIds;
    for (const auto& t : root->state.trolls)
        if (t.owner == 0) trollIds.push_back(t.id);

    for (size_t t = 0; t < root->perTrollActions.size(); t++) {
        if (t >= trollIds.size()) break;
        TrollPriorSnapshot snap;
        snap.trollId = trollIds[t];
        snap.actions = root->perTrollActions[t];
        snap.stats   = root->perTrollStats[t];
        g_priorTrolls.push_back(std::move(snap));
    }
    g_priorAvailable = !g_priorTrolls.empty();
}

// Copy matching action stats onto the new root's bandits. Seeds
// root->visits so the first UCB selection has a sensible logN term.
// Returns the matched count for debug logging.
[[maybe_unused]] static int applyPriorsToRoot(Node* root) {
    if (!g_priorAvailable || g_priorTrolls.empty()) return 0;
    if (!root->initialized) return 0;

    // Map perTrollActions slot t -> troll id in root state.
    vector<int> trollIds;
    for (const auto& t : root->state.trolls)
        if (t.owner == 0) trollIds.push_back(t.id);

    int maxSum = 0;
    int matched = 0;
    for (size_t t = 0; t < root->perTrollActions.size(); t++) {
        if (t >= trollIds.size()) break;
        int tid = trollIds[t];
        const TrollPriorSnapshot* prior = nullptr;
        for (const auto& p : g_priorTrolls)
            if (p.trollId == tid) { prior = &p; break; }
        if (!prior) continue;  // newly-trained troll has no prior

        const auto& acts = root->perTrollActions[t];
        auto& stats = root->perTrollStats[t];
        int sum = 0;
        for (size_t i = 0; i < acts.size(); i++) {
            for (size_t j = 0; j < prior->actions.size(); j++) {
                if (macrosMatch(acts[i], prior->actions[j])) {
                    stats[i] = prior->stats[j];
                    sum += stats[i].visits;
                    matched++;
                    break;
                }
            }
        }
        if (sum > maxSum) maxSum = sum;
    }
    root->visits = maxSum;
    return matched;
}

// Top-level entry point. Runs MCTS for the configured budget and
// returns the best joint action as a primitive Action list, ready
// to feed to emit().
[[maybe_unused]] static vector<Action> runMCTS(const GameState& rootState,
                                               int budgetMs) {
    using clock = std::chrono::steady_clock;
    g_rootTurn = rootState.turn;
    resetNodePool();

    Node* root = allocNode();
    if (!root) return {};
    root->state = rootState;
    initializeNode(root);

    // Warm-start the bandits from last turn's chosen-child snapshot.
    int reused = applyPriorsToRoot(root);

    auto start = clock::now();
    int iters = 0;
    while (true) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - start).count();
        if (elapsedMs >= budgetMs) break;
        if (nodeCount >= MAX_NODES - 64) break;
        mctsIter(root);
        iters++;
    }

    // Convert best macros into per-troll primitives for emit().
    vector<int> bestIdx = getBestPerTrollIndices(root);
    vector<MacroAction> myPlan;
    jointToPlan(root, bestIdx, myPlan);

    vector<Action> primitives;
    primitives.reserve(myPlan.size());
    for (auto& m : myPlan) {
        Action a = macroPrimitive(rootState, m);
        primitives.push_back(a);
    }

    // Snapshot for next turn's warm start, BEFORE the node pool gets
    // reset on the next runMCTS call (which would invalidate Node* refs).
    snapshotRootBandits(root, bestIdx);

    DBG("mcts: iters=" << iters << " nodes=" << nodeCount
        << " root.visits=" << root->visits
        << " reusedStats=" << reused);
    return primitives;
}

}  // namespace mcts

// MctsStrategy — implements IStrategy by delegating to mcts::runMCTS.
// CodinGame gives 1000ms on turn 1, 50ms after. We leave a margin.
struct MctsStrategy : IStrategy {
    vector<Action> decide(const GameState& gs) override {
        int budget = (gs.turn == 0) ? 950 : 42;
        return mcts::runMCTS(gs, budget);
    }
};

// =============================================================
// Layer 8 — SearchStrategy (lookahead-scored wrapper around FarmStrategy)
// =============================================================
//
// Architecture:
//   FarmStrategy = the policy (proposes default actions per turn).
//   SearchStrategy = wraps FarmStrategy. Each turn it:
//     1. Asks FarmStrategy for a default plan.
//     2. Generates a few variations (e.g. force saboteur to opp shack).
//     3. Simulates each plan LOOKAHEAD turns into the future (turn 1 =
//        the candidate plan, turns 2..N = FarmStrategy as continuation).
//     4. Picks the plan with the highest score delta and emits it.
//
// Why this avoids the eval trap that killed v1 of SearchStrategy:
//   - Eval is PURE score delta (myShack - oppShack), no positional
//     bonuses. There's nothing for an action to "consume".
//   - Lookahead is 3 (not 1). A MOVE→HARVEST→DROP sequence's full
//     score impact (+1 fruit in shack) only shows at turn 3.
//   - Continuation policy is FarmStrategy itself, so identical-from-
//     turn-2 candidates evaluate identically (no false differentiation).
// Flip player 0 ↔ player 1 in a GameState so FarmStrategy can be used
// as a proxy for the opponent's behavior. After this swap, what
// FarmStrategy::decide considers "our trolls" are actually opp's. The
// returned actions are valid moves for opp's trolls in the original
// game state (troll IDs are global).
static GameState swapPerspective(GameState gs) {
    std::swap(gs.shack[0], gs.shack[1]);
    std::swap(gs.shackPos[0][0], gs.shackPos[1][0]);
    std::swap(gs.shackPos[0][1], gs.shackPos[1][1]);
    // Also swap shack-cell terrains so FarmStrategy's `Terrain::SHACK_OWN`
    // checks correspond to the new "our" shack.
    for (int y = 0; y < gs.map.h; y++) {
        for (int x = 0; x < gs.map.w; x++) {
            Terrain c = gs.map.at(x, y);
            if (c == Terrain::SHACK_OWN) gs.map.set(x, y, Terrain::SHACK_OPP);
            else if (c == Terrain::SHACK_OPP) gs.map.set(x, y, Terrain::SHACK_OWN);
        }
    }
    for (auto& t : gs.trolls) t.owner = 1 - t.owner;
    return gs;
}

struct SearchStrategy : IStrategy {
    FarmStrategy policy;
    // MCTS-flavor v1: longer lookahead than pure rule-based; both sides
    // model'd by FarmStrategy (opp via perspective swap) so playouts
    // approximate real arena dynamics rather than assume idle opp.
    // LOOKAHEAD=15 covers the full sabotage round-trip on far maps
    // (walk 7 + chop 4 + walk 7 ≈ 18 turns) on close maps and reaches
    // mid-cycle on far ones.
    static constexpr int LOOKAHEAD = 20;

    // Pure game-score delta. We tried adding rival-style shaping
    // components (troll bonus, train-readiness, top-K tree proximity,
    // carry-in-flight) and every variant regressed the idle-test score
    // (vanilla=159, conservative=151, full rival-style=79). With our
    // 1-troll-deviation search + 20-turn deterministic FarmStrategy
    // continuation, the long tail is dominated by FarmStrategy's own
    // decisions; eval shaping cannot move outcomes because there's
    // only one trajectory per candidate. Shaping requires DUCT-MCTS
    // over macro-actions — until that lands, keep eval = real score.
    static double scoreDelta(const GameState& s) {
        const double my = s.shack[0].totalFruits()
                        + 4.0 * s.shack[0][ResourceType::WOOD];
        const double op = s.shack[1].totalFruits()
                        + 4.0 * s.shack[1][ResourceType::WOOD];
        return my - op;
    }

    // Compute opp's likely actions from current state using FarmStrategy
    // on a perspective-swapped view of the game. This is our proxy for
    // tsukammo's behavior — assumes opp plays a similar farm-based
    // strategy, which is what we observed in arena replays.
    static vector<Action> oppPolicy(const GameState& gs) {
        const bool prevSilent = g_silent_dbg;
        g_silent_dbg = true;
        GameState swapped = swapPerspective(gs);
        FarmStrategy oppPolicyInst;
        vector<Action> actions = oppPolicyInst.decide(swapped);
        g_silent_dbg = prevSilent;
        // After swap, FarmStrategy emitted actions for trolls it considers
        // "owner=0". In the original gs view those are opp's trolls
        // (owner=1). The action's trollId is global, so it's directly
        // usable as opp's action.
        return actions;
    }

    // Simulate plan at turn 1 + FarmStrategy continuation for turns
    // 2..LOOKAHEAD on BOTH sides. Returns score delta at end of sim.
    // The opp model makes scorePlan more realistic than the previous
    // "idle opp" assumption — it accounts for opp harvesting, training,
    // sabotaging our trees, etc.
    //
    // DBG is silenced inside scorePlan: each continuation call invokes
    // FarmStrategy::decide which emits one DBG line per turn. With 30+
    // candidates × LOOKAHEAD turns × 2 sides, that's hundreds of DBG
    // lines per real turn — enough to bust CodinGame's stderr cap and
    // truncate our actual search DBG. Suppressing during sim keeps the
    // arena log readable AND saves the cerr-write time.
    static double scorePlan(GameState gs, const vector<Action>& plan) {
        const bool prevSilent = g_silent_dbg;
        g_silent_dbg = true;

        // Turn 1: us = candidate plan, opp = their policy
        vector<Action> opp1 = oppPolicy(gs);
        gs = Simulator::step(gs, plan, {});

        FarmStrategy cont;
        for (int k = 1; k < LOOKAHEAD; k++) {
            vector<Action> myAct  = cont.decide(gs);
            vector<Action> oppAct = oppPolicy(gs);
            gs = Simulator::step(gs, myAct, oppAct);
        }

        g_silent_dbg = prevSilent;
        return scoreDelta(gs);
    }

    // Replace troll's action in plan in-place. If no existing action for
    // that troll, append. Skips MSG and team-level (TRAIN with trollId=-1).
    static void replaceTrollAction(vector<Action>& plan, int trollId, Action repl) {
        for (auto& a : plan) {
            if (a.trollId == trollId
                && a.kind != ActionKind::WAIT
                && a.kind != ActionKind::MSG) {
                a = repl;
                return;
            }
        }
        plan.push_back(repl);
    }

    // Generate all *plausible* actions for a single troll given current
    // state. The bot doesn't pre-decide what this troll should do — it
    // enumerates every legal option and lets the simulation rank them.
    // This is what makes the bot "flexible": no hardcoded role choice.
    static vector<Action> generateTrollOptions(const GameState& gs, const Troll& t) {
        vector<Action> out;
        const int sx = gs.shackPos[0][0], sy = gs.shackPos[0][1];
        const int ox = gs.shackPos[1][0], oy = gs.shackPos[1][1];
        const int distShack = std::abs(t.x - sx) + std::abs(t.y - sy);
        const int free = t.carryCap - t.carry.total();

        // Tree at troll's cell?
        int onTreeIdx = -1;
        for (size_t i = 0; i < gs.trees.size(); i++) {
            if (gs.trees[i].x == t.x && gs.trees[i].y == t.y) {
                onTreeIdx = (int)i; break;
            }
        }

        // ── In-place actions (HARVEST/CHOP/PLANT/PICK/DROP/MINE) ──
        if (onTreeIdx >= 0) {
            const Tree& tr = gs.trees[onTreeIdx];
            if (tr.fruits > 0 && t.harvest > 0 && free > 0)
                out.push_back(Action::Harvest(t.id));
            if (tr.health > 0 && t.chop > 0 && free > 0)
                out.push_back(Action::Chop(t.id));
        }
        if (distShack == 1 && t.carry.total() > 0)
            out.push_back(Action::Drop(t.id));
        if (distShack == 1 && t.carry.total() == 0) {
            for (int f = 0; f < FRUIT_COUNT; f++) {
                if (gs.shack[0].r[f] > 0)
                    out.push_back(Action::Pick(t.id, (FruitType)f));
            }
        }
        if (onTreeIdx < 0 && gs.map.at(t.x, t.y) == Terrain::GRASS) {
            for (int f = 0; f < FRUIT_COUNT; f++) {
                if (t.carry.r[f] > 0)
                    out.push_back(Action::Plant(t.id, (FruitType)f));
            }
        }
        // MINE if adj-iron
        if (t.chop > 0 && free > 0) {
            static const int DX[4] = {1, -1, 0, 0};
            static const int DY[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = t.x + DX[k], ny = t.y + DY[k];
                if (gs.map.inBounds(nx, ny)
                    && gs.map.at(nx, ny) == Terrain::IRON_ORE) {
                    out.push_back(Action::Mine(t.id));
                    break;
                }
            }
        }

        // ── MOVE candidates (pick context-relevant targets) ──
        //   1. Nearest fruit-bearing tree (any type)
        //   2. Nearest chopable tree (health > 0)
        //   3. Nearest opp-side tree (sabotage)
        //   4. Nearest iron-adjacent grass cell (mining setup)
        //   5. Nearest shack-adjacent grass cell (drop/pick prep)
        auto manh = [](int ax, int ay, int bx, int by) {
            return std::abs(ax-bx) + std::abs(ay-by);
        };

        int bestFruit = -1, bestFruitD = INT32_MAX;
        int bestChop = -1,  bestChopD  = INT32_MAX;
        int bestOpp = -1,   bestOppD   = INT32_MAX;
        for (size_t i = 0; i < gs.trees.size(); i++) {
            const Tree& tr = gs.trees[i];
            if (tr.health <= 0) continue;
            int dt = manh(t.x, t.y, tr.x, tr.y);
            int dopp = manh(tr.x, tr.y, ox, oy);
            int dmy  = manh(tr.x, tr.y, sx, sy);
            if (tr.fruits > 0 && dt < bestFruitD)  { bestFruitD = dt; bestFruit = i; }
            if (dt < bestChopD)                     { bestChopD  = dt; bestChop  = i; }
            if (dopp < dmy && dt < bestOppD)        { bestOppD   = dt; bestOpp   = i; }
        }
        if (bestFruit >= 0)
            out.push_back(Action::Move(t.id, gs.trees[bestFruit].x, gs.trees[bestFruit].y));
        if (bestChop >= 0 && bestChop != bestFruit)
            out.push_back(Action::Move(t.id, gs.trees[bestChop].x, gs.trees[bestChop].y));
        if (bestOpp >= 0 && bestOpp != bestFruit && bestOpp != bestChop)
            out.push_back(Action::Move(t.id, gs.trees[bestOpp].x, gs.trees[bestOpp].y));

        // Iron-adj grass
        {
            int bestX = -1, bestY = -1, bestD = INT32_MAX;
            for (int y = 0; y < gs.map.h; y++) {
                for (int x = 0; x < gs.map.w; x++) {
                    if (gs.map.at(x, y) != Terrain::GRASS) continue;
                    static const int DX[4] = {1, -1, 0, 0};
                    static const int DY[4] = {0, 0, 1, -1};
                    bool adj = false;
                    for (int k = 0; k < 4; k++) {
                        int nx = x + DX[k], ny = y + DY[k];
                        if (gs.map.inBounds(nx, ny)
                            && gs.map.at(nx, ny) == Terrain::IRON_ORE) {
                            adj = true; break;
                        }
                    }
                    if (!adj) continue;
                    int dt = manh(t.x, t.y, x, y);
                    if (dt < bestD) { bestD = dt; bestX = x; bestY = y; }
                }
            }
            if (bestX >= 0)
                out.push_back(Action::Move(t.id, bestX, bestY));
        }
        // Shack-adj grass (for drop/pick prep)
        {
            static const int DX[4] = {1, -1, 0, 0};
            static const int DY[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = sx + DX[k], ny = sy + DY[k];
                if (gs.map.inBounds(nx, ny)
                    && gs.map.at(nx, ny) == Terrain::GRASS) {
                    out.push_back(Action::Move(t.id, nx, ny));
                    break;
                }
            }
        }
        return out;
    }

    vector<Action> decide(const GameState& gs) override {
        // Each turn the bot generates many candidate plans, simulates each
        // LOOKAHEAD turns into the future, picks the highest-scoring.
        //
        // Three safeguards keep the search honest AND within the per-turn
        // time budget:
        //   1. **Test each swap against the DEFAULT plan, not compounded.**
        //      Greedy chains of marginal swaps drift into degenerate plans
        //      (saw `MOVE: 539, HARVEST: 25` with naive compounding).
        //   2. **Hysteresis threshold.** The continuation policy used in
        //      scorePlan (FarmStrategy) is NOT identical to the real-game
        //      policy (SearchStrategy), so small sim wins often don't
        //      materialise. We only accept a swap if it improves the score
        //      by > kSwapThreshold (clear signal, not noise).
        //   3. **Wall-clock deadline.** CodinGame gives 50ms per turn
        //      (1000ms turn 1). We target 42ms to leave I/O margin. When
        //      the deadline approaches we stop adding swap evals and
        //      return the best plan found so far (worst case: the
        //      FarmStrategy default plan, which is always safe to play).
        using clock = std::chrono::steady_clock;
        const auto t0 = clock::now();
        // 950ms first turn (init has 1000ms total), 42ms thereafter.
        const auto budget = std::chrono::milliseconds(gs.turn == 0 ? 950 : 42);
        auto deadlineHit = [&]() {
            return (clock::now() - t0) >= budget;
        };

        vector<Action> defaultPlan = policy.decide(gs);

        // If we're already past budget (rare — only on huge maps), skip
        // the eval entirely and play the safe default.
        if (deadlineHit()) {
            DBG("search: deadline hit before eval, returning default plan");
            return defaultPlan;
        }

        double defaultScore = scorePlan(gs, defaultPlan);

        vector<Action> bestPlan = defaultPlan;
        double bestScore = defaultScore;
        static constexpr double kSwapThreshold = 3.0;
        int evals = 1, swaps = 0;
        bool aborted = false;

        for (const Troll& t : gs.trolls) {
            if (t.owner != 0) continue;
            if (deadlineHit()) { aborted = true; break; }
            vector<Action> opts = generateTrollOptions(gs, t);
            for (const Action& alt : opts) {
                if (deadlineHit()) { aborted = true; break; }
                vector<Action> plan = defaultPlan;
                replaceTrollAction(plan, t.id, alt);
                double s = scorePlan(gs, plan);
                evals++;
                if (s > bestScore + kSwapThreshold) {
                    bestScore = s;
                    bestPlan = plan;
                    swaps++;
                }
            }
            if (aborted) break;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - t0).count();
        DBG("search: " << evals << " evals, " << swaps
            << " swaps, best=" << bestScore
            << " (default=" << defaultScore << ")"
            << " t=" << elapsed << "ms"
            << (aborted ? " ABORTED" : ""));
        return bestPlan;
    }
};

// =============================================================
// Simulator smoke tests (compiled in but only run with `./main test`)
// =============================================================

#define ASSERT_EQ(a, b, label) do { \
    auto _av = (a); auto _bv = (b); \
    if (_av != _bv) { \
        cerr << "FAIL " << label << ": got " << _av << " expected " << _bv << endl; \
        anyFail = true; \
    } else { \
        cerr << "OK   " << label << endl; \
    } \
} while (0)

// static int testSimulator() {
//     bool anyFail = false;

//     // Build a minimal 8x4 map with grass + 2 shacks.
//     GameState gs;
//     gs.map.w = 8; gs.map.h = 4;
//     gs.map.cells.assign(32, Terrain::GRASS);
//     // shack 0 at (1,1), shack 1 at (6,2). + iron at (0,0).
//     gs.map.set(1, 1, Terrain::SHACK_OWN);
//     gs.map.set(6, 2, Terrain::SHACK_OPP);
//     gs.map.set(0, 0, Terrain::IRON_ORE);
//     gs.shackPos[0][0]=1; gs.shackPos[0][1]=1;
//     gs.shackPos[1][0]=6; gs.shackPos[1][1]=2;

//     // T1: Move test
//     {
//         GameState st = gs;
//         Troll t; t.id=0; t.owner=0; t.x=1; t.y=1; t.movSpeed=2; t.carryCap=2;
//                 t.harvest=1; t.chop=1;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Move(0, 4, 1) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ(n.trolls[0].x, 3, "T1.Move target=(4,1), speed=2, ends at (3,1)");
//         ASSERT_EQ(n.trolls[0].y, 1, "T1.Move y");
//         ASSERT_EQ(n.turn, 1, "T1.turn advanced");
//     }

//     // T2: Harvest test
//     {
//         GameState st = gs;
//         Troll t; t.id=0; t.owner=0; t.x=2; t.y=2; t.movSpeed=1; t.carryCap=2;
//                 t.harvest=1; t.chop=0;
//         st.trolls.push_back(t);
//         Tree tr; tr.type=FruitType::APPLE; tr.x=2; tr.y=2; tr.size=4;
//                  tr.health=20; tr.fruits=3; tr.cooldown=9;
//         st.trees.push_back(tr);
//         vector<Action> mine = { Action::Harvest(0) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ(n.trees[0].fruits, 2, "T2.Harvest tree fruits 3→2");
//         ASSERT_EQ(n.trolls[0].carry.r[(int)ResourceType::APPLE], 1, "T2.Harvest troll carry +1 apple");
//     }

//     // T3: Chop test — kill a small tree, collect wood
//     {
//         GameState st = gs;
//         Troll t; t.id=0; t.owner=0; t.x=2; t.y=2; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=0; t.chop=4;
//         st.trolls.push_back(t);
//         Tree tr; tr.type=FruitType::BANANA; tr.x=2; tr.y=2; tr.size=1;
//                  tr.health=3; tr.fruits=0; tr.cooldown=6;
//         st.trees.push_back(tr);
//         vector<Action> mine = { Action::Chop(0) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ((int)n.trees.size(), 0, "T3.Chop killed tree → removed");
//         ASSERT_EQ(n.trolls[0].carry.r[(int)ResourceType::WOOD], 1, "T3.Chop wood=size=1");
//     }

//     // T4: Drop test
//     {
//         GameState st = gs;
//         Troll t; t.id=0; t.owner=0; t.x=2; t.y=1; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=1; t.chop=1;
//         t.carry.r[(int)ResourceType::WOOD] = 3;
//         t.carry.r[(int)ResourceType::APPLE] = 1;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Drop(0) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ(n.shack[0][ResourceType::WOOD], 3, "T4.Drop wood→shack");
//         ASSERT_EQ(n.shack[0][ResourceType::APPLE], 1, "T4.Drop apple→shack");
//         ASSERT_EQ(n.trolls[0].carry.total(), 0, "T4.Drop carry cleared");
//     }

//     // T5: Mine test
//     {
//         GameState st = gs;
//         Troll t; t.id=0; t.owner=0; t.x=0; t.y=1; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=0; t.chop=2;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Mine(0) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ(n.trolls[0].carry.r[(int)ResourceType::IRON], 2, "T5.Mine 2 iron (chop=2, adj iron)");
//     }

//     // T6: Plant test
//     {
//         GameState st = gs;
//         Troll t; t.id=0; t.owner=0; t.x=3; t.y=2; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=1; t.chop=1;
//         t.carry.r[(int)FruitType::LEMON] = 1;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Plant(0, FruitType::LEMON) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ((int)n.trees.size(), 1, "T6.Plant 1 new tree");
//         ASSERT_EQ(n.trolls[0].carry.r[(int)FruitType::LEMON], 0, "T6.Plant fruit consumed");
//         ASSERT_EQ((int)n.trees[0].type, (int)FruitType::LEMON, "T6.Plant type=LEMON");
//         ASSERT_EQ(n.trees[0].size, 1, "T6.Plant size=1");
//     }

//     // T7: Pick test
//     {
//         GameState st = gs;
//         st.shack[0].r[(int)FruitType::PLUM] = 5;
//         Troll t; t.id=0; t.owner=0; t.x=2; t.y=1; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=1; t.chop=1;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Pick(0, FruitType::PLUM) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ(n.shack[0][ResourceType::PLUM], 4, "T7.Pick shack 5→4");
//         ASSERT_EQ(n.trolls[0].carry.r[(int)FruitType::PLUM], 1, "T7.Pick troll +1 plum");
//     }

//     // T9: Train test — affordable tier C with N=1 spawns a new troll
//     {
//         GameState st = gs;
//         // shack inventory: enough for TRAIN 1 2 1 2 cost with N=1 (2P+5L+2A+5I)
//         st.shack[0].r[(int)FruitType::PLUM]  = 5;
//         st.shack[0].r[(int)FruitType::LEMON] = 6;
//         st.shack[0].r[(int)FruitType::APPLE] = 5;
//         st.shack[0].r[(int)ResourceType::IRON] = 6;
//         Troll t; t.id=0; t.owner=0; t.x=2; t.y=2; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=1; t.chop=1;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Train(1, 2, 1, 2) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ((int)n.trolls.size(), 2, "T9.Train spawns new troll");
//         ASSERT_EQ(n.shack[0][ResourceType::PLUM], 3, "T9.Train consumed 2 plums");
//         ASSERT_EQ(n.shack[0][ResourceType::LEMON], 1, "T9.Train consumed 5 lemons");
//         ASSERT_EQ(n.shack[0][ResourceType::APPLE], 3, "T9.Train consumed 2 apples");
//         ASSERT_EQ(n.shack[0][ResourceType::IRON], 1, "T9.Train consumed 5 iron");
//         // New troll at shack with given stats
//         const Troll& nt = n.trolls[1];
//         ASSERT_EQ(nt.movSpeed, 1, "T9.Train new troll movSpeed");
//         ASSERT_EQ(nt.carryCap, 2, "T9.Train new troll carryCap");
//         ASSERT_EQ(nt.harvest, 1, "T9.Train new troll harvest");
//         ASSERT_EQ(nt.chop, 2, "T9.Train new troll chop");
//         ASSERT_EQ(nt.owner, 0, "T9.Train new troll owner");
//     }

//     // T10: Train rejected when unaffordable
//     {
//         GameState st = gs;
//         st.shack[0].r[(int)FruitType::PLUM]  = 0;  // insufficient
//         st.shack[0].r[(int)FruitType::LEMON] = 6;
//         st.shack[0].r[(int)FruitType::APPLE] = 5;
//         st.shack[0].r[(int)ResourceType::IRON] = 6;
//         Troll t; t.id=0; t.owner=0; t.x=2; t.y=2; t.movSpeed=1; t.carryCap=4;
//                 t.harvest=1; t.chop=1;
//         st.trolls.push_back(t);
//         vector<Action> mine = { Action::Train(1, 2, 1, 2) };
//         GameState n = Simulator::step(st, mine, {});
//         ASSERT_EQ((int)n.trolls.size(), 1, "T10.Train rejected (no plums)");
//     }

//     // T8: Grow trees test — cooldown 1 → 0, produces fruit on size 4
//     {
//         GameState st = gs;
//         Tree tr; tr.type=FruitType::APPLE; tr.x=3; tr.y=2; tr.size=4;
//                  tr.health=20; tr.fruits=0; tr.cooldown=1;
//         st.trees.push_back(tr);
//         GameState n = Simulator::step(st, {}, {});
//         ASSERT_EQ(n.trees[0].fruits, 1, "T8.Grow size-4 apple produces fruit");
//         ASSERT_EQ(n.trees[0].cooldown, 9, "T8.Grow cooldown reset (apple far from water=9)");
//     }

//     cerr << "\n" << (anyFail ? "TESTS FAILED" : "ALL TESTS PASSED") << endl;
//     return anyFail ? 1 : 0;
// }

// =============================================================
// Layer 6 — Main
// =============================================================

#ifndef IMAGE3_TEST_BUILD
int main(int argc, char** argv) {
    // if (argc > 1 && std::string(argv[1]) == "test") {
    //     return testSimulator();
    // }
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    GameState gs;
    readInitial(gs);

    // FarmStrategy directly — SearchStrategy wrapper is currently a net
    // negative in arena (35-45ms/turn, frequent ABORTs, swaps that don't
    // materialise). Keep SearchStrategy compiled so the test harness can
    // still exercise it; re-enable here once we have macro-actions +
    // proper MCTS (the search shape it actually needs).
    FarmStrategy brain;

    while (cin.good()) {
        readTurn(gs);
        if (!cin.good()) break;

        // Build the static lookup tables once shack positions are known
        // (parsed from the first turn's troll list). FarmStrategy doesn't
        // use these yet — they're infrastructure for the macro-action /
        // MCTS layers landing in subsequent phases. Building is ~5-10ms
        // on max-size maps, well inside the 1000ms first-turn budget.
        if (!g_lookupsReady
            && gs.shackPos[0][0] >= 0 && gs.shackPos[1][0] >= 0) {
            buildStaticLookups(gs.map,
                               gs.shackPos[0][0], gs.shackPos[0][1],
                               gs.shackPos[1][0], gs.shackPos[1][1]);
        }

        // Strategy emits one comprehensive DBG line per turn (inside decide()).
        auto actions = brain.decide(gs);
        emit(actions);
        gs.turn++;
    }
    return 0;
}
#endif  // IMAGE3_TEST_BUILD
