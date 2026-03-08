/**
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║         CodinGame Winter Challenge — C++ Starter Template        ║
 * ║                  by a Senior Dev who loves contests              ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * Sections:
 *   1. Includes & Macros
 *   2. Type Aliases
 *   3. Debug Utilities
 *   4. Math & Number Theory
 *   5. Geometry (2D)
 *   6. Graph Algorithms
 *   7. Data Structures
 *   8. String Utilities
 *   9. Search & Optimization (BFS / DFS / A* / Beam Search)
 *  10. Monte Carlo Tree Search (MCTS) skeleton
 *  11. Simulation / Game helpers
 *  12. Main entry point
 */

// ─────────────────────────────────────────────────────────────────────────────
// 1. INCLUDES & MACROS
// ─────────────────────────────────────────────────────────────────────────────
#pragma GCC optimize("O3,unroll-loops")
#pragma GCC target("avx2")

#include <bits/stdc++.h>
using namespace std;

// Useful shorthand macros
#define ALL(x)          (x).begin(), (x).end()
#define SZ(x)           (int)(x).size()
#define REP(i, n)       for (int i = 0; i < (n); ++i)
#define REPR(i, n)      for (int i = (n)-1; i >= 0; --i)
#define FOR(i, a, b)    for (int i = (a); i < (b); ++i)
#define EACH(x, v)      for (auto& x : v)
#define PB              push_back
#define MP              make_pair
#define FI              first
#define SE              second

// ─────────────────────────────────────────────────────────────────────────────
// 2. TYPE ALIASES
// ─────────────────────────────────────────────────────────────────────────────
using ll   = long long;
using ull  = unsigned long long;
using ld   = long double;
using pii  = pair<int, int>;
using pll  = pair<ll, ll>;
using vi   = vector<int>;
using vll  = vector<ll>;
using vvi  = vector<vi>;
using vb   = vector<bool>;

// ─────────────────────────────────────────────────────────────────────────────
// 3. DEBUG UTILITIES  (disable in submission by removing -DDEBUG flag)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef DEBUG
    #define dbg(x)        cerr << "[DBG] " << #x << " = " << (x) << "\n"
    #define dbgv(v)       { cerr << "[DBG] " << #v << " = ["; for(auto _x:(v)) cerr<<_x<<","; cerr<<"]\n"; }
    #define dbg2(x, y)    cerr << "[DBG] " << #x << "=" << (x) << "  " << #y << "=" << (y) << "\n"
    #define TIMER_START   auto _t0 = chrono::steady_clock::now()
    #define TIMER_MS      (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()-_t0).count())
#else
    #define dbg(x)
    #define dbgv(v)
    #define dbg2(x, y)
    #define TIMER_START
    #define TIMER_MS      0
#endif

// ─────────────────────────────────────────────────────────────────────────────
// 4. MATH & NUMBER THEORY
// ─────────────────────────────────────────────────────────────────────────────
const ll  MOD  = 1e9 + 7;
const ll  INF  = 1e18;
const int IINF = 1e9;
const ld  PI   = acosl(-1.0L);
const ld  EPS  = 1e-9;

ll gcd(ll a, ll b)           { return b ? gcd(b, a % b) : a; }
ll lcm(ll a, ll b)           { return a / gcd(a, b) * b; }
ll power(ll b, ll e, ll m)   { ll r=1; b%=m; while(e>0){if(e&1)r=r*b%m; b=b*b%m; e>>=1;} return r; }
ll modinv(ll a, ll m)        { return power(a, m-2, m); }

// Clamp helpers
template<class T> T clamp(T v, T lo, T hi) { return max(lo, min(v, hi)); }
template<class T> bool inrange(T v, T lo, T hi) { return v >= lo && v < hi; }

// Sieve of Eratosthenes
vector<bool> sieve(int n) {
    vector<bool> is_prime(n+1, true);
    is_prime[0] = is_prime[1] = false;
    for (int i = 2; i * i <= n; ++i)
        if (is_prime[i])
            for (int j = i*i; j <= n; j += i)
                is_prime[j] = false;
    return is_prime;
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. GEOMETRY (2D)
// ─────────────────────────────────────────────────────────────────────────────
struct Vec2 {
    ld x, y;
    Vec2(ld x=0, ld y=0): x(x), y(y) {}
    Vec2 operator+(const Vec2& o) const { return {x+o.x, y+o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x-o.x, y-o.y}; }
    Vec2 operator*(ld t)          const { return {x*t, y*t}; }
    ld   dot(const Vec2& o)       const { return x*o.x + y*o.y; }
    ld   cross(const Vec2& o)     const { return x*o.y - y*o.x; }
    ld   norm()                   const { return sqrtl(x*x + y*y); }
    ld   norm2()                  const { return x*x + y*y; }
    Vec2 normalized()             const { ld n=norm(); return {x/n, y/n}; }
    ld   angle()                  const { return atan2l(y, x); }
    Vec2 rotate(ld a)             const { return {x*cosl(a)-y*sinl(a), x*sinl(a)+y*cosl(a)}; }

    ld distTo(const Vec2& o) const { return (*this - o).norm(); }
    ld dist2To(const Vec2& o) const { return (*this - o).norm2(); }

    // Useful for CodinGame grid coords
    static Vec2 fromInt(int x, int y) { return {(ld)x, (ld)y}; }
};

// Angle between two vectors in degrees
ld angleDeg(Vec2 a, Vec2 b) {
    return acosl(clamp(a.normalized().dot(b.normalized()), (ld)-1, (ld)1)) * 180.0L / PI;
}

// Closest point on segment AB to point P
Vec2 closestPointOnSegment(Vec2 A, Vec2 B, Vec2 P) {
    Vec2 AB = B - A, AP = P - A;
    ld t = clamp(AP.dot(AB) / AB.norm2(), (ld)0, (ld)1);
    return A + AB * t;
}

// Circle-circle intersection check
bool circlesOverlap(Vec2 c1, ld r1, Vec2 c2, ld r2) {
    return c1.dist2To(c2) <= (r1+r2)*(r1+r2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. GRAPH ALGORITHMS
// ─────────────────────────────────────────────────────────────────────────────

// --- BFS (unweighted shortest path) ---
vi bfs(int src, const vvi& adj, int n) {
    vi dist(n, -1);
    queue<int> q;
    dist[src] = 0;
    q.push(src);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        for (int v : adj[u]) {
            if (dist[v] == -1) {
                dist[v] = dist[u] + 1;
                q.push(v);
            }
        }
    }
    return dist;
}

// --- Dijkstra (weighted shortest path) ---
vector<ll> dijkstra(int src, const vector<vector<pair<int,ll>>>& adj, int n) {
    vector<ll> dist(n, INF);
    priority_queue<pll, vector<pll>, greater<pll>> pq;
    dist[src] = 0;
    pq.push({0, src});
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        for (auto [v, w] : adj[u]) {
            if (dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                pq.push({dist[v], v});
            }
        }
    }
    return dist;
}

// --- Union-Find (DSU) ---
struct DSU {
    vi parent, rank_;
    int components;
    DSU(int n): parent(n), rank_(n,0), components(n) { iota(ALL(parent), 0); }
    int find(int x) { return parent[x]==x ? x : parent[x]=find(parent[x]); }
    bool unite(int a, int b) {
        a=find(a); b=find(b);
        if (a==b) return false;
        if (rank_[a]<rank_[b]) swap(a,b);
        parent[b]=a;
        if (rank_[a]==rank_[b]) rank_[a]++;
        components--;
        return true;
    }
    bool connected(int a, int b) { return find(a)==find(b); }
};

// --- Grid helpers (4-dir and 8-dir) ---
const int dx4[] = {0,0,1,-1};
const int dy4[] = {1,-1,0,0};
const int dx8[] = {0,0,1,-1,1,1,-1,-1};
const int dy8[] = {1,-1,0,0,1,-1,1,-1};

// ─────────────────────────────────────────────────────────────────────────────
// 7. DATA STRUCTURES
// ─────────────────────────────────────────────────────────────────────────────

// --- Fenwick Tree (Binary Indexed Tree) ---
struct BIT {
    int n; vll tree;
    BIT(int n): n(n), tree(n+1,0) {}
    void update(int i, ll v) { for(++i; i<=n; i+=i&-i) tree[i]+=v; }
    ll query(int i) { ll s=0; for(++i; i>0; i-=i&-i) s+=tree[i]; return s; }
    ll query(int l, int r) { return query(r)-(l?query(l-1):0); }
};

// --- Segment Tree (range max / point update) ---
struct SegTree {
    int n; vi tree;
    SegTree(int n): n(n), tree(2*n, 0) {}
    void update(int i, int v) { for(tree[i+=n]=v; i>1; i>>=1) tree[i>>1]=max(tree[i],tree[i^1]); }
    int query(int l, int r) { // [l, r)
        int res=0;
        for(l+=n,r+=n; l<r; l>>=1,r>>=1) {
            if(l&1) res=max(res,tree[l++]);
            if(r&1) res=max(res,tree[--r]);
        }
        return res;
    }
};

// --- LRU Cache (useful for memoization with eviction) ---
template<class K, class V>
struct LRUCache {
    int cap;
    list<pair<K,V>> lst;
    unordered_map<K, typename list<pair<K,V>>::iterator> mp;
    LRUCache(int cap): cap(cap) {}
    optional<V> get(K key) {
        auto it = mp.find(key);
        if (it == mp.end()) return nullopt;
        lst.splice(lst.begin(), lst, it->second);
        return it->second->second;
    }
    void put(K key, V val) {
        auto it = mp.find(key);
        if (it != mp.end()) lst.erase(it->second);
        lst.push_front({key, val});
        mp[key] = lst.begin();
        if ((int)mp.size() > cap) { mp.erase(lst.back().first); lst.pop_back(); }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 8. STRING UTILITIES
// ─────────────────────────────────────────────────────────────────────────────

// Split string by delimiter
vector<string> split(const string& s, char delim) {
    vector<string> res;
    stringstream ss(s);
    string tok;
    while (getline(ss, tok, delim)) res.PB(tok);
    return res;
}

// Trim whitespace
string trim(string s) {
    s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char c){ return !isspace(c); }));
    s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !isspace(c); }).base(), s.end());
    return s;
}

// KMP pattern search → returns all match positions
vi kmp(const string& text, const string& pat) {
    string s = pat + "#" + text;
    int n = SZ(s);
    vi lps(n, 0);
    for (int i = 1; i < n; ++i) {
        int j = lps[i-1];
        while (j && s[i] != s[j]) j = lps[j-1];
        lps[i] = j + (s[i] == s[j]);
    }
    vi res;
    int m = SZ(pat);
    for (int i = m+1; i < n; ++i)
        if (lps[i] == m) res.PB(i - 2*m);
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. SEARCH & OPTIMIZATION
// ─────────────────────────────────────────────────────────────────────────────

// --- A* Search ---
// Usage: define State, heuristic, getNeighbors, isGoal, then call astar()
template<class State, class Hash = hash<State>>
struct AStar {
    using Cost = double;
    struct Node {
        Cost f, g;
        State state;
        bool operator>(const Node& o) const { return f > o.f; }
    };

    // Returns {cost, path} or {-1, {}} if no path found
    pair<Cost, vector<State>> search(
        State start,
        function<bool(State)>             isGoal,
        function<Cost(State)>             heuristic,
        function<vector<pair<State,Cost>>(State)> neighbors
    ) {
        unordered_map<State, Cost, Hash> gScore;
        unordered_map<State, State, Hash> parent;
        priority_queue<Node, vector<Node>, greater<Node>> open;

        gScore[start] = 0;
        open.push({heuristic(start), 0, start});

        while (!open.empty()) {
            auto [f, g, cur] = open.top(); open.pop();
            if (isGoal(cur)) {
                // Reconstruct path
                vector<State> path;
                for (State s = cur; parent.count(s); s = parent[s]) path.PB(s);
                path.PB(start);
                reverse(ALL(path));
                return {g, path};
            }
            if (gScore.count(cur) && gScore[cur] < g) continue;
            for (auto [next, cost] : neighbors(cur)) {
                Cost ng = g + cost;
                if (!gScore.count(next) || ng < gScore[next]) {
                    gScore[next] = ng;
                    parent[next] = cur;
                    open.push({ng + heuristic(next), ng, next});
                }
            }
        }
        return {-1, {}};
    }
};

// --- Beam Search ---
// Keep only the best `beamWidth` states at each step
template<class State>
vector<State> beamSearch(
    vector<State> initial,
    int beamWidth,
    int maxDepth,
    function<double(State)>          score,
    function<vector<State>(State)>   expand,
    function<bool(State)>            isTerminal
) {
    vector<State> beam = initial;
    vector<State> best;
    double bestScore = -1e18;

    for (int d = 0; d < maxDepth && !beam.empty(); ++d) {
        vector<pair<double, State>> candidates;
        for (auto& s : beam) {
            if (isTerminal(s)) {
                double sc = score(s);
                if (sc > bestScore) { bestScore = sc; best = {s}; }
                continue;
            }
            for (auto& ns : expand(s))
                candidates.push_back({score(ns), ns});
        }
        // Sort and keep best beamWidth
        sort(candidates.begin(), candidates.end(), [](auto& a, auto& b){ return a.first > b.first; });
        if ((int)candidates.size() > beamWidth) candidates.resize(beamWidth);
        beam.clear();
        for (auto& [sc, s] : candidates) beam.PB(s);
    }
    return best.empty() ? beam : best;
}

// Simulated Annealing helper
double saTemp(double t0, double tMin, double elapsed, double totalTime) {
    return tMin + (t0 - tMin) * (1.0 - elapsed / totalTime);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. MONTE CARLO TREE SEARCH (MCTS) — skeleton
// ─────────────────────────────────────────────────────────────────────────────
/*
 * Adapt GameState to your specific game.
 * Call MCTS::bestAction(rootState, timeLimitMs) from your game loop.
 */
struct GameState {
    // TODO: define your game state here
    bool isTerminal() const { return false; }
    vector<int> getLegalActions() const { return {}; }
    GameState applyAction(int action) const { return *this; }
    double rollout() const { return 0.0; } // returns score in [0,1] from current player perspective
    int currentPlayer() const { return 0; }
};

struct MCTSNode {
    GameState state;
    int action = -1;
    MCTSNode* parent = nullptr;
    vector<unique_ptr<MCTSNode>> children;
    double totalScore = 0.0;
    int visits = 0;
    vector<int> untriedActions;

    MCTSNode(GameState s, int act=-1, MCTSNode* par=nullptr)
        : state(s), action(act), parent(par), untriedActions(s.getLegalActions()) {}

    bool isFullyExpanded() const { return untriedActions.empty(); }
    bool isLeaf()          const { return children.empty(); }

    double ucb1(double C = 1.41) const {
        if (visits == 0) return 1e18;
        return totalScore / visits + C * sqrt(log(parent->visits) / visits);
    }

    MCTSNode* bestChild(double C=1.41) {
        return max_element(children.begin(), children.end(),
            [C](auto& a, auto& b){ return a->ucb1(C) < b->ucb1(C); })->get();
    }

    MCTSNode* expand() {
        int idx = rand() % SZ(untriedActions);
        int act = untriedActions[idx];
        untriedActions.erase(untriedActions.begin() + idx);
        children.push_back(make_unique<MCTSNode>(state.applyAction(act), act, this));
        return children.back().get();
    }
};

struct MCTS {
    static int bestAction(GameState root, int timeLimitMs) {
        auto start = chrono::steady_clock::now();
        auto node = make_unique<MCTSNode>(root);
        int iterations = 0;

        while (true) {
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start).count() >= timeLimitMs) break;

            // 1. Selection
            MCTSNode* cur = node.get();
            while (!cur->isLeaf() && cur->isFullyExpanded())
                cur = cur->bestChild();

            // 2. Expansion
            if (!cur->state.isTerminal() && !cur->isFullyExpanded())
                cur = cur->expand();

            // 3. Simulation (rollout)
            double score = cur->state.rollout();

            // 4. Backpropagation
            while (cur) {
                cur->visits++;
                cur->totalScore += score;
                cur = cur->parent;
            }
            iterations++;
        }

        cerr << "[MCTS] iterations=" << iterations << "\n";
        return node->bestChild(0)->action; // exploit best child (C=0)
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 11. SIMULATION / GAME HELPERS
// ─────────────────────────────────────────────────────────────────────────────

// Wall-clock timer (for staying within turn time limits)
struct Timer {
    chrono::time_point<chrono::steady_clock> start;
    Timer() { reset(); }
    void reset() { start = chrono::steady_clock::now(); }
    ll ms()  const { return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()-start).count(); }
    ll us()  const { return chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now()-start).count(); }
    bool exceeded(ll limitMs) const { return ms() >= limitMs; }
};

// Fast random (xorshift64)
struct RNG {
    ull state;
    RNG(ull seed = 42): state(seed ? seed : 1ULL) {}
    ull next() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
    int nextInt(int lo, int hi) { return lo + next() % (ull)(hi - lo + 1); } // [lo, hi]
    double nextDouble() { return (next() >> 11) * (1.0 / (1ULL << 53)); }    // [0, 1)
    template<class T> void shuffle(vector<T>& v) { for(int i=SZ(v)-1;i>0;--i) swap(v[i],v[nextInt(0,i)]); }
};

// Grid helper (row-major 2D grid)
template<class T>
struct Grid {
    int rows, cols;
    vector<T> data;
    Grid(int r, int c, T def=T{}): rows(r), cols(c), data(r*c, def) {}
    T& at(int r, int c)             { return data[r*cols+c]; }
    T  at(int r, int c) const       { return data[r*cols+c]; }
    bool valid(int r, int c) const  { return r>=0 && r<rows && c>=0 && c<cols; }
    void fill(T v)                  { std::fill(ALL(data), v); }
    void print() const {
        for (int r=0;r<rows;++r){
            for (int c=0;c<cols;++c) cerr << at(r,c) << " ";
            cerr << "\n";
        }
    }
};
