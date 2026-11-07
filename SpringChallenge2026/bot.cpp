// CodinGame troll resource-gathering bot.

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <type_traits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
using namespace std;

// ---- node string helpers ---------------------------------------------------
static inline int nx(const string& s) { return atoi(s.c_str()); }
static inline int ny(const string& s) {
    size_t p = s.find(' ');
    return p == string::npos ? 0 : atoi(s.c_str() + p + 1);
}
static inline string mk(int x, int y) { return to_string(x) + " " + to_string(y); }

// ---- numeric helpers (ceil / floor / round-half-away-from-zero semantics) ----
static inline int ceildiv_nonneg(long a, long b) {  // ceil(a/b) for a>=0, b>0
    if (b <= 0) return 1000000;
    if (a <= 0) return 0;
    return (int)((a + b - 1) / b);
}
static inline double round2(double x) {  // round to 2 decimals, half away (x>=0 here)
    if (x >= 0) return floor(x * 100.0 + 0.5) / 100.0;
    return ceil(x * 100.0 - 0.5) / 100.0;
}
static inline long round_half_away(double x) {
    return (x >= 0) ? (long)floor(x + 0.5) : (long)ceil(x - 0.5);
}
static inline string joinv(const vector<string>& v, const string& sep) {
    string r;
    for (size_t i = 0; i < v.size(); ++i) { if (i) r += sep; r += v[i]; }
    return r;
}

// stable argmin / argmax returning index (-1 if empty); keeps the first extremum
template <class T, class K>
int argminBy(const vector<T>& v, K keyfn) {
    int bi = -1;
    optional<invoke_result_t<K, const T&>> bk;
    for (int i = 0; i < (int)v.size(); ++i) {
        auto k = keyfn(v[i]);
        if (!bk || k < *bk) { bi = i; bk = k; }
    }
    return bi;
}
template <class T, class K>
int argmaxBy(const vector<T>& v, K keyfn) {
    int bi = -1;
    optional<invoke_result_t<K, const T&>> bk;
    for (int i = 0; i < (int)v.size(); ++i) {
        auto k = keyfn(v[i]);
        if (!bk || k > *bk) { bi = i; bk = k; }
    }
    return bi;
}

static inline void debug(const string&) {}  // diagnostics are intentionally silent

// ===========================================================================
//  Grid: cell graph with bidirectional-BFS shortest paths
// ===========================================================================
struct Grid {
    int width = 0, height = 0;
    unordered_map<string, vector<string>> structure;  // adjacency, insertion-ordered

    Grid() {}
    Grid(int w, int h, bool fill) : width(w), height(h) {
        if (fill) {
            for (int x = 0; x < width; ++x)
                for (int y = 0; y < height; ++y) add_cell(mk(x, y));
        }
    }

    bool cell_outside_norms(int x, int y) const {
        return x < 0 || y < 0 || x > width - 1 || y > height - 1;
    }
    void addEdgeOrdered(const string& a, const string& b) {  // set-like, first insert wins
        auto& v = structure[a];
        for (auto& n : v) if (n == b) return;
        v.push_back(b);
    }
    // NEIGHBORS order is fixed N, E, S, W; this ordering drives deterministic tie-breaks
    void add_cell(const string& node) {
        static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        structure[node];  // ensure present
        int X = nx(node), Y = ny(node);
        for (int k = 0; k < 4; ++k) {
            int ax = X + dx[k], ay = Y + dy[k];
            if (cell_outside_norms(ax, ay)) continue;
            string nb = mk(ax, ay);
            addEdgeOrdered(node, nb);
            addEdgeOrdered(nb, node);
        }
    }
    void remove_cell(const string& cell) {
        auto it = structure.find(cell);
        if (it == structure.end()) return;
        vector<string> nbrs = it->second;
        for (auto& other : nbrs) {
            auto oit = structure.find(other);
            if (oit == structure.end()) continue;
            auto& ov = oit->second;
            ov.erase(remove(ov.begin(), ov.end(), cell), ov.end());
            if (ov.empty()) structure.erase(other);
        }
        structure.erase(cell);
    }
    void remove_connection(const string& from, const string& to) {
        auto it = structure.find(from);
        if (it == structure.end()) return;
        auto& v = it->second;
        v.erase(remove(v.begin(), v.end(), to), v.end());
    }
    vector<string>& neighbors(const string& node) { return structure[node]; }  // auto-vivifies (empty)

    vector<string> n4(const string& node) {
        static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        vector<string> r;
        int X = nx(node), Y = ny(node);
        for (int k = 0; k < 4; ++k) {
            int ax = X + dx[k], ay = Y + dy[k];
            if (!cell_outside_norms(ax, ay)) r.push_back(mk(ax, ay));
        }
        return r;
    }
    int manhattan_distance(const string& a, const string& b) const {
        return abs(nx(a) - nx(b)) + abs(ny(a) - ny(b));
    }
    // Heuristic used to order neighbor expansion. Note the mid node is built with
    // an underscore, so its parsed y is 0 and its parsed x is the two mid
    // coordinates are concatenated as digit strings (the '_' separator is dropped).
    int manhattan_distance_from_mid(const string& node) const {
        int X = nx(node), Y = ny(node);
        int cmx = (width % 2 == 1) ? (width / 2)
                  : (abs(X - (width / 2)) <= abs(X - (width / 2 - 1)) ? (width / 2) : (width / 2 - 1));
        int cmy = (height % 2 == 1) ? (height / 2)
                  : (abs(Y - (height / 2)) <= abs(Y - (height / 2 - 1)) ? (height / 2) : (height / 2 - 1));
        long midx = atol((to_string(cmx) + to_string(cmy)).c_str());  // concatenated digits
        return (int)labs((long)X - midx) + abs(Y);                    // mid.y == 0
    }

    // ---- bidirectional BFS --------------------------------------------------
    string expand_layer(deque<string>& queue,
                        unordered_map<string, string>& visited,
                        unordered_map<string, string>& other_visited) {
        string current = queue.front();
        queue.pop_front();
        vector<string> ns = structure[current];
        stable_sort(ns.begin(), ns.end(), [&](const string& a, const string& b) {
            return manhattan_distance_from_mid(a) < manhattan_distance_from_mid(b);
        });
        for (auto& nb : ns) {
            if (visited.count(nb)) continue;
            visited[nb] = current;
            if (other_visited.count(nb)) return nb;  // intersection
            queue.push_back(nb);
        }
        return "";
    }
    vector<string> build_path(const string& intersect,
                              unordered_map<string, string>& fv,
                              unordered_map<string, string>& bv) {
        vector<string> path;
        string cur = intersect;
        while (!cur.empty()) {
            path.insert(path.begin(), cur);
            auto it = fv.find(cur);
            cur = (it != fv.end()) ? it->second : "";
        }
        auto it = bv.find(intersect);
        cur = (it != bv.end()) ? it->second : "";
        while (!cur.empty()) {
            path.push_back(cur);
            auto j = bv.find(cur);
            cur = (j != bv.end()) ? j->second : "";
        }
        return path;
    }
    // excluding: "" for none, else a single blocked node
    vector<string> shortest_path(const string& start, const string& goal, const string& excluding) {
        if (start == goal) return {start};
        deque<string> fq{start}, bq{goal};
        unordered_map<string, string> fv, bv;
        if (!excluding.empty()) { fv[excluding] = ""; bv[excluding] = ""; }
        fv[start] = "";
        bv[goal] = "";
        while (true) {
            if (!fq.empty()) {
                string inter = expand_layer(fq, fv, bv);
                if (!inter.empty()) return build_path(inter, fv, bv);
            }
            if (!bq.empty()) {
                string inter = expand_layer(bq, bv, fv);
                if (!inter.empty()) return build_path(inter, fv, bv);
            }
            if (fq.empty() || bq.empty()) return {};  // no path
        }
    }
};

// ===========================================================================
//  Data structs
// ===========================================================================
struct Camp {
    bool my = false;
    int x = 0, y = 0;
    string node() const { return mk(x, y); }
};

struct Inventory {
    int plum = 0, lemon = 0, apple = 0, banana = 0, iron = 0, wood = 0;
    int of(const string& t) const {
        if (t == "PLUM") return plum;
        if (t == "LEMON") return lemon;
        if (t == "APPLE") return apple;
        if (t == "BANANA") return banana;
        if (t == "IRON") return iron;
        if (t == "WOOD") return wood;
        return 0;
    }
    bool has(const string& t) const { return of(t) > 0; }
    bool can_afford(const map<string, int>& cost) const {
        for (auto& kv : cost) if (of(kv.first) < kv.second) return false;
        return true;
    }
    int score() const { return plum + lemon + apple + banana + 4 * wood; }
    int best_affordable_train_tier(const string& t, int allowed_max, int ewc) const {
        int store = of(t);
        if (store >= ewc + 9) return min(allowed_max, 3);
        if (store >= ewc + 4) return min(allowed_max, 2);
        if (store >= ewc + 1) return 1;
        return 0;
    }
    // "m c h ch" or "" if any tier unaffordable
    string best_intermediate_worker(int ewc) const {
        int p = best_affordable_train_tier("PLUM", 3, ewc);
        int carry = best_affordable_train_tier("LEMON", 2, ewc);
        int a = best_affordable_train_tier("APPLE", carry, ewc);
        int ir = best_affordable_train_tier("IRON", carry, ewc);
        if (p == 0 || carry == 0 || a == 0 || ir == 0) return "";
        return to_string(p) + " " + to_string(carry) + " " + to_string(a) + " " + to_string(ir);
    }
};

struct Worker;  // forward

struct Tree {
    string type;
    int x = 0, y = 0, size = 0, health = 0, fruits = 0, cooldown = 0, period = 0;

    string node() const { return mk(x, y); }
    bool type_is(const string& t) const { return type == t; }
    bool fruit() const { return fruits >= 1; }
    bool grown() const { return size == 4; }

    static int increment(const string& t) {
        if (t == "APPLE") return 3;
        if (t == "BANANA") return 1;
        return 2;  // PLUM, LEMON
    }
    static int health_at(const string& t, int sz) {
        if (t == "PLUM" || t == "LEMON") { static const int h[5] = {0, 6, 8, 10, 12}; return h[sz]; }
        if (t == "APPLE") { static const int h[5] = {0, 11, 14, 17, 20}; return h[sz]; }
        static const int h[5] = {0, 3, 4, 5, 6};  // BANANA
        return h[sz];
    }
    void apply_turn() {
        cooldown -= 1;
        if (cooldown > 0) return;
        cooldown = period;
        if (size < 4) { size += 1; health += increment(type); }
    }
    bool damaged() const { return health < health_at(type, size); }
    bool choppable_for_full_yield(int chop_power) const {
        if (grown()) return true;
        Tree copy = *this;
        while (true) {
            copy.health -= chop_power;
            if (copy.health <= 0) return copy.size == 4;
            copy.apply_turn();
        }
    }
    int turns_till_fruit() const {
        if (fruit()) return 0;
        return (4 - size) * period + cooldown;
    }
    int chop_turns(int chop_speed) const {
        if (chop_speed < 1) return 99;
        if (health <= chop_speed) return 1;
        Tree copy = *this;
        int turns = 1;
        while (true) {
            copy.health -= chop_speed;
            if (copy.health <= 0) return turns;
            copy.apply_turn();
            turns += 1;
        }
    }
    int turns_till_size(int desired) const {
        if (size >= desired) return 0;
        int diff = desired - size;
        int growth_periods = diff - 1;
        return max(growth_periods, 0) * period + cooldown;
    }
    int fruits_at_arrival(int turns) const {
        if (fruits == 3) return 3;
        int turns_to_grow = grown() ? 0 : cooldown + (4 - (size + 1)) * period;
        if (turns < turns_to_grow) return 0;
        int fruiting_turns = turns - turns_to_grow;
        int starting_cd = grown() ? cooldown : period;
        int cd_left = max(starting_cd - fruiting_turns, 0);
        if (cd_left > 0) return fruits;
        int grown_fruits = 1;
        fruiting_turns -= starting_cd;
        int fruiting_periods = (int)floor(fruiting_turns / (double)period);
        return min(3, fruits + fruiting_periods + grown_fruits);
    }
    // worker-dependent helpers (move_speed / harvest_power / carry_capacity passed in)
    int turns_till_fruit_in_hand(int move_speed, int path_size) const {
        int distance_penalty = ceildiv_nonneg(path_size - 1, move_speed);
        if (fruits > 0) return distance_penalty + 1;
        int cooldown_penalty = (4 - size) * period + cooldown;
        return max(distance_penalty, cooldown_penalty) + 1;
    }
    int turns_till_chop(int move_speed, int chop_power, int path_size) const {
        int move_turns = ceildiv_nonneg(path_size - 1, move_speed);
        return move_turns + chop_turns(chop_power);
    }
    double average_fruit_yield(int distance_to_camp, int move_speed, int carry_capacity, int harvest_power) const {
        int move_turns = ceildiv_nonneg(distance_to_camp - 1, move_speed);
        int travel_turns = move_turns * 2 + 1;
        int min_cycle = travel_turns + 1;
        if (min_cycle < period) return 1.0 / period;
        double available_fruit = min(3.0, min_cycle / (double)period);
        double carried_fruit = min(available_fruit, (double)carry_capacity);
        double harvest_turns = carried_fruit / (double)harvest_power;
        double cycle_length = travel_turns + harvest_turns;
        return round2(carried_fruit / cycle_length);
    }
};

struct Worker {
    int id = 0, player = 0, x = 0, y = 0;
    int move_speed = 0, carry_capacity = 0, harvest_power = 0, chop_power = 0;
    int carry_plum = 0, carry_lemon = 0, carry_apple = 0, carry_banana = 0, carry_iron = 0, carry_wood = 0;

    string node() const { return mk(x, y); }
    bool my() const { return player == 0; }
    int carriedSum() const {
        return carry_plum + carry_lemon + carry_apple + carry_banana + carry_iron + carry_wood;
    }
    bool full() const { return carriedSum() >= carry_capacity; }
    int carryOf(const string& t) const {
        if (t == "PLUM") return carry_plum;
        if (t == "LEMON") return carry_lemon;
        if (t == "APPLE") return carry_apple;
        if (t == "BANANA") return carry_banana;
        if (t == "IRON") return carry_iron;
        if (t == "WOOD") return carry_wood;
        return 0;
    }
    bool carrying(const string& t, int at_least = 1) const { return carryOf(t) >= at_least; }
    bool carry_seed() const {
        return carrying("PLUM") || carrying("LEMON") || carrying("APPLE") || carrying("BANANA");
    }
    string carried_seed() const {
        static const char* order[4] = {"PLUM", "LEMON", "APPLE", "BANANA"};
        for (auto t : order) if (carrying(t)) return t;
        return "";
    }
    int free_capacity() const { return carry_capacity - carriedSum(); }
    bool can_chop() const { return chop_power > 0; }
    bool can_harvest() const { return harvest_power > 0; }
    int mining_turns() const { return ceildiv_nonneg(carry_capacity, chop_power); }
};

struct Cell {
    int x = 0, y = 0;
    int tree = -1, worker = -1, opp = -1;  // indices into per-turn vectors
};

struct Plan {
    string name;
    int worker_id = 0;
    string type;  // "" when none
    string node;  // "x y" for MOVE/CHOP, "" otherwise
    string command() const {
        if (name == "MOVE") return "MOVE " + to_string(worker_id) + " " + node;
        string r = name + " " + to_string(worker_id);
        if (!type.empty()) r += " " + type;
        return r;
    }
    bool name_is(const string& n) const { return name == n; }
    bool chop() const { return name == "CHOP"; }
};

struct Prediction {
    bool valid = false;
    int move = 0, carry = 0, harvest = 0, chop = 0;
    map<string, int> costs;
    int turns = 0, remaining_turns = 0;

    string name() const {
        return to_string(move) + " " + to_string(carry) + " " + to_string(harvest) + " " + to_string(chop);
    }
    int cost() const {
        int s = 0;
        for (auto& kv : costs) if (kv.first != "IRON") s += kv.second;
        return s;
    }
    int wood_points_per_cycle() const { return 4 * carry; }
    int cycle_length() const {
        int base = (chop >= 1) ? ceildiv_nonneg(6, chop) : 1000000;
        return base + 3 + (move < 2 ? 1 : 0);
    }
    int chop_cycles() const {
        int cl = cycle_length();
        if (cl <= 0) return 0;
        return (int)floor(remaining_turns / (double)cl);
    }
    int chop_points() const { return chop_cycles() * wood_points_per_cycle(); }
    long grand_total() const { return (long)chop_points() - round_half_away(cost() * 1.5); }
    // [m, c, existing_worker_count, ch]; sort key uses only the first three (a known quirk)
    vector<array<int, 4>> satisfyable_tiers(int ewc) const {
        vector<array<int, 4>> v;
        for (int m = 1; m <= move; ++m)
            for (int c = 1; c <= carry; ++c)
                for (int ch = 1; ch <= chop; ++ch) v.push_back({m, c, ewc, ch});
        stable_sort(v.begin(), v.end(), [](const array<int, 4>& A, const array<int, 4>& B) {
            int am = A[0], ac = A[1], ak = A[2];
            int bm = B[0], bc = B[1], bk = B[2];
            array<int, 5> ka = {am + ac + ak, max({am, ac, ak}), ak, am, ac};
            array<int, 5> kb = {bm + bc + bk, max({bm, bc, bk}), bk, bm, bc};
            return ka < kb;
        });
        return v;
    }
};

// ===========================================================================
//  Controller
// ===========================================================================
struct Controller {
    string field;
    int turn = 0;
    Grid grid;
    Camp my_camp, opp_camp;
    bool has_opp_camp = false;
    Inventory my_inventory, opp_inventory;

    // per-turn state
    unordered_map<string, Cell> cells;
    vector<Tree> trees;
    vector<Worker> workers;
    int helperIdx = -1, interIdx = -1, chopperIdx = -1;

    vector<Prediction> predictions;
    Prediction best_prediction;  // .valid flags presence

    string training;
    unordered_map<int, Plan> plans;
    vector<int> planOrder;

    int distance_between_camps = 0;

    // ---- permanent caches (computed once) ----
    unordered_map<string, vector<string>> spCache;

    vector<string> grass_nodes, water_nodes, iron_nodes, wet_nodes, mining_nodes;
    unordered_set<string> grass_set, water_set, iron_set, wet_set, mining_set;

    bool have_n3c = false;
    vector<string> n3c_vec;          unordered_set<string> n3c_set;          // nodes_within_3_of_camp
    bool have_n3c_es = false;
    vector<string> n3c_es_vec;       unordered_set<string> n3c_es_set;       // except seed
    bool have_n3oc = false;
    vector<string> n3oc_vec;         unordered_set<string> n3oc_set;         // nodes_within_3_of_opp_camp
    bool have_w3c = false;
    vector<string> w3c_vec;          unordered_set<string> w3c_set;          // wet within 3 of camp
    bool have_mynodes = false;
    vector<string> mynodes_vec;      unordered_set<string> mynodes_set;
    vector<string> dropoff_vec;      unordered_set<string> dropoff_set;
    bool have_seed = false;          string seed_node_;
    bool have_spm = false;           vector<string> spm_path;                // shortest_path_to_mining

    // ---- per-turn memos ----
    int mw_turn = -1;  vector<Worker> mw_cache;     // my_workers
    int t3c_turn = -1; vector<Tree*> t3c_cache;     // trees_within_3_of_camp (pointers into trees)
    int ttolt_turn = -1; double ttolt_cache = 0;    // turns_till_own_lemon_tree

    static vector<string> splitlines(const string& s) {
        vector<string> r; string cur; 
        for (char c : s) { if (c == '\n') { r.push_back(cur); cur.clear(); } else cur += c; }
        r.push_back(cur);
        return r;
    }
    static vector<string> tokenize(const string& s) {
        vector<string> r; stringstream ss(s); string t;
        while (ss >> t) r.push_back(t);
        return r;
    }

    Controller(const string& f) : field(f) { init_grid(); init_warm(); }

    // ---- tree period mapping ----
    int tree_period(bool wet, const string& type) const {
        if (wet) { if (type == "PLUM") return 3; if (type == "LEMON") return 3; if (type == "APPLE") return 2; return 4; }
        if (type == "PLUM") return 8; if (type == "LEMON") return 8; if (type == "APPLE") return 9; return 6;
    }

    // ---- cell lookups ----
    Tree* treeAt(const string& node) {
        auto it = cells.find(node);
        if (it == cells.end() || it->second.tree < 0) return nullptr;
        return &trees[it->second.tree];
    }
    Worker* workerAt(const string& node) {
        auto it = cells.find(node);
        if (it == cells.end() || it->second.worker < 0) return nullptr;
        return &workers[it->second.worker];
    }
    Worker* oppAt(const string& node) {
        auto it = cells.find(node);
        if (it == cells.end() || it->second.opp < 0) return nullptr;
        return &workers[it->second.opp];
    }

    Worker* helper() { return helperIdx >= 0 ? &workers[helperIdx] : nullptr; }
    Worker* inter() { return interIdx >= 0 ? &workers[interIdx] : nullptr; }
    Worker* chopper() { return chopperIdx >= 0 ? &workers[chopperIdx] : nullptr; }

    // ---- cached shortest path with reverse + subpath seeding ----
    vector<string> sp(const string& from, const string& to, const string& excluding = "") {
        string key = from + "\x01" + to + "\x01" + excluding;
        vector<string> path;
        auto it = spCache.find(key);
        if (it != spCache.end()) path = it->second;
        else { path = grid.shortest_path(from, to, excluding); spCache[key] = path; }

        string rkey = to + "\x01" + from + "\x01" + excluding;
        if (!spCache.count(rkey)) {
            if (!path.empty()) spCache[rkey] = vector<string>(path.rbegin(), path.rend());
            else spCache[rkey] = vector<string>();
        }
        if (path.empty()) return {};

        if (excluding.empty() && path.size() >= 3) {
            {
                string k = path[0] + "\x01" + path[path.size() - 2] + "\x01";
                if (!spCache.count(k)) spCache[k] = vector<string>(path.begin(), path.end() - 1);
            }
            {
                string k = path[1] + "\x01" + path[path.size() - 1] + "\x01";
                if (!spCache.count(k)) spCache[k] = vector<string>(path.begin() + 1, path.end());
            }
        }
        return path;
    }
    int spSize(const string& from, const string& to, const string& excluding = "") {
        return (int)sp(from, to, excluding).size();
    }

    // ---- permanent geometry ----
    vector<string>& nodes_within_3_of_camp() {
        if (!have_n3c) {
            for (auto& g : grass_nodes)
                if (spSize(my_camp.node(), g) <= 4) { n3c_vec.push_back(g); n3c_set.insert(g); }
            have_n3c = true;
        }
        return n3c_vec;
    }
    vector<string>& nodes_within_3_of_opp_camp() {
        if (!have_n3oc) {
            for (auto& g : grass_nodes)
                if (spSize(opp_camp.node(), g) <= 4) { n3oc_vec.push_back(g); n3oc_set.insert(g); }
            have_n3oc = true;
        }
        return n3oc_vec;
    }
    vector<string>& wet_nodes_within_3_of_camp() {
        if (!have_w3c) {
            nodes_within_3_of_camp();
            for (auto& n : n3c_vec) if (wet_set.count(n)) { w3c_vec.push_back(n); w3c_set.insert(n); }
            have_w3c = true;
        }
        return w3c_vec;
    }
    bool node_secluded(const string& node) {
        return distance_between_camps + 10 < (spSize(opp_camp.node(), node) - 1);
    }
    string seed_node() {
        if (have_seed) return seed_node_;
        nodes_within_3_of_camp();
        wet_nodes_within_3_of_camp();
        auto neigh_in_n3c = [&](const string& n) {
            int c = 0; for (auto& x : grid.neighbors(n)) if (n3c_set.count(x)) ++c; return c;
        };
        if (!w3c_vec.empty()) {
            int bi = argminBy(w3c_vec, [&](const string& n) {
                int sc = (node_secluded(n) ? 1 : 0) + neigh_in_n3c(n);
                return make_tuple(-sc, spSize(my_camp.node(), n), -spSize(opp_camp.node(), n));
            });
            seed_node_ = w3c_vec[bi];
        } else {
            int bi = argminBy(n3c_vec, [&](const string& n) {
                return make_tuple(-neigh_in_n3c(n), spSize(my_camp.node(), n), -spSize(opp_camp.node(), n));
            });
            seed_node_ = n3c_vec[bi];
        }
        have_seed = true;
        return seed_node_;
    }
    vector<string>& nodes_within_3_of_camp_except_seed() {
        if (!have_n3c_es) {
            string sd = seed_node();
            for (auto& n : nodes_within_3_of_camp())
                if (n != sd) { n3c_es_vec.push_back(n); n3c_es_set.insert(n); }
            have_n3c_es = true;
        }
        return n3c_es_vec;
    }
    vector<string>& my_nodes() {
        if (!have_mynodes) {
            for (auto& g : grass_nodes) {
                int my_path = spSize(my_camp.node(), g) - 1;
                int opp_path = spSize(opp_camp.node(), g) - 1;
                if (my_path < opp_path && opp_path > distance_between_camps) {
                    mynodes_vec.push_back(g); mynodes_set.insert(g);
                }
            }
            have_mynodes = true;
        }
        return mynodes_vec;
    }
    vector<string>& dropoff_nodes() { return dropoff_vec; }
    vector<string> opp_dropoff_nodes() { return grid.neighbors(opp_camp.node()); }
    vector<string>& shortest_path_to_mining() {
        if (!have_spm) {
            vector<vector<string>> cands;
            for (auto& mn : mining_nodes)
                for (auto& n : grid.neighbors(my_camp.node())) cands.push_back(sp(n, mn));
            int bi = argminBy(cands, [](const vector<string>& p) { return (int)p.size(); });
            spm_path = (bi >= 0) ? cands[bi] : vector<string>();
            have_spm = true;
        }
        return spm_path;
    }

    // ---- per-turn memos ----
    vector<Worker>& my_workers() {
        if (mw_turn != turn) {
            mw_cache.clear();
            for (auto& w : workers) if (w.my()) mw_cache.push_back(w);
            mw_turn = turn;
        }
        return mw_cache;
    }
    vector<Worker*> opp_workers_ptrs() {
        vector<Worker*> r; for (auto& w : workers) if (!w.my()) r.push_back(&w); return r;
    }
    vector<Tree*>& trees_within_3_of_camp() {
        if (t3c_turn != turn) {
            t3c_cache.clear();
            nodes_within_3_of_camp();
            for (auto& t : trees) if (n3c_set.count(t.node())) t3c_cache.push_back(&t);
            t3c_turn = turn;
        }
        return t3c_cache;
    }

    // ---- plan helpers ----
    void setPlan(int id, const Plan& p) {
        if (!plans.count(id)) planOrder.push_back(id);
        plans[id] = p;
    }
    bool hasPlan(int id) { return plans.count(id) > 0; }
    Plan* getPlan(int id) { auto it = plans.find(id); return it == plans.end() ? nullptr : &it->second; }

    bool node_reserved_by_any_plan(const string& node) {
        for (int id : planOrder) {
            const Plan& plan = plans[id];
            if (plan.node == node && !node.empty()) return true;
            for (auto& w : my_workers()) if (w.id == plan.worker_id) { if (w.node() == node) return true; break; }
        }
        return false;
    }

    // ---- cost / predictions ----
    map<string, int> worker_cost(int move, int carry, int harvest, int chop) {
        int ew = (int)my_workers().size();
        map<string, int> m;
        m["PLUM"] = ew + move * move;
        m["LEMON"] = ew + carry * carry;
        m["APPLE"] = ew + harvest * harvest;
        m["IRON"] = ew + chop * chop;
        return m;
    }
    map<string, int> aimed_chopper_cost() { return best_prediction.costs; }

    bool use_shortscale() {
        if (chopper()) return false;
        return wet_nodes_within_3_of_camp().size() == 0;
    }

    double turns_to_gather(const string& type, int count) {
        if (count <= 0) return 0;
        if (type == "IRON") {
            vector<Worker> cand;
            for (auto& w : my_workers()) if (w.can_chop()) cand.push_back(w);
            stable_sort(cand.begin(), cand.end(), [](const Worker& a, const Worker& b) {
                return make_tuple(-a.move_speed, -a.carry_capacity, -a.chop_power) <
                       make_tuple(-b.move_speed, -b.carry_capacity, -b.chop_power);
            });
            int spmSize = (int)shortest_path_to_mining().size();
            double sum = 0;
            for (int i = 0; i < (int)cand.size(); ++i) {
                Worker& w = cand[i];
                int mining_cycle = ceildiv_nonneg(spmSize - 1, w.move_speed) * 2 + 1 + w.mining_turns();
                if (mining_cycle == 0) mining_cycle = 1;
                sum += (w.carry_capacity / (double)mining_cycle) * pow(0.8, i);
            }
            if (sum == 0) return 300;
            return ceil(count / sum);
        }
        // fruit
        vector<Worker> harv;
        for (auto& w : my_workers()) if (w.can_harvest()) harv.push_back(w);
        stable_sort(harv.begin(), harv.end(), [](const Worker& a, const Worker& b) {
            return make_tuple(-a.move_speed, -a.carry_capacity, -a.chop_power) <
                   make_tuple(-b.move_speed, -b.carry_capacity, -b.chop_power);
        });
        int penalty = -1;
        double sum = 0;
        bool ss = use_shortscale();
        bool wetAny = !wet_nodes_within_3_of_camp().empty();
        for (int i = 0; i < (int)harv.size(); ++i) {
            Worker& w = harv[i];
            // descending-average list, then take element at index i
            vector<pair<Tree*, double>> ty;
            for (auto& t : trees) if (t.type_is(type)) {
                int dist = spSize(my_camp.node(), t.node()) - 1;
                ty.push_back({&t, t.average_fruit_yield(dist, w.move_speed, w.carry_capacity, w.harvest_power)});
            }
            stable_sort(ty.begin(), ty.end(), [](const pair<Tree*, double>& a, const pair<Tree*, double>& b) {
                return -a.second < -b.second;
            });
            if (i < (int)ty.size()) {
                sum += ty[i].second * pow(0.8, i);
            } else {
                if (!my_inventory.has(type)) continue;
                if (ss) continue;
                if (wetAny) { if (penalty < 0) penalty = 15; sum += (1.0 / 5.0) * pow(0.8, i); }
                else { if (penalty < 0) penalty = 26; sum += (1.0 / 9.0) * pow(0.8, i); }
            }
        }
        if (sum == 0) return 300;
        return ceil(count / sum) + (penalty < 0 ? 0 : penalty);
    }

    Prediction predict(int move, int carry, int harvest, int chop) {
        map<string, int> costs = worker_cost(move, carry, harvest, chop);
        double t = turns_to_gather("PLUM", costs["PLUM"] - my_inventory.plum) +
                   turns_to_gather("LEMON", costs["LEMON"] - my_inventory.lemon) +
                   turns_to_gather("APPLE", costs["APPLE"] - my_inventory.apple) +
                   turns_to_gather("IRON", costs["IRON"] - my_inventory.iron);
        Prediction p;
        p.valid = true;
        p.move = move; p.carry = carry; p.harvest = harvest; p.chop = chop;
        p.costs = costs;
        p.turns = (int)t;
        p.remaining_turns = 300 - (turn + p.turns);
        return p;
    }

    void init_predictions() {
        predictions.clear();
        if (chopper()) return;
        vector<array<int, 4>> variants;
        if (use_shortscale()) {
            int existing = (int)my_workers().size();
            int move = my_inventory.best_affordable_train_tier("PLUM", 3, existing);
            int carry = my_inventory.best_affordable_train_tier("LEMON", 3, existing);
            int chop = 0;
            for (auto* w : opp_workers_ptrs()) chop = max(chop, w->chop_power);
            vector<array<int, 4>> all = {
                {move, carry, 0, chop}, {move + 1, carry, 0, chop}, {move, carry + 1, 0, chop},
                {move, carry, 0, chop + 1}, {move + 1, carry + 1, 0, chop}, {move + 1, carry, 0, chop + 1},
                {move, carry + 1, 0, chop + 1}, {move + 1, carry + 1, 0, chop + 1}};
            for (auto& d : all) if (!(d[1] > 3) && !(d[3] > 3)) variants.push_back(d);
        } else {
            variants = {{2, 4, 0, 3}, {2, 4, 0, 2}, {2, 3, 0, 3}, {2, 3, 0, 2}};
        }
        for (auto& v : variants) predictions.push_back(predict(v[0], v[1], v[2], v[3]));
        int bi = argminBy(predictions, [](const Prediction& p) {
            return make_tuple(-p.grand_total(), (long)p.turns);
        });
        best_prediction = (bi >= 0) ? predictions[bi] : Prediction{};
    }
    // TODO: check the best values
    int turns_till_chopper() {
        int spm = (int)shortest_path_to_mining().size();
        return -min(my_inventory.plum - 5, 0) * 5 +
               -min(my_inventory.lemon - 17, 0) * 5 +
               -min(my_inventory.apple - 1, 0) * 5 +
               -min(my_inventory.iron - 10, 0) * spm * 2;
    }

    string self_harvest_seed() {
        static const char* order[3] = {"BANANA", "PLUM", "LEMON"};
        for (auto t : order) if (my_inventory.has(t)) return t;
        return "";
    }

    string closest_dropoff(const string& from) {
        int bi = argminBy(dropoff_nodes(), [&](const string& d) { return spSize(from, d); });
        return bi >= 0 ? dropoff_nodes()[bi] : "";
    }
    vector<string> shortest_path_to_drop(const string& from) { return sp(from, closest_dropoff(from)); }
    int turns_to_drop(const Worker& w) {
        if (!w.full()) return 0;
        return ceildiv_nonneg((int)shortest_path_to_drop(w.node()).size() - 1, w.move_speed) + 1;
    }

    double turns_till_own_lemon_tree() {
        if (ttolt_turn == turn) return ttolt_cache;
        Worker* h = helper();
        // nearby lemon among trees within 3 of camp
        vector<Tree*> lem;
        for (auto* t : trees_within_3_of_camp()) if (t->type_is("LEMON")) lem.push_back(t);
        if (!lem.empty()) {
            int bi = argminBy(lem, [&](Tree* t) {
                return t->turns_till_fruit_in_hand(h->move_speed, spSize(h->node(), t->node()));
            });
            ttolt_cache = lem[bi]->turns_till_fruit_in_hand(h->move_speed, spSize(h->node(), lem[bi]->node()));
            ttolt_turn = turn; return ttolt_cache;
        }
        if (my_inventory.lemon > 0) {
            string example; int cdp;
            if (!wet_nodes_within_3_of_camp().empty()) { example = wet_nodes_within_3_of_camp()[0]; cdp = tree_period(true, "LEMON"); }
            else { example = dropoff_nodes()[0]; cdp = tree_period(false, "LEMON"); }
            Tree tmp; tmp.type = "LEMON"; tmp.x = nx(example); tmp.y = ny(example);
            tmp.size = 1; tmp.health = 8; tmp.fruits = 0; tmp.cooldown = cdp; tmp.period = cdp;
            ttolt_cache = tmp.turns_till_fruit_in_hand(h->move_speed, spSize(h->node(), example));
            ttolt_turn = turn; return ttolt_cache;
        }
        // grab seed from afar, return, plant
        double best = 1e18; bool found = false;
        for (auto& t : trees) if (t.type_is("LEMON")) {
            int tth = t.turns_till_fruit_in_hand(h->move_speed, spSize(h->node(), t.node()));
            int get_back = spSize(t.node(), my_camp.node()) - 2;
            string example; int cdp;
            if (!wet_nodes_within_3_of_camp().empty()) { example = wet_nodes_within_3_of_camp()[0]; cdp = tree_period(true, "LEMON"); }
            else { example = dropoff_nodes()[0]; cdp = tree_period(false, "LEMON"); }
            Tree tmp; tmp.type = "LEMON"; tmp.x = nx(example); tmp.y = ny(example);
            tmp.size = 1; tmp.health = 8; tmp.fruits = 0; tmp.cooldown = cdp; tmp.period = cdp;
            int new_growth = tmp.turns_till_fruit_in_hand(h->move_speed, spSize(h->node(), example));
            double total = (double)tth + get_back + new_growth;
            if (total < best) { best = total; found = true; }
        }
        ttolt_cache = found ? best : 300;
        ttolt_turn = turn; return ttolt_cache;
    }

    bool no_way_to_scale_to_chopper() {
        return chopper() == nullptr && my_inventory.lemon < 4 && (turns_till_own_lemon_tree() + turn) > 50;
    }

    // ---- movement primitives (all return true: a plan is set) ----
    bool go(Worker& worker, const string& node) {
        string excludable = "";
        Worker* ch = chopper();
        if (ch && worker.id != ch->id) {
            Plan* cp = getPlan(ch->id);
            if (cp && !cp->node.empty()) excludable = cp->node;
        }
        vector<string> path = sp(worker.node(), node, excludable);
        if (path.empty()) path = sp(worker.node(), node, "");
        string target;
        if ((int)path.size() > worker.move_speed) target = path[worker.move_speed];
        else if (!path.empty()) target = path.back();
        else target = worker.node();
        setPlan(worker.id, Plan{"MOVE", worker.id, "", target});
        return true;
    }
    bool go_and_mine(Worker& w, const string& node) {
        if (w.node() == node) setPlan(w.id, Plan{"MINE", w.id, "", ""});
        else { go(w, node); }
        return true;
    }
    bool go_and_harvest(Worker& w, const string& node) {
        if (w.node() == node) setPlan(w.id, Plan{"HARVEST", w.id, "", ""});
        else go(w, node);
        return true;
    }
    bool go_and_pick(Worker& w, const string& node, const string& type) {
        if (w.node() == node) setPlan(w.id, Plan{"PICK", w.id, type, ""});
        else go(w, node);
        return true;
    }
    bool go_and_plant(Worker& w, const string& node, const string& type) {
        if (node.empty()) { go_and_drop(w, closest_dropoff(w.node())); return true; }  // guard impossible-input
        if (w.node() == node) setPlan(w.id, Plan{"PLANT", w.id, type, ""});
        else go(w, node);
        return true;
    }
    bool go_and_chop(Worker& w, const string& node) {
        if (node.empty()) return true;
        if (w.node() == node) setPlan(w.id, Plan{"CHOP", w.id, "", node});
        else go(w, node);
        return true;
    }
    bool go_and_drop(Worker& w, const string& node) {
        if (node_reserved_by_any_plan(node)) {
            vector<string> path = sp(w.node(), node);
            if (((int)path.size() - 1) / (double)w.move_speed == 1.0) {
                vector<string> alt;
                for (auto& d : dropoff_nodes()) if (d != node) alt.push_back(d);
                int bi = argminBy(alt, [&](const string& d) { return spSize(w.node(), d); });
                if (bi >= 0) { go(w, alt[bi]); return true; }
            }
        }
        if (w.node() == node) setPlan(w.id, Plan{"DROP", w.id, "", ""});
        else go(w, node);
        return true;
    }

    // ---- gather / harvest / plant ----
    bool gather_iron(Worker& w) {
        if (w.full()) return go_and_drop(w, closest_dropoff(w.node()));
        int bi = argminBy(mining_nodes, [&](const string& n) { return spSize(w.node(), n); });
        return go_and_mine(w, bi >= 0 ? mining_nodes[bi] : w.node());
    }

    bool gather_initial_fruit(Worker& w, const string& fruit_type, int max_wait) {
        if (w.full()) return go_and_drop(w, closest_dropoff(w.node()));
        Tree* here = treeAt(w.node());
        if (here && here->type == fruit_type && here->fruit()) {
            setPlan(w.id, Plan{"HARVEST", w.id, "", ""});
            return true;
        }
        Worker* h = helper();
        vector<string> cand;
        for (auto& node : nodes_within_3_of_camp()) {
            Tree* t = treeAt(node);
            if (!t || !t->type_is(fruit_type)) continue;
            Plan* hp = h ? getPlan(h->id) : nullptr;
            if (hp && hp->name_is("HARVEST") && h->node() == node) continue;
            cand.push_back(node);
        }
        // among candidates with turns_till <= max_wait, min by turns_till
        int best = -1; int bestTurns = 0; vector<string> bestPath;
        for (auto& node : cand) {
            vector<string> path = sp(w.node(), node);
            int tt = treeAt(node)->turns_till_fruit_in_hand(w.move_speed, (int)path.size());
            if (tt > max_wait) continue;
            if (best < 0 || tt < bestTurns) { best = 1; bestTurns = tt; bestPath = path; }
        }
        if (best < 0) return false;
        go(w, bestPath.empty() ? w.node() : bestPath.back());
        return true;
    }

    bool gather_anywhere_fruit(Worker& w, const string& fruit_type, int max_wait) {
        if (w.full()) return go_and_drop(w, closest_dropoff(w.node()));
        Tree* here = treeAt(w.node());
        if (here && here->type == fruit_type && here->fruit()) {
            setPlan(w.id, Plan{"HARVEST", w.id, "", ""});
            return true;
        }
        Worker* h = helper();
        int best = -1; int bestTurns = 0; vector<string> bestPath;
        for (auto& t : trees) {
            if (!t.type_is(fruit_type)) continue;
            if (h) { Plan* hp = getPlan(h->id); if (hp && hp->name_is("HARVEST") && h->node() == t.node()) continue; }
            vector<string> path = sp(w.node(), t.node());
            int tt = t.turns_till_fruit_in_hand(w.move_speed, (int)path.size());
            if (tt > max_wait) continue;
            if (best < 0 || tt < bestTurns) { best = 1; bestTurns = tt; bestPath = path; }
        }
        if (best < 0) return false;
        go(w, bestPath.empty() ? w.node() : bestPath.back());
        return true;
    }

    bool gather_and_plant(Worker& w, const string& fruit_type) {
        if (w.carrying(fruit_type)) {
            vector<string> cand;
            for (auto& node : nodes_within_3_of_camp_except_seed()) {
                if (treeAt(node)) continue;
                if (spSize(opp_camp.node(), node) < 5) continue;
                cand.push_back(node);
            }
            int bi = argminBy(cand, [&](const string& node) {
                return spSize(w.node(), node) + spSize(my_camp.node(), node) -
                       (wet_set.count(node) ? 0.5 : 0.0);
            });
            if (bi < 0) { go_and_drop(w, closest_dropoff(w.node())); return true; }
            return go_and_plant(w, cand[bi], fruit_type);
        }
        // not carrying: get a seed fruit
        vector<Tree*> cand;
        for (auto& t : trees) if (t.type_is(fruit_type)) cand.push_back(&t);
        int bi = argminBy(cand, [&](Tree* t) {
            return make_tuple(t->turns_till_fruit_in_hand(w.move_speed, spSize(w.node(), t->node())),
                              -spSize(opp_camp.node(), t->node()));
        });
        if (bi < 0) return false;
        return go_and_harvest(w, cand[bi]->node());
    }

    bool harvest_already_stood_on_tree(Worker& w, const vector<string>& types) {
        if (w.full()) return false;
        Tree* t = treeAt(w.node());
        if (!t) return false;
        if (find(types.begin(), types.end(), t->type) == types.end()) return false;
        if (!t->fruit()) return false;
        return go_and_harvest(w, t->node());
    }

    bool handle_planting_at_end_of(Worker& w, const vector<string>& path, const string& tree_type) {
        if (path.empty()) return false;
        if (w.node() == path.back() && w.carrying(tree_type)) {
            setPlan(w.id, Plan{"PLANT", w.id, tree_type, ""});
            return true;
        } else if (w.carrying(tree_type)) {
            return go(w, path.back());
        } else if (path.size() >= 2 && w.node() == path[1] && !w.carrying(tree_type) && my_inventory.has(tree_type)) {
            setPlan(w.id, Plan{"PICK", w.id, tree_type, ""});
            return true;
        } else if (my_inventory.has(tree_type)) {
            return go(w, path.size() >= 2 ? path[1] : path.back());
        }
        return false;
    }

    bool ensure_sufficient_lemon_growth(Worker& w) {
        double prod = 0;
        for (auto& nn : nodes_within_3_of_camp()) {
            Tree* t = treeAt(nn);
            if (!t || t->type != "LEMON") continue;
            prod += wet_set.count(nn) ? (1.0 / 3.0) : (1.0 / 8.0);
        }
        if (prod >= (2.0 / 8.0)) return false;
        // wet candidate path
        vector<vector<string>> wetPaths;
        for (auto& n : wet_nodes_within_3_of_camp()) {
            Tree* t = treeAt(n);
            if (t == nullptr) wetPaths.push_back(sp(my_camp.node(), n));
        }
        int wb = argminBy(wetPaths, [&](const vector<string>& p) {
            const string& last = p.empty() ? string() : p.back();
            return make_tuple((double)p.size() - (node_secluded(last) ? 1.1 : 0.0),
                              (double)(-spSize(opp_camp.node(), last)));
        });
        if (wb >= 0) return handle_planting_at_end_of(w, wetPaths[wb], "LEMON");
        // regular
        struct Trip { vector<string> mp, wp, op; };
        vector<Trip> trips;
        for (auto& n : nodes_within_3_of_camp()) {
            Tree* t = treeAt(n);
            if (t == nullptr) trips.push_back({sp(my_camp.node(), n), sp(w.node(), n), sp(opp_camp.node(), n)});
        }
        int rb = argminBy(trips, [&](const Trip& t) {
            return (int)t.mp.size() + (int)t.wp.size() - (int)t.op.size();
        });
        if (rb >= 0) return handle_planting_at_end_of(w, trips[rb].mp, "LEMON");
        return false;
    }

    bool ensure_sufficient_plum_growth(Worker& w) {
        double prod = 0;
        for (auto& nn : nodes_within_3_of_camp()) {
            Tree* t = treeAt(nn);
            if (!t || t->type != "PLUM") continue;
            prod += wet_set.count(nn) ? (1.0 / 8.0) : (1.0 / 3.0);
        }
        if (prod >= (1.0 / 8.0)) return false;
        vector<vector<string>> wetPaths;
        for (auto& n : wet_nodes_within_3_of_camp()) {
            Tree* t = treeAt(n);
            if (t == nullptr) wetPaths.push_back(sp(my_camp.node(), n));
        }
        int wb = argminBy(wetPaths, [&](const vector<string>& p) {
            const string& last = p.empty() ? string() : p.back();
            return (double)p.size() - (node_secluded(last) ? 1.1 : 0.0);
        });
        if (wb >= 0) return handle_planting_at_end_of(w, wetPaths[wb], "PLUM");
        struct Trip { vector<string> mp, wp; };
        vector<Trip> trips;
        for (auto& n : nodes_within_3_of_camp()) {
            Tree* t = treeAt(n);
            if (t == nullptr) trips.push_back({sp(my_camp.node(), n), sp(w.node(), n)});
        }
        int rb = argminBy(trips, [&](const Trip& t) { return (int)t.mp.size() + (int)t.wp.size(); });
        if (rb >= 0) return handle_planting_at_end_of(w, trips[rb].mp, "PLUM");
        return false;
    }

    bool harvest_closest_harvestable(Worker& w, const vector<string>& except_nodes = {}) {
        Worker* ch = chopper();
        vector<Tree*> base;
        for (auto& t : trees) {
            if (find(except_nodes.begin(), except_nodes.end(), t.node()) != except_nodes.end()) continue;
            if (ch) { Plan* cp = getPlan(ch->id); if (cp && cp->chop() && cp->node == t.node()) continue; }
            if (t.node() == seed_node()) continue;
            if (t.size > 1) base.push_back(&t);
        }
        stable_sort(base.begin(), base.end(), [&](Tree* a, Tree* b) {
            return grid.manhattan_distance(w.node(), a->node()) < grid.manhattan_distance(w.node(), b->node());
        });
        if ((int)base.size() > 10) base.resize(10);
        vector<Tree*> reachable;
        for (auto* t : base) {
            int ttr = ceildiv_nonneg(spSize(w.node(), t->node()) - 1, w.move_speed);
            if (t->fruits_at_arrival(ttr) >= w.free_capacity()) reachable.push_back(t);
        }
        int bi = argminBy(reachable, [&](Tree* t) {
            return make_tuple(spSize(w.node(), t->node()), t->period);
        });
        if (bi >= 0) return go_and_harvest(w, reachable[bi]->node());
        return false;
    }

    bool seek_to_chop(Worker& w, const string& node) {
        if (w.full()) return go_and_drop(w, closest_dropoff(w.node()));
        return go_and_chop(w, node);
    }

    bool seek_to_plant_carried_banana(Worker& w) {
        if (w.carry_banana <= 0) return false;
        if (treeAt(seed_node()) == nullptr) return go_and_plant(w, seed_node(), "BANANA");
        vector<string> cand;
        for (auto& node : nodes_within_3_of_camp_except_seed()) if (treeAt(node) == nullptr) cand.push_back(node);
        int bi = argminBy(cand, [&](const string& node) {
            double a = spSize(w.node(), node) + spSize(my_camp.node(), node) + spSize(seed_node(), node) -
                       (wet_set.count(node) ? 0.5 : 0.0);
            return make_tuple(a, (double)(-spSize(opp_camp.node(), node)));
        });
        if (bi >= 0) return go_and_plant(w, cand[bi], "BANANA");
        return false;
    }

    void seek_to_self_plant(Worker& w) {
        nodes_within_3_of_camp();
        for (auto& ow : workers)
            if (!ow.my() && n3c_set.count(ow.node())) return;

        if (w.carry_seed()) {
            vector<string> cand;
            for (auto& n : my_nodes()) if (treeAt(n) == nullptr) cand.push_back(n);
            int bi = argminBy(cand, [&](const string& n) {
                return make_tuple(spSize(my_camp.node(), n), sp(w.node(), n));
            });
            if (bi >= 0) go_and_plant(w, cand[bi], w.carried_seed());
            return;
        }
        if (treeAt(w.node())) { go_and_chop(w, w.node()); return; }
        string seed = self_harvest_seed();
        if (!seed.empty()) {
            vector<string> candidates;
            for (auto& n : my_nodes()) {
                if (!dropoff_set.count(n)) continue;
                bool reserved = false;
                for (int id : planOrder) if (plans[id].node == n) { reserved = true; break; }
                if (!reserved) candidates.push_back(n);
            }
            string self_seeding_node;
            if (!candidates.empty()) {
                int bi = argminBy(candidates, [&](const string& n) { return spSize(w.node(), n); });
                self_seeding_node = candidates[bi];
            } else {
                int bi = argminBy(dropoff_nodes(), [&](const string& n) { return spSize(w.node(), n); });
                self_seeding_node = bi >= 0 ? dropoff_nodes()[bi] : "";
            }
            go_and_pick(w, self_seeding_node, seed);
        }
    }

    bool seek_to_plant_banana(Worker& w) {
        seek_to_plant_carried_banana(w);
        if (hasPlan(w.id)) return false;
        Tree* here = treeAt(w.node());
        if (here && here->type_is("BANANA") && here->fruit()) { go_and_harvest(w, here->node()); return true; }
        Tree* st = treeAt(seed_node());
        bool seeding_bananas = st && st->type_is("BANANA") &&
                               st->turns_till_fruit_in_hand(w.move_speed, spSize(w.node(), seed_node())) < 5;
        if (seeding_bananas) { go_and_harvest(w, seed_node()); return true; }
        if (my_inventory.banana > 0) { go_and_pick(w, closest_dropoff(w.node()), "BANANA"); return true; }
        // banana trees on map (trees vector preserves cell insertion order)
        vector<Tree*> bnodes;
        for (auto& t : trees) if (t.type_is("BANANA")) bnodes.push_back(&t);
        int bi = argminBy(bnodes, [&](Tree* t) {
            return t->turns_till_fruit_in_hand(w.move_speed, spSize(w.node(), t->node()));
        });
        if (bi >= 0) { go_and_harvest(w, bnodes[bi]->node()); return true; }
        return false;
    }

    bool training_is_chopper_train() {
        static const regex re("TRAIN \\d+ \\d+ 0");
        return regex_search(training, re);
    }

    void chop_wars(Worker& worker) {
        if (!worker.can_chop()) return;
        vector<Worker*> opp_chopping;
        for (auto& w : workers) {
            if (w.my()) continue;
            if (!w.can_chop()) continue;
            Tree* t = treeAt(w.node());
            if (t && t->damaged()) opp_chopping.push_back(&w);
        }
        if (opp_chopping.empty()) return;

        Tree* worker_tree = treeAt(worker.node());
        if (worker_tree && oppAt(worker.node()) && worker_tree->damaged()) {
            bool atDrop = dropoff_set.count(worker_tree->node()) > 0;
            if (atDrop && worker.full()) { go_and_drop(worker, worker.node()); return; }
            if (atDrop) {
                Worker* ow = oppAt(worker.node());
                int turns_to_fell = worker_tree->chop_turns(ow->chop_power);
                if (turns_to_fell >= 3 && worker_tree->fruit()) { go_and_harvest(worker, worker_tree->node()); return; }
            } else {
                Worker* ow = oppAt(worker.node());
                if (worker.chop_power > ow->chop_power) go_and_chop(worker, worker.node());
                else { go(worker, worker.node()); }
            }
        }

        struct Chop { vector<string> path; int to_reach; int to_fell; string oppnode; };
        vector<Chop> chops;
        for (auto* ow : opp_chopping) {
            Tree* t = treeAt(ow->node());
            vector<string> path = sp(worker.node(), ow->node());
            int to_reach = ceildiv_nonneg((int)path.size() - 1, worker.move_speed) + (turns_to_drop(worker) * 2);
            int to_fell = t->chop_turns(ow->chop_power);
            chops.push_back({path, to_reach, to_fell, ow->node()});
        }
        vector<Chop> interceptable;
        if ((int)trees.size() > 2) {
            for (auto& c : chops) if (c.to_fell < 5 && c.to_reach <= 3 && (c.to_reach + 1) <= c.to_fell) interceptable.push_back(c);
        } else {
            for (auto& c : chops) if (c.to_fell < 6 && c.to_reach <= 5 && (c.to_reach + 1) <= c.to_fell) interceptable.push_back(c);
        }
        if (!interceptable.empty()) {
            int bi = argmaxBy(interceptable, [&](const Chop& c) {
                Tree* t = treeAt(c.path.empty() ? c.oppnode : c.path.back());
                return t ? t->size : 0;
            });
            if (worker.full()) {
                vector<string> d = shortest_path_to_drop(worker.node());
                go_and_drop(worker, d.empty() ? worker.node() : d.back());
                return;
            }
            go_and_chop(worker, interceptable[bi].path.empty() ? interceptable[bi].oppnode : interceptable[bi].path.back());
            return;
        }
    }

    void organize_chopping(Worker& worker) {
        vector<Worker*> opp = opp_workers_ptrs();

        if (worker.carry_wood > 0 && worker.full()) {
            int best = INT_MAX; bool any = false;
            for (auto& t : trees) if (t.type_is("LEMON") && n3oc_set.count(t.node())) { best = min(best, t.turns_till_fruit()); any = true; }
            if (!any || best > 12) { go_and_drop(worker, closest_dropoff(worker.node())); return; }
        }

        if (inter() == nullptr && (int)my_workers().size() >= (int)opp.size() && !opp.empty()) {
            int maxOppCarry = 0; for (auto* w : opp) maxOppCarry = max(maxOppCarry, w->carry_capacity);
            if (worker.carry_capacity > maxOppCarry) {
                // wet lemon trees near opp
                vector<Tree*> wl;
                for (auto& t : trees) if (t.type_is("LEMON") && n3oc_set.count(t.node()) && wet_set.count(t.node())) wl.push_back(&t);
                int bi = argminBy(wl, [&](Tree* t) {
                    int d = t->turns_till_fruit_in_hand(worker.move_speed, spSize(worker.node(), t->node()));
                    return make_tuple(d, spSize(worker.node(), t->node()));
                });
                if (bi >= 0) { go_and_chop(worker, wl[bi]->node()); return; }
                vector<Tree*> dl;
                for (auto& t : trees) if (t.type_is("LEMON") && n3oc_set.count(t.node())) dl.push_back(&t);
                int dbi = argminBy(dl, [&](Tree* t) {
                    int d = t->turns_till_fruit_in_hand(worker.move_speed, spSize(worker.node(), t->node()));
                    return make_tuple(d, spSize(worker.node(), t->node()));
                });
                if (dbi >= 0) { go_and_chop(worker, dl[dbi]->node()); return; }
            }
        }

        bool anyGrown = false; for (auto& t : trees) if (t.grown()) { anyGrown = true; break; }
        if ((anyGrown ? worker.carry_wood > 0 : worker.full()) || (worker.full() && worker.carry_iron > 0)) {
            go_and_drop(worker, closest_dropoff(worker.node())); return;
        }

        chop_wars(worker);
        if (hasPlan(worker.id)) return;

        if (turn > 287) {
            vector<string> nb;
            for (auto& n : nodes_within_3_of_camp()) { Tree* t = treeAt(n); if (t && t->grown() && t->type_is("BANANA")) nb.push_back(n); }
            if (!nb.empty()) {
                int bi = argminBy(nb, [&](const string& n) { return spSize(worker.node(), n); });
                go_and_chop(worker, nb[bi]); return;
            }
            vector<string> nnb;
            for (auto& n : nodes_within_3_of_camp()) { Tree* t = treeAt(n); if (t && t->grown() && !t->type_is("BANANA")) nnb.push_back(n); }
            if (!nnb.empty()) {
                int bi = argminBy(nnb, [&](const string& n) { return spSize(worker.node(), n); });
                go_and_chop(worker, nnb[bi]); return;
            }
        }

        { Tree* st = treeAt(seed_node()); if (st && !st->type_is("BANANA")) { go_and_chop(worker, seed_node()); return; } }

        {
            vector<string> ch;
            for (auto& n : nodes_within_3_of_camp_except_seed()) {
                Tree* t = treeAt(n);
                if (t && t->type_is("BANANA") && t->choppable_for_full_yield(worker.chop_power)) ch.push_back(n);
            }
            if (!ch.empty()) { int bi = argminBy(ch, [&](const string& n) { return spSize(worker.node(), n); }); go_and_chop(worker, ch[bi]); return; }
        }
        {
            vector<string> gs;
            auto& sn = grid.neighbors(seed_node());
            unordered_set<string> snset(sn.begin(), sn.end());
            for (auto& n : nodes_within_3_of_camp_except_seed()) { Tree* t = treeAt(n); if (t && t->grown() && snset.count(n)) gs.push_back(n); }
            if (!gs.empty()) { int bi = argminBy(gs, [&](const string& n) { return spSize(worker.node(), n); }); go_and_chop(worker, gs[bi]); return; }
        }
        {
            vector<string> gr;
            for (auto& n : nodes_within_3_of_camp_except_seed()) { Tree* t = treeAt(n); if (t && t->grown()) gr.push_back(n); }
            if (!gr.empty()) { int bi = argminBy(gr, [&](const string& n) { return spSize(worker.node(), n); }); go_and_chop(worker, gr[bi]); return; }
        }
        {
            vector<string> gw;
            for (auto& n : nodes_within_3_of_camp_except_seed()) { Tree* t = treeAt(n); if (t && t->turns_till_size(4) <= 2) gw.push_back(n); }
            if (!gw.empty()) {
                int bi = argminBy(gw, [&](const string& n) { return treeAt(n)->turns_till_size(4); });
                string growest = gw[bi];
                if (worker.node() == growest) return;  // wait
                go(worker, growest); return;
            }
        }
        {
            vector<Tree*> cg;
            for (auto& t : trees) if (t.grown() && t.node() != seed_node()) cg.push_back(&t);
            int bi = argminBy(cg, [&](Tree* t) { return spSize(worker.node(), t->node()); });
            if (bi >= 0) { go_and_chop(worker, cg[bi]->node()); return; }
        }
        {
            vector<Tree*> all; for (auto& t : trees) all.push_back(&t);
            int bi = argminBy(all, [&](Tree* t) { return spSize(worker.node(), t->node()); });
            if (bi >= 0) { go_and_chop(worker, all[bi]->node()); return; }
        }
        auto odn = opp_dropoff_nodes();
        int bi = argminBy(odn, [&](const string& n) { return spSize(worker.node(), n); });
        go_and_chop(worker, bi >= 0 ? odn[bi] : worker.node());
    }

    void organize_helper(Worker& worker) {
        if (worker.carry_wood > 0 || (worker.full() && worker.carry_iron > 0)) {
            go_and_drop(worker, closest_dropoff(worker.node())); return;
        }
        chop_wars(worker);
        if (hasPlan(worker.id)) return;

        if (my_inventory.score() > opp_inventory.score() + 40 && (int)trees.size() < 6) {
            vector<Tree*> all; for (auto& t : trees) all.push_back(&t);
            int bi = argminBy(all, [&](Tree* t) { return sp(worker.node(), t->node()); });
            if (bi >= 0) seek_to_chop(worker, all[bi]->node());
        }
        if (hasPlan(worker.id)) return;

        if (use_shortscale() && chopper() == nullptr && best_prediction.valid) {
            for (auto& tier : best_prediction.satisfyable_tiers((int)my_workers().size())) {
                map<string, int> tc = worker_cost(tier[0], tier[1], tier[2], tier[3]);
                map<string, int> tcNoIron = tc; tcNoIron.erase("IRON");
                if (my_inventory.can_afford(tcNoIron)) continue;
                int L = tc["LEMON"], P = tc["PLUM"], A = tc["APPLE"];
                bool done = false;
                if (!done && my_inventory.lemon < L && gather_initial_fruit(worker, "LEMON", 1)) done = true;
                if (!done && my_inventory.plum < P && gather_initial_fruit(worker, "PLUM", 1)) done = true;
                if (!done && my_inventory.lemon < L && gather_initial_fruit(worker, "LEMON", 5)) done = true;
                if (!done && my_inventory.plum < P && gather_initial_fruit(worker, "PLUM", 5)) done = true;
                if (!done && my_inventory.apple < A && gather_initial_fruit(worker, "APPLE", 2)) done = true;
                if (!done && my_inventory.lemon < L && gather_initial_fruit(worker, "LEMON", 8)) done = true;
                if (!done && my_inventory.plum < P && gather_initial_fruit(worker, "PLUM", 8)) done = true;
                if (!done && my_inventory.apple < A && gather_initial_fruit(worker, "APPLE", 4)) done = true;
                if (!done && my_inventory.lemon < L && gather_anywhere_fruit(worker, "LEMON", 10)) done = true;
                if (!done && my_inventory.plum < P && gather_anywhere_fruit(worker, "PLUM", 10)) done = true;
                if (!done && my_inventory.apple < A && gather_anywhere_fruit(worker, "APPLE", 10)) done = true;
                if (hasPlan(worker.id)) break;
            }
            if (hasPlan(worker.id)) return;
            if (my_inventory.iron < aimed_chopper_cost()["IRON"]) gather_iron(worker);
        }
        if (hasPlan(worker.id)) return;

        if (chopper() == nullptr && !training_is_chopper_train() && best_prediction.valid) {
            auto& cost = best_prediction.costs;
            int L = cost["LEMON"], P = cost["PLUM"], A = cost["APPLE"], I = cost["IRON"];
            bool done = false;
            if (!done && seek_to_plant_carried_banana(worker)) done = true;
            if (!done && turn < 40 && my_inventory.lemon < L && ensure_sufficient_lemon_growth(worker)) done = true;
            if (!done && turn < 40 && my_inventory.plum < P && ensure_sufficient_plum_growth(worker)) done = true;
            if (!done) {
                vector<string> types;
                if (my_inventory.lemon < L) types.push_back("LEMON");
                if (my_inventory.plum < P) types.push_back("PLUM");
                if (harvest_already_stood_on_tree(worker, types)) done = true;
            }
            if (!done && my_inventory.lemon < L && gather_initial_fruit(worker, "LEMON", 5)) done = true;
            if (!done && my_inventory.plum < P && gather_initial_fruit(worker, "PLUM", 5)) done = true;
            if (!done && my_inventory.apple < A && gather_initial_fruit(worker, "APPLE", 2)) done = true;
            if (!done && my_inventory.iron < I && inter() == nullptr && gather_iron(worker)) done = true;
            if (!done && my_inventory.lemon >= L && my_inventory.plum >= P && gather_iron(worker)) done = true;
            if (!done && inter() != nullptr && turns_till_chopper() < 15 && seek_to_plant_banana(worker)) done = true;
            if (!done && my_inventory.lemon < L && gather_initial_fruit(worker, "LEMON", 10)) done = true;
            if (!done && my_inventory.plum < P && gather_initial_fruit(worker, "PLUM", 10)) done = true;
            if (!done && my_inventory.apple < A && gather_anywhere_fruit(worker, "APPLE", 10)) done = true;
            if (!done && my_inventory.lemon < L && gather_anywhere_fruit(worker, "LEMON", 10)) done = true;
            if (!done && my_inventory.plum < P && gather_anywhere_fruit(worker, "PLUM", 10)) done = true;
            if (!done && my_inventory.iron < I && gather_iron(worker)) done = true;
            if (!hasPlan(worker.id)) harvest_closest_harvestable(worker);
        }
        if (hasPlan(worker.id)) return;

        bool anyGrown = false; for (auto& t : trees) if (t.grown()) { anyGrown = true; break; }
        if (!anyGrown) { seek_to_self_plant(worker); if (hasPlan(worker.id)) return; }

        if (worker.full() && worker.carry_banana == 0) { go_and_drop(worker, closest_dropoff(worker.node())); return; }

        bool onTaken = false;
        for (int id : planOrder) if (plans[id].node == worker.node()) { onTaken = true; break; }
        if (onTaken) {
            if (worker.full() && worker.carry_banana > 0) {
                vector<string> empt;
                for (auto& n : grid.neighbors(worker.node())) if (treeAt(n) == nullptr) empt.push_back(n);
                int bi = argminBy(empt, [&](const string& n) { return sp(my_camp.node(), n); });
                if (bi >= 0) { go_and_plant(worker, empt[bi], "BANANA"); return; }
                vector<string> tnodes;
                for (auto& n : grid.neighbors(worker.node())) if (treeAt(n)) tnodes.push_back(n);
                int ti = argminBy(tnodes, [&](const string& n) { return treeAt(n)->turns_till_size(4); });
                if (ti >= 0) { go_and_chop(worker, tnodes[ti]); return; }
            }
            vector<string> nb;
            for (auto& n : grid.neighbors(worker.node())) {
                Tree* t = treeAt(n);
                if (t && t->type_is("BANANA") && t->turns_till_fruit() <= 1) nb.push_back(n);
            }
            int nbi = argminBy(nb, [&](const string& n) { return sp(seed_node(), n); });
            if (nbi >= 0) { go_and_harvest(worker, nb[nbi]); return; }

            vector<string> tnodes;
            for (auto& n : grid.neighbors(worker.node())) if (treeAt(n)) tnodes.push_back(n);
            int ti = argminBy(tnodes, [&](const string& n) { return treeAt(n)->turns_till_size(4); });
            if (ti >= 0) { go_and_chop(worker, tnodes[ti]); return; }

            vector<string> empt;
            for (auto& n : grid.neighbors(worker.node())) if (treeAt(n) == nullptr) empt.push_back(n);
            int ei = argminBy(empt, [&](const string& n) { return sp(my_camp.node(), n); });
            if (ei >= 0) { go_and_chop(worker, empt[ei]); return; }
        }

        seek_to_plant_banana(worker);
    }

    void organize_intermediate(Worker& worker) {
        bool onTaken = false;
        for (int id : planOrder) if (plans[id].node == worker.node()) { onTaken = true; break; }
        if (onTaken) {
            if (worker.full() && dropoff_set.count(worker.node())) {
                vector<string> alt;
                for (auto& d : dropoff_nodes()) if (!node_reserved_by_any_plan(d)) alt.push_back(d);
                int bi = argminBy(alt, [&](const string& d) { return spSize(worker.node(), d); });
                if (bi < 0) {
                    auto& ns = grid.neighbors(worker.node());
                    go(worker, ns.empty() ? worker.node() : ns.front());
                    return;
                }
                go_and_drop(worker, alt[bi]); return;
            }
            Tree* here = treeAt(worker.node());
            if (!worker.full() && here && here->fruit()) {
                harvest_closest_harvestable(worker, {worker.node()}); return;
            }
        }

        if (worker.carry_wood > 0 || (worker.full() && worker.carry_iron > 0)) {
            go_and_drop(worker, closest_dropoff(worker.node())); return;
        }

        if (chopper() == nullptr) { chop_wars(worker); if (hasPlan(worker.id)) return; }

        if ((int)my_workers().size() > (int)opp_workers_ptrs().size()) {
            vector<Tree*> wl;
            for (auto& t : trees) if (t.type_is("LEMON") && wet_set.count(t.node()) && (spSize(opp_camp.node(), t.node()) - 1) <= 3) wl.push_back(&t);
            int bi = argminBy(wl, [&](Tree* t) {
                return make_tuple(spSize(opp_camp.node(), t->node()) - 1, spSize(worker.node(), t->node()));
            });
            if (bi >= 0) { go_and_chop(worker, wl[bi]->node()); return; }
            vector<Tree*> dl;
            for (auto& t : trees) if (t.type_is("LEMON") && (spSize(opp_camp.node(), t.node()) - 1) <= 3) dl.push_back(&t);
            int dbi = argminBy(dl, [&](Tree* t) {
                return make_tuple(spSize(opp_camp.node(), t->node()) - 1, spSize(worker.node(), t->node()));
            });
            if (dbi >= 0) { go_and_chop(worker, dl[dbi]->node()); return; }
        }

        if (my_inventory.score() > opp_inventory.score() + 40 && (int)trees.size() < 6) {
            vector<Tree*> all; for (auto& t : trees) all.push_back(&t);
            int bi = argminBy(all, [&](Tree* t) { return sp(worker.node(), t->node()); });
            if (bi >= 0) seek_to_chop(worker, all[bi]->node());
        }
        if (hasPlan(worker.id)) return;

        if (no_way_to_scale_to_chopper()) {
            Worker* h = helper();
            string helper_chop;
            if (h) { Tree* ht = treeAt(h->node()); if (ht) helper_chop = ht->node(); }
            auto pick = [&](const string& type) -> Tree* {
                vector<Tree*> cand;
                for (auto& t : trees) if (t.type_is(type) && t.node() != helper_chop) cand.push_back(&t);
                int bi = argminBy(cand, [&](Tree* t) { return t->turns_till_chop(worker.move_speed, worker.chop_power, spSize(worker.node(), t->node())); });
                return bi >= 0 ? cand[bi] : nullptr;
            };
            if (Tree* b = pick("BANANA")) { seek_to_chop(worker, b->node()); return; }
            if (Tree* p = pick("PLUM")) { seek_to_chop(worker, p->node()); return; }
            if (Tree* l = pick("LEMON")) { seek_to_chop(worker, l->node()); return; }
            if (Tree* a = pick("APPLE")) { seek_to_chop(worker, a->node()); return; }
        }

        if (chopper() == nullptr && !training_is_chopper_train() && best_prediction.valid) {
            int aL = aimed_chopper_cost()["LEMON"], aP = aimed_chopper_cost()["PLUM"];
            int aA = aimed_chopper_cost()["APPLE"], aI = aimed_chopper_cost()["IRON"];
            auto noneT3C = [&](const string& type) {
                for (auto* t : trees_within_3_of_camp()) if (t->type_is(type)) return false;
                return true;
            };
            bool done = false;
            if (!done && my_inventory.lemon == 0 && noneT3C("LEMON") && gather_and_plant(worker, "LEMON")) done = true;
            if (!done && my_inventory.plum == 0 && noneT3C("PLUM") && gather_and_plant(worker, "PLUM")) done = true;
            if (!done && my_inventory.lemon < 2 && gather_initial_fruit(worker, "LEMON", 1)) done = true;
            if (!done && my_inventory.plum < 2 && gather_initial_fruit(worker, "PLUM", 1)) done = true;
            if (!done && my_inventory.lemon < 2 && gather_initial_fruit(worker, "LEMON", 2)) done = true;
            if (!done && my_inventory.plum < 2 && gather_initial_fruit(worker, "PLUM", 2)) done = true;
            if (!done && my_inventory.lemon < 6 && gather_initial_fruit(worker, "LEMON", 1)) done = true;
            if (!done && my_inventory.plum < 6 && gather_initial_fruit(worker, "PLUM", 1)) done = true;
            if (!done && my_inventory.lemon < 6 && gather_initial_fruit(worker, "LEMON", 2)) done = true;
            if (!done && my_inventory.plum < 6 && gather_initial_fruit(worker, "PLUM", 2)) done = true;
            if (!done && my_inventory.lemon < aL && gather_initial_fruit(worker, "LEMON", 1)) done = true;
            if (!done && my_inventory.plum < aP && gather_initial_fruit(worker, "PLUM", 1)) done = true;
            if (!done && my_inventory.lemon < aL && gather_anywhere_fruit(worker, "LEMON", 2)) done = true;
            if (!done && my_inventory.plum < aP && gather_anywhere_fruit(worker, "PLUM", 2)) done = true;
            if (!done && my_inventory.apple < aA && gather_anywhere_fruit(worker, "APPLE", 30)) done = true;
            if (!done && my_inventory.iron < aI && gather_iron(worker)) done = true;
            if (hasPlan(worker.id)) return;
        }

        if (worker.full()) { go_and_drop(worker, closest_dropoff(worker.node())); return; }

        {
            vector<Tree*> lt; for (auto& t : trees) if (t.type_is("LEMON")) lt.push_back(&t);
            int bi = argminBy(lt, [&](Tree* t) { return make_tuple(spSize(opp_camp.node(), t->node()), spSize(worker.node(), t->node())); });
            if (bi >= 0) { go_and_chop(worker, lt[bi]->node()); return; }
        }
        {
            vector<Tree*> bt; for (auto& t : trees) if (t.type_is("BANANA") && spSize(opp_camp.node(), t.node()) < 4) bt.push_back(&t);
            int bi = argminBy(bt, [&](Tree* t) { return make_tuple(spSize(opp_camp.node(), t->node()), spSize(worker.node(), t->node())); });
            if (bi >= 0) { go_and_chop(worker, bt[bi]->node()); return; }
        }
        {
            vector<Tree*> pt; for (auto& t : trees) if (t.type_is("PLUM")) pt.push_back(&t);
            int bi = argminBy(pt, [&](Tree* t) { return make_tuple(spSize(opp_camp.node(), t->node()), spSize(worker.node(), t->node())); });
            if (bi >= 0) { go_and_chop(worker, pt[bi]->node()); return; }
        }

        bool anyGrown = false; for (auto& t : trees) if (t.grown()) { anyGrown = true; break; }
        if (!anyGrown) { seek_to_self_plant(worker); if (hasPlan(worker.id)) return; }
    }

    void prefill_tree_paths() {
        for (auto& t : trees) sp(my_camp.node(), t.node());
    }

    void ensureCell(const string& node, int x, int y) {
        auto& c = cells[node];
        c.x = x; c.y = y;
    }

    void init_turn_variables(const string& input) {
        mw_turn = -1; t3c_turn = -1; ttolt_turn = -1;
        vector<string> lines = splitlines(input);
        int idx = 0;
        {
            vector<string> t = tokenize(lines[idx++]);
            my_inventory = Inventory{atoi(t[0].c_str()), atoi(t[1].c_str()), atoi(t[2].c_str()),
                                     atoi(t[3].c_str()), atoi(t[4].c_str()), atoi(t[5].c_str())};
        }
        {
            vector<string> t = tokenize(lines[idx++]);
            opp_inventory = Inventory{atoi(t[0].c_str()), atoi(t[1].c_str()), atoi(t[2].c_str()),
                                      atoi(t[3].c_str()), atoi(t[4].c_str()), atoi(t[5].c_str())};
        }
        cells.clear();
        trees.clear();
        int tc = atoi(lines[idx++].c_str());
        trees.reserve(tc + 1);
        for (int i = 0; i < tc; ++i) {
            vector<string> t = tokenize(lines[idx++]);
            Tree tr;
            tr.type = t[0];
            tr.x = atoi(t[1].c_str()); tr.y = atoi(t[2].c_str());
            tr.size = atoi(t[3].c_str()); tr.health = atoi(t[4].c_str());
            tr.fruits = atoi(t[5].c_str()); tr.cooldown = atoi(t[6].c_str());
            string node = mk(tr.x, tr.y);
            tr.period = tree_period(wet_set.count(node) > 0, tr.type);
            trees.push_back(tr);
            ensureCell(node, tr.x, tr.y);
            cells[node].tree = (int)trees.size() - 1;
        }
        helperIdx = interIdx = chopperIdx = -1;
        workers.clear();
        int wc = atoi(lines[idx++].c_str());
        workers.reserve(wc + 1);
        for (int i = 0; i < wc; ++i) {
            vector<string> t = tokenize(lines[idx++]);
            Worker w;
            w.id = atoi(t[0].c_str()); w.player = atoi(t[1].c_str());
            w.x = atoi(t[2].c_str()); w.y = atoi(t[3].c_str());
            w.move_speed = atoi(t[4].c_str()); w.carry_capacity = atoi(t[5].c_str());
            w.harvest_power = atoi(t[6].c_str()); w.chop_power = atoi(t[7].c_str());
            w.carry_plum = atoi(t[8].c_str()); w.carry_lemon = atoi(t[9].c_str());
            w.carry_apple = atoi(t[10].c_str()); w.carry_banana = atoi(t[11].c_str());
            w.carry_iron = atoi(t[12].c_str()); w.carry_wood = atoi(t[13].c_str());
            workers.push_back(w);
            string node = mk(w.x, w.y);
            ensureCell(node, w.x, w.y);
            if (w.my()) cells[node].worker = (int)workers.size() - 1;
            else cells[node].opp = (int)workers.size() - 1;
        }
        // roles: sort my workers by id
        vector<int> myIdx;
        for (int i = 0; i < (int)workers.size(); ++i) if (workers[i].my()) myIdx.push_back(i);
        sort(myIdx.begin(), myIdx.end(), [&](int a, int b) { return workers[a].id < workers[b].id; });
        if ((int)myIdx.size() == 3) {
            helperIdx = myIdx[0]; interIdx = myIdx[1]; chopperIdx = myIdx[2];
        } else {
            for (int i = 0; i < (int)myIdx.size(); ++i) {
                int wi = myIdx[i];
                if (i == 0) { helperIdx = wi; continue; }
                if (i == 1 && workers[wi].harvest_power == 0) chopperIdx = wi;
                else interIdx = wi;
            }
        }
        init_predictions();
    }

    string call(int t, const string& input) {
        turn = t;
        init_turn_variables(input);
        training = "";
        plans.clear();
        planOrder.clear();

        if (turn <= 1 && !use_shortscale()) {
            if (turns_till_chopper() > 65) {
                string pot = my_inventory.best_intermediate_worker((int)my_workers().size());
                if (!pot.empty()) training = "TRAIN " + pot;
            }
        }
        if (chopper() == nullptr && best_prediction.valid && my_inventory.can_afford(best_prediction.costs)) {
            training = "TRAIN " + best_prediction.name();
        }
        if (chopper()) organize_chopping(*chopper());
        organize_helper(*helper());
        if (inter()) organize_intermediate(*inter());

        vector<string> parts;
        if (!training.empty()) parts.push_back(training);
        for (int id : planOrder) parts.push_back(plans[id].command());
        string result = joinv(parts, "; ");

        prefill_tree_paths();
        return result.empty() ? "WAIT" : result;
    }

    void init_grid() {
        vector<string> lines = splitlines(field);
        int W = (int)lines[0].size(), H = (int)lines.size();
        grid = Grid(W, H, true);
        for (int y = 0; y < H; ++y) {
            const string& line = lines[y];
            for (int x = 0; x < (int)line.size(); ++x) {
                char ch = line[x];
                string node = mk(x, y);
                if (ch == '~' || ch == '#' || ch == '+') grid.remove_cell(node);
                if (ch == '.') { grass_nodes.push_back(node); grass_set.insert(node); }
                if (ch == '~') { water_nodes.push_back(node); water_set.insert(node); }
                if (ch == '+') { iron_nodes.push_back(node); iron_set.insert(node); }
                if (ch == '0') { my_camp = Camp{true, x, y}; }
                if (ch == '1') { opp_camp = Camp{false, x, y}; has_opp_camp = true; }
            }
        }
        distance_between_camps = max((int)grid.shortest_path(my_camp.node(), opp_camp.node(), "").size() - 2, 0);
        for (auto& nb : grid.n4(my_camp.node())) grid.remove_connection(nb, my_camp.node());
        if (has_opp_camp) for (auto& nb : grid.n4(opp_camp.node())) grid.remove_connection(nb, opp_camp.node());
    }

    // Warm caches in the original order so equal-length paths resolve identically.
    void init_warm() {
        dropoff_vec = grid.neighbors(my_camp.node());
        dropoff_set = unordered_set<string>(dropoff_vec.begin(), dropoff_vec.end());

        for (auto& g : grass_nodes) {
            bool isWet = false, isMine = false;
            for (auto& n : grid.n4(g)) { if (water_set.count(n)) isWet = true; if (iron_set.count(n)) isMine = true; }
            if (isWet) { wet_nodes.push_back(g); wet_set.insert(g); }
            if (isMine) { mining_nodes.push_back(g); mining_set.insert(g); }
        }
        wet_nodes_within_3_of_camp();
        nodes_within_3_of_opp_camp();
        seed_node();
        my_nodes();
        seed_node();
        for (auto& g : grass_nodes) sp(g, seed_node());
        nodes_within_3_of_camp_except_seed();
        for (auto& mn : mining_nodes)
            for (auto& n : grid.neighbors(my_camp.node())) sp(n, mn);
        {
            auto& v = nodes_within_3_of_camp();
            for (auto& a : v) for (auto& b : v) if (a != b) sp(a, b);
        }
        for (auto& n : grid.neighbors(my_camp.node()))
            for (auto& g : grass_nodes) sp(n, g);
        for (auto& g : grass_nodes) sp(opp_camp.node(), g);
    }
};

int main() {
    ios::sync_with_stdio(false);
    string firstLine;
    getline(cin, firstLine);
    vector<string> wh = Controller::tokenize(firstLine);
    int W = atoi(wh[0].c_str()), H = atoi(wh[1].c_str());
    (void)W;
    vector<string> fieldLines;
    for (int i = 0; i < H; ++i) { string l; getline(cin, l); fieldLines.push_back(l); }
    Controller controller(joinv(fieldLines, "\n"));

    int turn = 0;
    string line;
    while (true) {
        vector<string> block;
        // my inventory, opp inventory, tree count
        if (!getline(cin, line)) break;
        block.push_back(line);
        if (!getline(cin, line)) break;
        block.push_back(line);
        if (!getline(cin, line)) break;
        block.push_back(line);
        int tc = atoi(block.back().c_str());
        for (int i = 0; i < tc; ++i) { getline(cin, line); block.push_back(line); }
        getline(cin, line); block.push_back(line);  // worker count
        int wc = atoi(block.back().c_str());
        for (int i = 0; i < wc; ++i) { getline(cin, line); block.push_back(line); }

        ++turn;
        cout << controller.call(turn, joinv(block, "\n")) << "\n" << flush;
    }
    return 0;
}