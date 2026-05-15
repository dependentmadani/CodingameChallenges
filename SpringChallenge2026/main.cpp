// Spring Challenge 2026 — bot
// Single-file C++17, designed for forward-compatibility across leagues.
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

#define DBG(x) do { cerr << x << endl; } while (0)

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
// Layer 5 — Strategy
// =============================================================

struct IStrategy {
    virtual vector<Action> decide(const GameState&) = 0;
    virtual ~IStrategy() = default;
};

// GreedyStrategy: priority-rule controller. Per-team priorities:
//   0. (Turn 0 only) TRAIN a min-stat 2nd troll if affordable — matches boss opener.
// For each owned troll:
//   1. DROP if carrying fruit AND 4-adjacent to my shack.
//   2. HARVEST if standing on a tree with fruits AND free capacity.
//   3. MOVE to shack if full OR no fruits available anywhere on the map.
//   4. MOVE to nearest BFS-reachable tree with fruits > 0.
//   5. WAIT (only when nothing else applies).
struct GreedyStrategy : IStrategy {
    vector<Action> decide(const GameState& gs) override {
        vector<Action> out;

        const int W = gs.map.w;
        const int H = gs.map.h;
        const int myShackX = gs.shackPos[0][0];
        const int myShackY = gs.shackPos[0][1];

        // Ally trolls block each other ("1 troll per team per cell"). Build a
        // mask of cells currently occupied by any of MY trolls. Each troll's
        // BFS will treat these as blocked, except for the troll's own cell.
        vector<char> blockedByAlly(W*H, 0);
        for (const auto& tr : gs.trolls) {
            if (tr.owner == 0) blockedByAlly[tr.y*W + tr.x] = 1;
        }
        // Tree cells reserved this turn — claimed by an earlier troll.
        vector<char> reservedCell(W*H, 0);

        // Priority 0: train a second troll on turn 0.
        // Cost with N existing trolls and attribute a: N + a^2 fruits of that type.
        // For TRAIN 1 1 1 0 with 1 existing troll: 2 PLUM + 2 LEMON + 2 APPLE = 6 fruits.
        // Chop attr = 0 costs no banana.
        int myTrollCount = 0;
        for (const auto& t : gs.trolls) if (t.owner == 0) myTrollCount++;

        if (gs.turn == 0 && myTrollCount == 1) {
            const int N = myTrollCount;
            const int cP = N + 1*1;
            const int cL = N + 1*1;
            const int cA = N + 1*1;
            if (gs.shack[0][ResourceType::PLUM]  >= cP &&
                gs.shack[0][ResourceType::LEMON] >= cL &&
                gs.shack[0][ResourceType::APPLE] >= cA) {
                out.push_back(Action::Train(1, 1, 1, 0));
                out.push_back(Action::Msg("train min troll"));
                DBG("strategy: emit TRAIN 1 1 1 0 (cost " << cP << "P+" << cL << "L+" << cA << "A)");
            } else {
                DBG("strategy: skip TRAIN, insufficient fruits");
            }
        }

        // Are there any fruit-bearing trees right now?
        bool anyFruit = false;
        for (const auto& tr : gs.trees) if (tr.fruits > 0) { anyFruit = true; break; }

        for (const auto& t : gs.trolls) {
            if (t.owner != 0) continue;

            const int carried = t.carry.total();
            const int freeCap = t.carryCap - carried;
            const bool adjShack = manhattan(t.x, t.y, myShackX, myShackY) == 1;

            // Priority 1: drop
            if (carried > 0 && adjShack) {
                out.push_back(Action::Drop(t.id));
                continue;
            }

            // Priority 2: harvest in place
            const Tree* hereTree = nullptr;
            for (const auto& tr : gs.trees) {
                if (tr.x == t.x && tr.y == t.y) { hereTree = &tr; break; }
            }
            if (hereTree && hereTree->fruits > 0 && freeCap > 0) {
                out.push_back(Action::Harvest(t.id));
                continue;
            }

            // Priority 3: head to shack if full or nothing to harvest
            if (freeCap == 0 || (!anyFruit && carried > 0)) {
                out.push_back(Action::Move(t.id, myShackX, myShackY));
                continue;
            }

            // Priority 4: walk to nearest fruit-bearing tree.
            // Walkable predicate: grass cells only, not occupied by other
            // ally trolls, not reserved by an earlier troll's choice this turn.
            if (anyFruit) {
                auto walk = [&](int x, int y) -> bool {
                    if (gs.map.at(x, y) != Terrain::GRASS) return false;
                    const int idx = y*W + x;
                    if (reservedCell[idx]) return false;
                    if (blockedByAlly[idx] && !(x == t.x && y == t.y)) return false;
                    return true;
                };
                PathInfo p = bfs(gs.map, t.x, t.y, walk);
                const Tree* best = nullptr;
                int bestDist = INT32_MAX;
                for (const auto& tr : gs.trees) {
                    if (tr.fruits <= 0) continue;
                    if (reservedCell[tr.y*W + tr.x]) continue;
                    int d = p.at(tr.x, tr.y);
                    if (d < 0) continue;            // unreachable
                    if (d < bestDist) { bestDist = d; best = &tr; }
                }
                if (best) {
                    reservedCell[best->y*W + best->x] = 1;
                    out.push_back(Action::Move(t.id, best->x, best->y));
                    continue;
                }
            }

            // Priority 5: nothing useful
            out.push_back(Action::Wait());
        }

        if (out.empty()) out.push_back(Action::Wait());
        return out;
    }
};

// =============================================================
// Layer 6 — Main
// =============================================================

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    GameState gs;
    readInitial(gs);

    GreedyStrategy brain;

    while (cin.good()) {
        readTurn(gs);
        if (!cin.good()) break;

        auto t0 = chrono::steady_clock::now();
        auto actions = brain.decide(gs);
        auto dt = chrono::duration_cast<chrono::microseconds>(
                    chrono::steady_clock::now() - t0).count();

        DBG("turn " << gs.turn << " trees=" << gs.trees.size()
            << " trolls=" << gs.trolls.size()
            << " score(me/opp)=" << gs.shack[0].totalFruits()
            << "/" << gs.shack[1].totalFruits()
            << " think=" << dt << "us");

        emit(actions);
        gs.turn++;
    }
    return 0;
}
