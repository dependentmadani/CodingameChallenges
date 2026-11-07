// TryAngle-Catch referee: C++ port of github.com/eulerscheZahl/TryAngle-Catch
// (engine/*.java, engine/task/*.java, com/codingame/game/Referee.java).
//
// The port follows the Java code step by step, including its frame scheduling:
// one "frame" per task priority group, extra frames for surround and capture,
// and the 200 input turns counted through maxTurns. Function names match the
// Java methods so the two can be compared side by side.
#pragma once
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace tac {

// ---------------------------------------------------------------- java.util.Random
struct JavaRandom {
    static constexpr uint64_t MASK = (1ULL << 48) - 1;
    uint64_t seed;
    explicit JavaRandom(int64_t s) : seed(((uint64_t)s ^ 0x5DEECE66DULL) & MASK) {}
    int next(int bits) {
        seed = (seed * 0x5DEECE66DULL + 0xBULL) & MASK;
        return (int)(int64_t)(seed >> (48 - bits));
    }
    int nextInt(int bound) {
        if ((bound & -bound) == bound) return (int)(((int64_t)bound * next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while ((int64_t)bits - val + (bound - 1) > INT_MAX);  // Java int overflow test
        return val;
    }
    double nextDouble() {
        int64_t hi = next(26);  // Java evaluates left to right
        int64_t lo = next(27);
        return (double)((hi << 27) + lo) * 0x1.0p-53;
    }
};

// ---------------------------------------------------------------- constants (Board.java)
constexpr int TOP_BORDER = 70, BOTTOM_BORDER = 190, SIDE_BORDER = 80;
constexpr int WIDTH = 1920 - 2 * SIDE_BORDER, HEIGHT = 1080 - TOP_BORDER - BOTTOM_BORDER;
constexpr int NODE_COUNT = 50, NODE_MIN_DIST = 140, MIN_TRIANGLE_COUNT = 20;
constexpr int MAX_PATH_LENGTH = 400, MAX_ANGLE = 160;
constexpr double SPARSE_MIN = 0.1, SPARSE_MAX = 0.5;

enum ErrorCode { UNKNOWN_COMMAND = 1, NOT_AVAILABLE, INVALID_NODE, INVALID_TRIANGLE, CANT_BE_APPLIED,
                 CANT_ADD_PATH, POSITIVE_AMOUNT, NOT_NEAR_TRIANGLE, ERROR_CODES };

struct Node {
    int id, x, y;
    std::vector<int> nb;  // neighbor ids, sorted
    int units[2] = {0, 0};
    int rem[2] = {0, 0};  // remainingUnits
};

struct Tri {
    int n[3];
    int owner = -1, initialOwner = -1;
    bool cc[2] = {true, true};  // canCapture
    bool hasNode(int v) const { return n[0] == v || n[1] == v || n[2] == v; }
};

struct Edge { int a, b, id; };

inline bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// ---------------------------------------------------------------- Board
struct Board {
    std::vector<Node> nodes;
    std::vector<Tri> pool;  // every triangle object ever made; tasks keep indices into it
    std::vector<int> tris;  // current triangles (Board.triangles), indices into pool
    bool killedSurrounded = false;
    int statCaptured[2] = {0, 0};     // triangles captured, by capturing player
    int statSurrounded[2] = {0, 0};   // units killed by surrounding, by surrounding player

    int dist2(int a, int b) const {
        int dx = nodes[a].x - nodes[b].x, dy = nodes[a].y - nodes[b].y;
        return dx * dx + dy * dy;
    }
    double dist(int a, int b) const { return std::sqrt((double)dist2(a, b)); }
    static int dist2xy(int x1, int y1, int x2, int y2) { int dx = x1 - x2, dy = y1 - y2; return dx * dx + dy * dy; }

    bool ccw(int a, int b, int c) const {
        const Node &A = nodes[a], &B = nodes[b], &C = nodes[c];
        return (C.y - A.y) * (B.x - A.x) > (B.y - A.y) * (C.x - A.x);
    }
    bool isCrossing(int a1, int a2, int b1, int b2) const {
        if (a1 == a2) return false;
        if (a1 == b1 || a2 == b1 || a1 == b2 || a2 == b2) return false;
        return ccw(a1, b1, b2) != ccw(a2, b1, b2) && ccw(a1, a2, b1) != ccw(a1, a2, b2);
    }
    template <class EdgeList>
    bool isBlocked(int a, int b, const EdgeList& edges) const {
        if (dist(a, b) > MAX_PATH_LENGTH) return true;
        for (const auto& e : edges)
            if (isCrossing(a, b, e.a, e.b)) return true;
        return false;
    }

    void addNeighbor(int a, int b) {
        if (contains(nodes[a].nb, b)) return;
        nodes[a].nb.push_back(b);
        std::sort(nodes[a].nb.begin(), nodes[a].nb.end());
        addNeighbor(b, a);
    }
    void removeNeighbor(int a, int b) {
        auto& x = nodes[a].nb; auto it = std::find(x.begin(), x.end(), b); if (it != x.end()) x.erase(it);
        auto& y = nodes[b].nb; auto jt = std::find(y.begin(), y.end(), a); if (jt != y.end()) y.erase(jt);
    }

    // Node.bfs: distances from start; NODE_COUNT means unreachable
    std::vector<int> bfs(int start, int blocked = -1) const {
        std::vector<int> d(NODE_COUNT, NODE_COUNT);
        d[start] = 0;
        std::vector<int> q{start};
        for (size_t h = 0; h < q.size(); ++h) {
            int cur = q[h];
            for (int nx : nodes[cur].nb) {
                if (d[nx] < NODE_COUNT || nx == blocked) continue;
                d[nx] = 1 + d[cur];
                q.push_back(nx);
            }
        }
        return d;
    }

    int mirror(int id) const { return id % 2 == 0 ? id + 1 : id - 1; }

    bool isStronglyConnected() const {
        for (int blocked = 0; blocked < (int)nodes.size(); ++blocked) {
            int start = blocked == 0 ? 1 : 0;
            auto d = bfs(start, blocked);
            int unreachable = 0;
            for (int i = 0; i < (int)nodes.size(); i++)
                if (d[i] == NODE_COUNT) unreachable++;
            if (unreachable > 1) return false;
        }
        return true;
    }

    // Board.obtuseAngle. Triples of already placed nodes passed this test when they were placed,
    // so only triples that include one of the two new nodes can fail. The angle formulas are the
    // same as in Java, so the result is the same.
    bool obtuseAngle(int x1, int y1, int x2, int y2) const {
        int m = (int)nodes.size();
        std::vector<int> X(m + 2), Y(m + 2);
        for (int i = 0; i < m; i++) { X[i] = nodes[i].x; Y[i] = nodes[i].y; }
        X[m] = x1; Y[m] = y1; X[m + 1] = x2; Y[m + 1] = y2;
        auto D = [&](int i, int j) { return std::sqrt((double)dist2xy(X[i], Y[i], X[j], Y[j])); };
        const double R2D = 57.29577951308232;  // Math.toDegrees
        for (int i1 = 0; i1 < m + 2; i1++)
            for (int i2 = i1 + 1; i2 < m + 2; i2++)
                for (int i3 = std::max(i2 + 1, m); i3 < m + 2; i3++) {
                    double a = D(i1, i2), b = D(i1, i3), c = D(i2, i3);
                    if (a > MAX_PATH_LENGTH || b > MAX_PATH_LENGTH || c > MAX_PATH_LENGTH) continue;
                    double alpha = std::acos((b * b + c * c - a * a) / (2 * b * c)) * R2D;
                    double beta = std::acos((a * a + c * c - b * b) / (2 * a * c)) * R2D;
                    double gamma = std::acos((a * a + b * b - c * c) / (2 * a * b)) * R2D;
                    if (alpha > MAX_ANGLE || beta > MAX_ANGLE || gamma > MAX_ANGLE) return true;
                }
        return false;
    }

    bool isInside(int n, int a, int b, int c) const {
        if (n == a || n == b || n == c) return false;
        bool c1 = ccw(n, a, b), c2 = ccw(n, b, c), c3 = ccw(n, c, a);
        bool hasNeg = !c1 || !c2 || !c3;
        bool hasPos = c1 || c2 || c3;
        return !(hasNeg && hasPos);
    }

    // Board.updateTriangles + findTriangles: keep the old triangle object (owner, flags) when it still exists
    void updateTriangles() {
        std::vector<int> result;
        int N = (int)nodes.size();
        for (int n1 = 0; n1 < N; n1++)
            for (int n2 : nodes[n1].nb)
                for (int n3 : nodes[n2].nb) {
                    if (!(n1 < n2 && n2 < n3 && contains(nodes[n1].nb, n3))) continue;
                    bool inside = false;
                    for (int n = 0; n < N && !inside; n++) inside = isInside(n, n1, n2, n3);
                    if (inside) continue;
                    int old = -1;
                    for (int t : tris) {
                        const Tri& T = pool[t];
                        if (T.hasNode(n1) && T.hasNode(n2) && T.hasNode(n3)) old = t;
                    }
                    if (old < 0) {
                        Tri T; T.n[0] = n1; T.n[1] = n2; T.n[2] = n3;
                        pool.push_back(T);
                        old = (int)pool.size() - 1;
                    }
                    result.push_back(old);
                }
        tris = result;
    }

    bool canConnect(int from, int to) const {
        std::vector<Edge> existing;
        for (int a = 0; a < (int)nodes.size(); a++)
            for (int b : nodes[a].nb)
                if (b > a) existing.push_back({a, b, 0});
        return !isBlocked(from, to, existing);
    }

    void connect(int from, int to) { addNeighbor(from, to); updateTriangles(); }
    void disconnect(int from, int to) { removeNeighbor(from, to); updateTriangles(); }

    // Node.getOwner: surrounded houses belong to the surrounding side, otherwise the majority
    int nodeOwner(int v) const {
        const Node& nd = nodes[v];
        if (!nd.nb.empty()) {
            bool all0 = true, all1 = true;
            for (int n : nd.nb) {
                if (!(nodes[n].units[0] > nodes[n].units[1])) all0 = false;
                if (!(nodes[n].units[1] > nodes[n].units[0])) all1 = false;
            }
            if (all0) return 0;
            if (all1) return 1;
        }
        if (nd.units[0] > nd.units[1]) return 0;
        if (nd.units[1] > nd.units[0]) return 1;
        return -1;
    }

    // ---- Triangle methods
    void updateAllowedCaptures(Tri& t) {
        for (int i = 0; i < 2; i++) {
            if (t.owner >= 0) t.cc[i] = t.owner != i;
            else t.cc[i] |= nodes[t.n[0]].units[i] == 0 && nodes[t.n[1]].units[i] == 0 && nodes[t.n[2]].units[i] == 0;
        }
    }
    bool capture(Tri& t) {
        int old = t.owner;
        for (int p = 0; p < 2; p++)
            if (t.cc[p] && nodeOwner(t.n[0]) == p && nodeOwner(t.n[1]) == p && nodeOwner(t.n[2]) == p) t.owner = p;
        return t.owner != old;
    }
    void use(Tri& t, int units) {
        int o = t.owner;
        for (int k = 0; k < 3; k++) {
            Node& nd = nodes[t.n[k]];
            int take = std::min(units, nd.units[o]);
            nd.units[o] -= take;
            nd.rem[o] -= take;
        }
        t.cc[o] = false;
        t.owner = -1;
    }
    bool canUse(const Tri& t, int p, int units) const {
        return t.owner == p && t.initialOwner == p && nodes[t.n[0]].rem[p] >= units && nodes[t.n[1]].rem[p] >= units &&
               nodes[t.n[2]].rem[p] >= units;
    }
    bool hasNeighbor(const Tri& t, int v) const {
        return contains(nodes[t.n[0]].nb, v) || contains(nodes[t.n[1]].nb, v) || contains(nodes[t.n[2]].nb, v);
    }

    // ---- turn resolution (Board.finalizeTurn and helpers)
    std::vector<std::string>* surroundLog = nullptr;  // optional diagnostics
    bool surroundNodes() {
        if (killedSurrounded) return false;
        killedSurrounded = true;
        int N = (int)nodes.size();
        std::vector<char> adv[2];
        for (int i = 0; i < 2; i++) {
            adv[i].assign(N, 0);
            for (int v = 0; v < N; v++) adv[i][v] = nodes[v].units[i] > nodes[v].units[1 - i];
        }
        bool surrounded = false;
        for (int i = 0; i < 2; i++)
            for (int v = 0; v < N; v++) {
                Node& nd = nodes[v];
                if (nd.units[i] <= 0 || nd.nb.empty()) continue;
                bool all = true;
                for (int n : nd.nb) all &= adv[1 - i][n] != 0;
                if (all) {
                    if (surroundLog) {  // diagnostics: victim side, house, units, and each neighbor's counts at kill time
                        std::string e = "p" + std::to_string(i) + " lost " + std::to_string(nd.units[i]) + " at " + std::to_string(v) + " [nb";
                        for (int n : nd.nb) e += " " + std::to_string(n) + ":" + std::to_string(nodes[n].units[i]) + "/" + std::to_string(nodes[n].units[1 - i]);
                        surroundLog->push_back(e + "]");
                    }
                    statSurrounded[1 - i] += nd.units[i]; nd.units[i] = 0; surrounded = true;
                }
            }
        return surrounded;
    }
    bool captureTriangles() {
        bool result = false;
        for (int t : tris) {
            bool c = capture(pool[t]);
            if (c) statCaptured[pool[t].owner]++;
            result |= c;
        }
        return result;
    }
    // returns true when this frame was used for surround kills or captures
    bool finalizeTurn(int score[2]) {
        for (auto& nd : nodes) { nd.rem[0] = nd.units[0]; nd.rem[1] = nd.units[1]; }
        for (int t : tris) updateAllowedCaptures(pool[t]);
        if (surroundNodes() || captureTriangles()) return true;
        for (int t : tris) {
            Tri& T = pool[t];
            if (T.owner >= 0) score[T.owner]++;
            T.initialOwner = T.owner;
        }
        return false;
    }

    int unitsOf(int p) const { int s = 0; for (auto& nd : nodes) s += nd.units[p]; return s; }
    int trianglesOf(int p) const { int s = 0; for (int t : tris) s += pool[t].owner == p; return s; }

    // ---- map generation (Board constructor)
    void generate(int64_t seed, int score[2]) {
        JavaRandom random(seed);
        std::vector<Edge> existing;
        int edgeIds = 0;
        while (true) {
            int tries = 0;
            nodes.clear(); pool.clear(); tris.clear();
            std::set<int> nodeDistances;
            while ((int)nodes.size() < NODE_COUNT && tries++ < 10000) {
                int x1 = SIDE_BORDER + random.nextInt(WIDTH);
                int y1 = TOP_BORDER + random.nextInt(HEIGHT);
                int x2 = WIDTH - 1 - (x1 - SIDE_BORDER) + SIDE_BORDER;
                int y2 = HEIGHT - 1 - (y1 - TOP_BORDER) + TOP_BORDER;
                bool bad = false;
                for (auto& n : nodes) if (std::sqrt((double)dist2xy(n.x, n.y, x1, y1)) < NODE_MIN_DIST) { bad = true; break; }
                int d12 = dist2xy(x1, y1, x2, y2);
                if (bad || std::sqrt((double)d12) < NODE_MIN_DIST || nodeDistances.count(d12)) continue;
                for (auto& n : nodes) if (nodeDistances.count(dist2xy(n.x, n.y, x1, y1))) { bad = true; break; }
                if (bad || obtuseAngle(x1, y1, x2, y2)) continue;
                nodeDistances.insert(d12);
                for (auto& n : nodes) nodeDistances.insert(dist2xy(n.x, n.y, x1, y1));
                int id = (int)nodes.size();
                nodes.push_back(Node{id, x1, y1, {}, {0, 0}, {0, 0}});
                nodes.push_back(Node{id + 1, x2, y2, {}, {0, 0}, {0, 0}});
            }

            std::vector<Edge> edges;
            for (int i = 0; i < (int)nodes.size(); i++)
                for (int j = i + 1; j < (int)nodes.size(); j++) edges.push_back({i, j, edgeIds++});
            std::stable_sort(edges.begin(), edges.end(), [&](const Edge& e1, const Edge& e2) { return dist(e1.a, e1.b) < dist(e2.a, e2.b); });
            existing.clear();
            for (auto& e : edges) {
                if (isBlocked(e.a, e.b, existing)) continue;
                addNeighbor(e.a, e.b);
                existing.push_back(e);
            }
            updateTriangles();
            if (isStronglyConnected() && (int)tris.size() >= MIN_TRIANGLE_COUNT) break;
        }

        // randomly remove some edges again, always together with the mirrored edge
        std::set<int> tested;
        int removeEdgeCount = (int)(existing.size() * (random.nextDouble() * (SPARSE_MAX - SPARSE_MIN) + SPARSE_MIN));
        while (removeEdgeCount > 0) {
            removeEdgeCount -= 2;
            Edge e = existing[random.nextInt((int)existing.size())];
            if (tested.count(e.id)) continue;
            int m1 = mirror(e.a), m2 = mirror(e.b);
            Edge partner{-1, -1, -1};
            for (auto& f : existing)
                if ((f.a == m1 && f.b == m2) || (f.a == m2 && f.b == m1)) partner = f;
            if (partner.id < 0) throw std::runtime_error("map generation: edge without mirrored partner");
            tested.insert(e.id);
            tested.insert(partner.id);
            removeNeighbor(e.a, e.b);
            removeNeighbor(partner.a, partner.b);
            updateTriangles();
            if ((int)tris.size() >= MIN_TRIANGLE_COUNT && isStronglyConnected()) {
                auto eraseId = [&](int id) {
                    for (size_t k = 0; k < existing.size(); k++)
                        if (existing[k].id == id) { existing.erase(existing.begin() + k); return; }
                };
                eraseId(e.id);
                eraseId(partner.id);
            } else {
                addNeighbor(e.a, e.b);
                addNeighbor(partner.a, partner.b);
            }
        }

        std::vector<int> byX(nodes.size());
        for (size_t i = 0; i < byX.size(); i++) byX[i] = (int)i;
        std::stable_sort(byX.begin(), byX.end(), [&](int a, int b) { return nodes[a].x < nodes[b].x; });
        for (int i = 0; i < 3; i++) {
            nodes[byX[i]].units[0] = 1;
            nodes[mirror(byX[i])].units[1] = 1;
        }

        // start from a clean triangle pool (no owners exist yet)
        pool.clear(); tris.clear();
        updateTriangles();
        while (finalizeTurn(score)) {}
    }
};

// ---------------------------------------------------------------- tasks
enum TaskType { ATTACK = 0, MOVE = 1, SPAWN = 2, ADD_PATH = 3, REMOVE_PATH = 4 };  // value = task priority

struct Task {
    int type, player;
    bool failed = false;
    int from = -1, to = -1, amount = 0, tri = -1, target = -1;
    bool multi() const { return type == MOVE || type == SPAWN; }
    int requiredLeague() const { return type == MOVE || type == SPAWN ? 1 : type == ATTACK ? 2 : 3; }
};

struct Errors {
    int count[ERROR_CODES] = {};
    std::string firstMessage[ERROR_CODES];
    void add(int code, const std::string& msg) {
        count[code]++;
        if (firstMessage[code].empty()) firstMessage[code] = msg;
    }
};

// ---------------------------------------------------------------- game (Referee.java + TaskManager.java)
struct Game {
    Board board;
    int score[2] = {0, 0};
    bool active[2] = {true, true};
    bool ended = false;
    std::string endReason[2];
    int league = 4;
    int maxTurns = 200;
    int frame = 0;
    int inputTurns = 0;
    int statSpawned[2] = {0, 0}, statAttacked[2] = {0, 0};  // units spawned, opponent units killed by attacks
    Errors errors[2];  // totals over the game
    std::string critical[2];  // first critical error of the current input (kills the player)
    std::vector<Task> store;
    std::vector<int> tasks;  // TaskManager.tasks, indices into store

    // returns false on timeout or crash; fills `out` with the bot's line
    using IO = std::function<bool(int player, const std::string& input, bool firstTurn, std::string& out)>;
    // optional diagnostics hooks: every resolved move (player, from, first step, amount), and a call
    // after both players' commands are parsed, before they are applied
    std::vector<std::array<int, 4>>* moveLog = nullptr;
    std::function<void(Game&)> beforeActions;

    explicit Game(int64_t seed, int leagueLevel = 4) : league(leagueLevel) { board.generate(seed, score); }

    // ---- input (Board.getInput)
    std::vector<std::pair<int, int>> linkable() const {
        std::vector<std::pair<int, int>> canLink;
        const auto& nodes = board.nodes;
        if (league >= 3)
            for (int a = 0; a < (int)nodes.size(); a++)
                for (int b = a + 1; b < (int)nodes.size(); b++)
                    if (!contains(nodes[a].nb, b) && board.canConnect(a, b)) canLink.push_back({a, b});
        return canLink;
    }
    std::string getInput(bool initial, int p, const std::vector<std::pair<int, int>>& canLink) const {
        const auto& nodes = board.nodes;
        std::ostringstream sb;
        if (initial) {
            sb << nodes.size() << "\n";
            for (auto& n : nodes) sb << n.id << " " << n.x << " " << n.y << "\n";
        }
        std::vector<std::pair<int, int>> links;
        for (auto& n1 : nodes)
            for (int n2 : n1.nb)
                if (n2 > n1.id) links.push_back({n1.id, n2});
        sb << score[p] << " " << score[1 - p] << "\n";
        for (auto& n : nodes) sb << n.id << " " << n.units[p] << " " << n.units[1 - p] << "\n";
        sb << links.size() << "\n";
        for (auto& l : links) sb << l.first << " " << l.second << "\n";
        sb << board.tris.size() << "\n";
        for (int t : board.tris) {
            const Tri& T = board.pool[t];
            int o = T.owner == p ? 0 : T.owner >= 0 ? 1 : -1;
            sb << T.n[0] << " " << T.n[1] << " " << T.n[2] << " " << o << " " << (T.cc[p] ? 1 : 0) << " "
               << (T.cc[1 - p] ? 1 : 0) << "\n";
        }
        sb << canLink.size() << "\n";
        for (auto& l : canLink) sb << l.first << " " << l.second << "\n";
        return sb.str();
    }

    // ---- parsing (TaskManager.parseTasks + Task.parseTask + task constructors)
    static bool isJavaSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\x0B' || c == '\f' || c == '\r'; }
    static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
    static std::string javaTrim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (unsigned char)s[a] <= ' ') a++;
        while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
        return s.substr(a, b - a);
    }
    static bool isDigits(const std::string& s, bool allowMinus) {
        size_t i = allowMinus && !s.empty() && s[0] == '-' ? 1 : 0;
        if (i >= s.size()) return false;
        for (; i < s.size(); i++) if (!isdigit((unsigned char)s[i])) return false;
        return true;
    }
    // Integer.parseInt; false when it would throw
    static bool parseInt(const std::string& s, int& v) {
        long long r = 0; bool neg = s[0] == '-';
        for (size_t i = neg ? 1 : 0; i < s.size(); i++) {
            r = r * 10 + (s[i] - '0');
            if (r > (long long)INT_MAX + 1) return false;
        }
        if (neg) r = -r;
        if (r > INT_MAX || r < INT_MIN) return false;
        v = (int)r;
        return true;
    }

    void addParsingError(Task& t, int code, const std::string& msg) {
        if (t.failed) return;
        t.failed = true;
        errors[t.player].add(code, msg);
    }
    int getNode(Task& t, int id, const std::string& tok) {
        if (id < 0 || id >= (int)board.nodes.size()) { addParsingError(t, INVALID_NODE, "Invalid node: " + tok); return -1; }
        return id;
    }
    int getTriangle(Task& t, int a, int b, int c) {
        if (a < 0 || b < 0 || c < 0) return -1;
        for (int i : board.tris) {
            const Tri& T = board.pool[i];
            if (T.hasNode(a) && T.hasNode(b) && T.hasNode(c)) return i;
        }
        addParsingError(t, INVALID_TRIANGLE, "Invalid triangle: " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c));
        return -1;
    }

    void parseTask(int p, const std::string& command) {
        std::string tr = javaTrim(command);
        if (tr.empty() || upper(tr) == "WAIT") return;
        std::vector<std::string> tok;
        {
            std::string cur;
            for (char c : command) {
                if (isJavaSpace(c)) { if (!cur.empty()) tok.push_back(cur), cur.clear(); }
                else cur += c;
            }
            if (!cur.empty()) tok.push_back(cur);
        }
        auto unknown = [&] {
            errors[p].add(UNKNOWN_COMMAND, "Unknown command: " + command);
            if (critical[p].empty()) critical[p] = "Unknown command: " + command;
        };
        if (tok.empty()) { unknown(); return; }
        std::string kw = upper(tok[0]);
        auto numbers = [&](size_t n, size_t signedFrom, std::vector<int>& v) -> int {  // 1 ok, 0 no match, -1 overflow
            if (tok.size() != n + 1) return 0;
            for (size_t i = 1; i <= n; i++) if (!isDigits(tok[i], i >= signedFrom)) return 0;
            v.resize(n + 1);
            for (size_t i = 1; i <= n; i++) if (!parseInt(tok[i], v[i])) return -1;
            return 1;
        };
        std::vector<int> v;
        Task t; t.player = p;
        int m;
        if (kw == "MOVE") {
            if ((m = numbers(3, 3, v)) <= 0) return unknown();
            t.type = MOVE;
            t.from = getNode(t, v[1], tok[1]);
            t.to = getNode(t, v[2], tok[2]);
            t.amount = v[3];
            if (t.amount <= 0) addParsingError(t, POSITIVE_AMOUNT, "Amount of units to move must be positive");
        } else if (kw == "SPAWN") {
            if ((m = numbers(3, 99, v)) <= 0) return unknown();
            t.type = SPAWN;
            t.from = getNode(t, v[1], tok[1]);
            int n2 = getNode(t, v[2], tok[2]), n3 = getNode(t, v[3], tok[3]);
            t.tri = getTriangle(t, t.from, n2, n3);
        } else if (kw == "ATTACK") {
            if ((m = numbers(4, 99, v)) <= 0) return unknown();
            t.type = ATTACK;
            int n1 = getNode(t, v[1], tok[1]), n2 = getNode(t, v[2], tok[2]), n3 = getNode(t, v[3], tok[3]);
            t.target = getNode(t, v[4], tok[4]);
            t.tri = getTriangle(t, n1, n2, n3);
            if (!t.failed && !board.hasNeighbor(board.pool[t.tri], t.target))
                addParsingError(t, NOT_NEAR_TRIANGLE, "Can't attack house " + tok[4] + " from given triangle");
        } else if (kw == "ADD_PATH" || kw == "REMOVE_PATH") {
            if ((m = numbers(4, 99, v)) <= 0) return unknown();
            t.type = kw == "ADD_PATH" ? ADD_PATH : REMOVE_PATH;
            t.from = getNode(t, v[1], tok[1]);
            int n2 = getNode(t, v[2], tok[2]), n3 = getNode(t, v[3], tok[3]);
            t.to = getNode(t, v[4], tok[4]);
            t.tri = getTriangle(t, t.from, n2, n3);
            if (t.type == ADD_PATH && !t.failed && !board.canConnect(t.from, t.to))
                addParsingError(t, CANT_ADD_PATH, "Can't connect houses " + tok[1] + " and " + tok[4]);
            if (t.type == REMOVE_PATH && !t.failed && !board.hasNeighbor(board.pool[t.tri], t.to))
                addParsingError(t, NOT_NEAR_TRIANGLE, "House " + tok[4] + " isn't connected to given triangle");
        } else if (kw == "LINE" || kw == "RECT") {  // debug drawings: no effect on the game
            if ((m = numbers(4, 1, v)) <= 0) return unknown();
            return;
        } else if (kw == "CIRCLE") {
            if ((m = numbers(3, 1, v)) <= 0) return unknown();
            return;
        } else if (kw == "TEXT") {
            if (tok.size() < 4 || !isDigits(tok[1], true) || !isDigits(tok[2], true)) return unknown();
            int a; if (!parseInt(tok[1], a) || !parseInt(tok[2], a)) return unknown();
            return;
        } else {
            return unknown();
        }
        if (t.requiredLeague() > league) {
            addParsingError(t, NOT_AVAILABLE, "Command not available in your league: " + command);
            return;
        }
        if (!canApply(t, true)) addParsingError(t, CANT_BE_APPLIED, "Task can't be applied: " + command);
        store.push_back(t);
        tasks.push_back((int)store.size() - 1);
    }

    void parseTasks(int p, const std::string& line) {
        // String.split(";") drops trailing empty strings; empty commands are ignored anyway
        size_t start = 0;
        while (true) {
            size_t end = line.find(';', start);
            std::string comm = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (upper(comm).rfind("MSG ", 0) != 0) parseTask(p, comm);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    // ---- task rules
    bool canApply(const Task& t, bool strict) const {
        const Board& b = board;
        switch (t.type) {
            case MOVE: {
                if (!strict) return true;
                if (t.failed) return false;
                if (t.from == t.to || b.nodes[t.from].rem[t.player] <= 0) return false;
                return b.bfs(t.to)[t.from] > 0;  // Node.canMoveTo
            }
            case SPAWN: return !t.failed && b.canUse(b.pool[t.tri], t.player, 0);
            case ATTACK:
                if (!strict) return true;
                return !t.failed && b.canUse(b.pool[t.tri], t.player, 1) && b.hasNeighbor(b.pool[t.tri], t.target);
            case ADD_PATH: {
                bool valid = !t.failed && b.canUse(b.pool[t.tri], t.player, 1) && !contains(b.nodes[t.from].nb, t.to);
                if (!strict) valid = true;
                return valid && b.canConnect(t.from, t.to);
            }
            case REMOVE_PATH:
                if (!strict) return true;
                return !t.failed && b.canUse(b.pool[t.tri], t.player, 1) && contains(b.nodes[t.from].nb, t.to) &&
                       !b.pool[t.tri].hasNode(t.to);
        }
        return false;
    }
    int taskCost(const Task& t) const { return t.type == ADD_PATH ? board.dist2(t.from, t.to) : 0; }

    void apply(Task& t) {
        Board& b = board;
        int p = t.player;
        switch (t.type) {
            case MOVE: {
                Node& from = b.nodes[t.from];
                t.amount = std::min(t.amount, from.rem[p]);
                if (t.from == t.to) return;  // Node.moveTo
                int amount = std::min(t.amount, from.rem[p]);
                if (amount <= 0) return;
                auto d = b.bfs(t.to);
                for (int nx : from.nb) {
                    if (d[nx] < d[t.from]) {
                        if (moveLog) moveLog->push_back({p, t.from, nx, amount});
                        from.units[p] -= amount;
                        from.rem[p] -= amount;
                        b.nodes[nx].units[p] += amount;
                        return;
                    }
                }
                return;
            }
            case SPAWN:
                b.use(b.pool[t.tri], 0);
                b.nodes[t.from].units[p]++;
                statSpawned[p]++;
                return;
            case ATTACK: {
                b.use(b.pool[t.tri], 1);
                Node& tg = b.nodes[t.target];
                t.amount = tg.units[1 - p];
                statAttacked[p] += t.amount;
                tg.units[1 - p] = 0;
                tg.rem[1 - p] = 0;
                return;
            }
            case ADD_PATH:
                b.use(b.pool[t.tri], 1);
                b.connect(t.from, t.to);
                return;
            case REMOVE_PATH:
                b.use(b.pool[t.tri], 1);
                b.disconnect(t.from, t.to);
                return;
        }
    }

    // ---- TaskManager scheduling
    std::vector<int> peekTasks() {
        for (int i = (int)tasks.size() - 1; i >= 0; i--)
            if (!canApply(store[tasks[i]], true)) tasks.erase(tasks.begin() + i);
        std::vector<int> result;
        for (int t : tasks) {
            if (!result.empty() && store[result[0]].type > store[t].type) result.clear();
            if (result.empty() || store[t].type == store[result[0]].type) result.push_back(t);
        }
        std::stable_sort(result.begin(), result.end(), [&](int a, int b) { return taskCost(store[a]) < taskCost(store[b]); });
        return result;
    }
    bool hasTasks() { return !peekTasks().empty(); }
    void popTasks(std::vector<int> out[2]) {
        auto pop = peekTasks();
        for (int t : pop) tasks.erase(std::find(tasks.begin(), tasks.end(), t));
        out[0].clear(); out[1].clear();
        for (int t : pop) out[store[t].player].push_back(t);
    }

    void applyActions() {
        board.killedSurrounded = false;
        std::vector<int> per[2];
        popTasks(per);
        std::vector<int> toApply;
        for (int p = 0; p < 2; p++) {
            while (!per[p].empty()) {
                int t = per[p].front();
                per[p].erase(per[p].begin());
                if (!canApply(store[t], true)) continue;
                toApply.push_back(t);
                if (!store[t].multi()) break;
            }
        }
        std::stable_sort(toApply.begin(), toApply.end(), [&](int a, int b) { return taskCost(store[a]) < taskCost(store[b]); });
        for (int t : toApply) {
            if (!canApply(store[t], false)) continue;
            if (store[t].multi() && !canApply(store[t], true)) continue;
            apply(store[t]);
        }
        for (int p = 0; p < 2; p++) tasks.insert(tasks.end(), per[p].begin(), per[p].end());
        hasTasks();  // Java prunes here too (summary check)
    }

    void killPlayer(int p, const std::string& msg) {
        if (!active[p]) return;
        active[p] = false;
        endReason[p] = msg;
        score[p] = -1;
        ended = true;
    }

    // Referee.gameTurn for one frame
    void gameTurn(const IO& io) {
        if (!hasTasks()) {
            if (board.finalizeTurn(score)) { maxTurns++; return; }
            std::vector<int> act;
            for (int p = 0; p < 2; p++) if (active[p]) act.push_back(p);
            std::vector<int> exec;
            for (int p : act) {
                int u = board.unitsOf(p), tr = board.trianglesOf(p);
                if (u == 0 && tr == 0) { killPlayer(p, "no units or triangles left"); continue; }
                if (u == 1 && tr == 0 && score[p] < score[1 - p]) { killPlayer(p, "can't score any more points"); continue; }
                if (board.trianglesOf(1 - p) >= board.tris.size() * 0.8) { killPlayer(p, "is dominated"); continue; }
                exec.push_back(p);
            }
            // After a kill the game ends with this frame and its actions can't change the score, so stop here.
            if (ended) return;
            inputTurns++;
            std::string input[2];
            auto canLink = linkable();
            for (int p : exec) input[p] = getInput(frame == 1, p, canLink);
            store.clear();  // no task is left from the previous turn here
            for (int p : exec) {
                std::string out;
                if (!io(p, input[p], frame == 1, out)) { killPlayer(p, "timeout"); continue; }
                critical[p].clear();
                parseTasks(p, out);
                if (!critical[p].empty()) killPlayer(p, critical[p]);
            }
            if (ended) return;
            if (beforeActions) beforeActions(*this);
        } else {
            maxTurns++;
        }
        applyActions();
    }

    void run(const IO& io) {
        for (frame = 1; frame <= maxTurns && !ended; frame++) gameTurn(io);
    }
};

}  // namespace tac
