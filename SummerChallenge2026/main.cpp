#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


#ifndef P_TURNS_EXP
#define P_TURNS_EXP 1.82
#endif
#ifndef P_FAST_BONUS
#define P_FAST_BONUS 1.38
#endif
#ifndef P_HYST
#define P_HYST 1.0
#endif
#ifndef P_HOR_BASE
#define P_HOR_BASE 18.4
#endif
#ifndef P_HOR_DIV
#define P_HOR_DIV 2.5
#endif
#ifndef P_RISK_W
#define P_RISK_W 0.35
#endif
#ifndef P_PAINT_W
#define P_PAINT_W 0.042
#endif
#ifndef P_ATK_AFF
#define P_ATK_AFF 14.0
#endif
#ifndef P_ATK_TRK
#define P_ATK_TRK 0.8
#endif
#ifndef P_ATK_DEST
#define P_ATK_DEST 1.0
#endif
#ifndef P_ATK_HEXP
#define P_ATK_HEXP 1.35
#endif
// Recursive self-simulation of the opponent (predictMiddleBotAction) costs a
// whole extra gameTurn and measured strength-neutral over 96 matches (t=0.23),
// while its cost pushed the turn close enough to the 50 ms limit that contention
// caused real timeouts: 22.5% of loaded matches forfeited with it on, 5% with it
// off. Left in place behind the switch in case a quieter machine changes that.
#ifndef P_PREDICT_FOE
#define P_PREDICT_FOE 0
#endif
#ifndef P_TREE_ITERS
// Root compaction and cached first-ply transitions make this small budget useful.
// A larger 48-iteration build measured slightly stronger but still produced
// timing forfeits under referee contention, so safety takes priority here.
#define P_TREE_ITERS 24
#endif
#ifndef P_ROOT_ACTIONS
// Keep the root portfolio small enough for every action to receive several
// paired samples under the fixed iteration budget.
#define P_ROOT_ACTIONS 8
#endif
#ifndef P_TREE_BRANCH
// With a small fixed simulation budget, deeper nodes should refine the best
// route under three disruption choices instead of opening nine mostly
// one-sample route/attack combinations.
#define P_TREE_BRANCH 9
#endif
#ifndef P_TREE_MS
// Search admission guard on the reserved decision clock (gameTurn starts at
// +5 ms). The later hard stop protects the 50 ms referee limit if a normally
// cheap turn becomes slow because of the board or judge scheduling.
#define P_TREE_MS 20.0
#endif
#ifndef P_TREE_STOP_MS
// Absolute emergency stop on that same clock. Since it includes the artificial
// five-millisecond reserve, 30 here means roughly 25 ms of real computation and
// leaves about 25 ms for a final safety check, formatting and judge IPC.
#define P_TREE_STOP_MS 30.0
#endif
#ifndef P_TREE_DEPTH
#define P_TREE_DEPTH 3
#endif
#ifndef P_TREE_EXPLORE
#define P_TREE_EXPLORE 0.55
#endif
#ifndef P_TREE_ENABLE
#define P_TREE_ENABLE 1
#endif


// ---- Tier B: greedy disruption scoring ----
#ifndef A_MINE_MUL
#define A_MINE_MUL 1.5
#endif
#ifndef A_THRESH
#define A_THRESH 0.05
#endif
#ifndef A_EXAMINE
#define A_EXAMINE 20
#endif
#ifndef A_H1
#define A_H1 25
#endif
#ifndef A_H1_FOE
#define A_H1_FOE 1.2
#endif
#ifndef A_H1_MINE
#define A_H1_MINE 1.8
#endif
#ifndef A_H1_BONUS
#define A_H1_BONUS 2.0
#endif
#ifndef A_H2
#define A_H2 18
#endif
#ifndef A_H2_FOE
#define A_H2_FOE 0.8
#endif
#ifndef A_H2_MINE
#define A_H2_MINE 1.5
#endif
#ifndef A_H2_STICK
#define A_H2_STICK 1.25
#endif
#ifndef A_H3
#define A_H3 10
#endif
#ifndef A_H3_FOE
#define A_H3_FOE 0.5
#endif
#ifndef A_H3_MINE
#define A_H3_MINE 1.5
#endif
#ifndef A_H3_DIV
#define A_H3_DIV 1.6
#endif
#ifndef A_H3_STICK
#define A_H3_STICK 1.15
#endif

// ---- Tier A: tree-search evaluation and policy ----
#ifndef S_RV_IMM_HOR
#define S_RV_IMM_HOR 60
#endif
#ifndef S_RV_HOR
#define S_RV_HOR 20
#endif
#ifndef S_RV_TEXP
#define S_RV_TEXP 1.65
#endif
#ifndef S_RV_RISK
#define S_RV_RISK 0.3
#endif
#ifndef S_RV_PRIOR
#define S_RV_PRIOR 0.03
#endif
#ifndef S_ATK_LOST
#define S_ATK_LOST 12.0
#endif
#ifndef S_ATK_OWN
#define S_ATK_OWN 1.5
#endif
#ifndef S_ATK_DEST
#define S_ATK_DEST 0.6
#endif
#ifndef S_EV_HOR
#define S_EV_HOR 14
#endif
#ifndef S_EV_VULN_PAD
#define S_EV_VULN_PAD 2
#endif
#ifndef S_EV_POT
#define S_EV_POT 0.8
#endif
#ifndef S_NORM_MIN
#define S_NORM_MIN 80.0
#endif
#ifndef S_NORM_RATE
#define S_NORM_RATE 10.0
#endif
#ifndef S_NORM_ADD
#define S_NORM_ADD 30.0
#endif
#ifndef S_ROOTATK_HOR
#define S_ROOTATK_HOR 18
#endif
#ifndef S_ROOTATK_FOE
#define S_ROOTATK_FOE 0.6
#endif
#ifndef S_ROOTR_TEXP
#define S_ROOTR_TEXP 1.82
#endif
#ifndef S_ROOTR_CUT
#define S_ROOTR_CUT 0.7
#endif
#ifndef S_ROOT_EXPLORE
#define S_ROOT_EXPLORE 0.12
#endif
#ifndef S_FIRST_PRIOR
#define S_FIRST_PRIOR 0.04
#endif
#ifndef S_PK_OWN
#define S_PK_OWN 500
#endif
#ifndef S_PK_FOE
#define S_PK_FOE 200
#endif
#ifndef S_PK_DEST
#define S_PK_DEST 250
#endif
#ifndef S_PK_TOWN
#define S_PK_TOWN 180
#endif

using namespace std;


struct Tile {
    int region = -1, terrain = 0;
    bool town = false;
};
struct Region {
    int id = -1, instability = 0;
    bool inked = false, hasTown = false;
    vector<int> cells;
};
struct Town {
    int id = -1, cell = -1;
    vector<int> requests;
};
struct ActivePath {
    vector<int> cells;
    int mine = 0, foe = 0;
};
struct Income {
    int mine = 0, foe = 0;
    vector<ActivePath> paths;
    int net() const { return mine - foe; }
};
struct Plan {
    vector<int> path;
    double value = 0;
    int paint = 0, turns = 0;
};
struct MacroAction {
    vector<int> placements;
    int attack = -1;
    double prior = 0;
    vector<int> continuation;
};

struct Game {
    int myId = 0, width = 0, height = 0, turn = 0;
    int myScore = 0, foeScore = 0, lastAttack = -1;
    vector<Tile> tiles;
    vector<Region> regions;
    vector<Town> towns;
    vector<vector<int>> neighbors;
    vector<int> owners, previousPath;
    chrono::steady_clock::time_point started;

    // Where each connection's scoring path will eventually settle. The referee
    // always routes a connection along the shortest *physical* path through the
    // built network, so once the board fills in, geometry decides the route, not
    // ownership. destiny[cell] counts how many requested connections are headed
    // through that cell: a cell on three of them pays three points a turn for the
    // same one-off paint, and a cell on none can never score at all.
    vector<int> destiny;
    vector<vector<int>> destinedPaths;
    vector<int> trunkWaypoints;

    mutable vector<int> bfsParent, bfsQueue;
    // searchIncome() is the hottest routine in the search; it was clearing a
    // fresh 600-byte flag array for every connection of every simulated turn.
    // A generation counter gives the same "seen this region yet" test for free.
    mutable vector<int> regionStamp;
    mutable int stampGeneration = 0;
    mutable vector<double> dijkstraDist;
    mutable vector<int> dijkstraParent;
    uint64_t rngState = 0x9e3779b97f4a7c15ULL;
    bool predictingMiddle = false, suppressOutput = false;
    int foeLastAttack = -1;
    vector<int> foePreviousPath;
    MacroAction predictedFoeAction, lastDecision;
    MacroAction previousFoePrediction, previousOwnAction;
    int middleEvidence = 0, middleMisses = 0;

    bool init() {
        if (!(cin >> myId >> width >> height) || width <= 0 || height <= 0)
            return false;
        tiles.resize(width * height);
        owners.resize(tiles.size(), -1);
        neighbors.resize(tiles.size());
        bfsParent.resize(tiles.size(), -2);
        regionStamp.assign(tiles.size(), 0);
        bfsQueue.resize(tiles.size(), 0);
        dijkstraDist.resize(tiles.size(), 0);
        dijkstraParent.resize(tiles.size(), -1);

        unordered_map<int, int> regionIndex, townIndex;
        for (int cell = 0; cell < (int)tiles.size(); ++cell) {
            int id, terrain;
            if (!(cin >> id >> terrain)) return false;
            if (!regionIndex.count(id)) {
                regionIndex[id] = (int)regions.size();
                Region region;
                region.id = id;
                regions.push_back(region);
            }
            int r = regionIndex.at(id);
            tiles[cell].region = r;
            tiles[cell].terrain = terrain;
            regions[r].cells.push_back(cell);
            int x = cell % width, y = cell / width;
            // N/E/S/W tie-breaking order strictly maintained
            if (y > 0) neighbors[cell].push_back(cell - width);
            if (x + 1 < width) neighbors[cell].push_back(cell + 1);
            if (y + 1 < height) neighbors[cell].push_back(cell + width);
            if (x > 0) neighbors[cell].push_back(cell - 1);
        }
        int count;
        if (!(cin >> count)) return false;
        towns.resize(count);
        for (int i = 0; i < count; ++i) {
            int x, y;
            string desired;
            if (!(cin >> towns[i].id >> x >> y >> desired)) return false;
            towns[i].cell = y * width + x;
            tiles[towns[i].cell].town = true;
            regions[tiles[towns[i].cell].region].hasTown = true;
            townIndex[towns[i].id] = i;
            if (desired != "x") {
                stringstream input(desired);
                string part;
                while (getline(input, part, ','))
                    towns[i].requests.push_back(stoi(part));
            }
        }
        for (Town &town : towns)
            for (int &target : town.requests) target = townIndex.at(target);
        return true;
    }

    bool parse() {
        if (!(cin >> myScore >> foeScore)) return false;
        for (int cell = 0; cell < (int)tiles.size(); ++cell) {
            int instability, inked;
            string activeConnections;
            if (!(cin >> owners[cell] >> instability >> inked >> activeConnections))
                return false;
            Region &region = regions[tiles[cell].region];
            region.instability = instability;
            region.inked = inked != 0;
        }
        if (!previousFoePrediction.placements.empty()) {
            vector<bool> weAlsoPlaced(tiles.size(), false);
            for (int cell : previousOwnAction.placements) weAlsoPlaced[cell] = true;
            for (int cell : previousFoePrediction.placements) {
                int expected = weAlsoPlaced[cell] ? 2 : 1 - myId;
                if (owners[cell] == expected) ++middleEvidence;
                else ++middleMisses;
            }
        }
        ++turn;
        return true;
    }

    bool blocked(int cell, int excluded = -1) const {
        int r = tiles[cell].region;
        return r == excluded || regions[r].inked;
    }

    double elapsedMs() const {
        return chrono::duration<double, milli>(chrono::steady_clock::now() - started).count();
    }

    bool anyConnectionPossible(int excluded) const {
        vector<int> component(tiles.size(), -1), queue(tiles.size());
        for (int start = 0; start < (int)tiles.size(); ++start) {
            if (component[start] != -1 || blocked(start, excluded)) continue;
            int head = 0, tail = 0;
            queue[tail++] = start;
            component[start] = start;
            while (head < tail) {
                for (int next : neighbors[queue[head++]]) {
                    if (component[next] != -1 || blocked(next, excluded)) continue;
                    component[next] = start;
                    queue[tail++] = next;
                }
            }
        }
        for (const Town &town : towns) {
            if (component[town.cell] == -1) continue;
            for (int target : town.requests)
                if (component[town.cell] == component[towns[target].cell]) return true;
        }
        return false;
    }

    Income income(const vector<int> &track, int excluded = -1, bool details = false) const {
        Income result;
        for (const Town &town : towns) {
            if (town.requests.empty() || blocked(town.cell, excluded)) continue;
            fill(bfsParent.begin(), bfsParent.end(), -2);
            int head = 0, tail = 0;
            bfsQueue[tail++] = town.cell;
            bfsParent[town.cell] = -1;
            while (head < tail) {
                int cell = bfsQueue[head++];
                for (int next : neighbors[cell]) {
                    if (bfsParent[next] != -2 || blocked(next, excluded)) continue;
                    if (!tiles[next].town && track[next] == -1) continue;
                    bfsParent[next] = cell;
                    bfsQueue[tail++] = next;
                }
            }
            for (int target : town.requests) {
                int cell = towns[target].cell;
                if (bfsParent[cell] == -2) continue;
                ActivePath path;
                for (; cell != -1; cell = bfsParent[cell]) {
                    if (details) path.cells.push_back(cell);
                    if (!tiles[cell].town) {
                        path.mine += track[cell] == myId;
                        path.foe += track[cell] == 1 - myId;
                    }
                }
                result.mine += path.mine;
                result.foe += path.foe;
                if (details) result.paths.push_back(std::move(path));
            }
        }
        return result;
    }

    // Recomputed each turn because ink permanently reshapes the geometry.
    void updateDestiny() {
        destiny.assign(tiles.size(), 0);
        destinedPaths.clear();
        for (const Town &town : towns) {
            if (town.requests.empty() || blocked(town.cell)) continue;
            // Same N/E/S/W FIFO walk the referee uses to break ties, but over every
            // open cell rather than only built ones, so it finds the route the
            // connection will land on once the gaps are filled by either side.
            fill(bfsParent.begin(), bfsParent.end(), -2);
            int head = 0, tail = 0;
            bfsQueue[tail++] = town.cell;
            bfsParent[town.cell] = -1;
            while (head < tail) {
                int cell = bfsQueue[head++];
                for (int next : neighbors[cell]) {
                    if (bfsParent[next] != -2 || blocked(next)) continue;
                    bfsParent[next] = cell;
                    bfsQueue[tail++] = next;
                }
            }
            for (int target : town.requests) {
                int cell = towns[target].cell;
                if (bfsParent[cell] == -2) continue;
                vector<int> path;
                for (; cell != -1; cell = bfsParent[cell]) path.push_back(cell);
                reverse(path.begin(), path.end());
                for (int c : path)
                    if (!tiles[c].town) ++destiny[c];
                destinedPaths.push_back(std::move(path));
            }
        }
        // Cells carrying several connections at once are the trunks worth routing
        // through; a plain point-to-point search never proposes them on its own.
        trunkWaypoints.clear();
        for (int cell = 0; cell < (int)tiles.size(); ++cell)
            if (destiny[cell] >= 2 && !tiles[cell].town) trunkWaypoints.push_back(cell);
        sort(trunkWaypoints.begin(), trunkWaypoints.end(),
             [&](int a, int b) { return destiny[a] > destiny[b]; });
        if ((int)trunkWaypoints.size() > 8) trunkWaypoints.resize(8);
    }

    vector<int> predictOpponentThreat() const {
        vector<int> threat(tiles.size(), 0);
        vector<double> dist(tiles.size(), 1e9);
        vector<int> parent(tiles.size(), -1);

        for (const Town &town : towns) {
            if (town.requests.empty() || blocked(town.cell)) continue;
            if (elapsedMs() > (predictingMiddle ? 29.0 : 13.0)) break;
            fill(dist.begin(), dist.end(), 1e9);
            fill(parent.begin(), parent.end(), -1);

            using State = pair<double, int>;
            priority_queue<State, vector<State>, greater<State>> pq;
            dist[town.cell] = 0;
            pq.emplace(0, town.cell);

            while (!pq.empty()) {
                auto [d, u] = pq.top();
                pq.pop();
                if (d > dist[u]) continue;

                for (int v : neighbors[u]) {
                    if (blocked(v)) continue;
                    double w;
                    if (tiles[v].town) w = 0.05;
                    else if (owners[v] == 1 - myId) w = 0.05;
                    else if (owners[v] == 2) w = 0.2;
                    else if (owners[v] == myId) w = 3.5;
                    else w = tiles[v].terrain + 1.0;

                    if (d + w + 1e-9 < dist[v]) {
                        dist[v] = d + w;
                        parent[v] = u;
                        pq.emplace(dist[v], v);
                    }
                }
            }

            for (int target : town.requests) {
                int finish = towns[target].cell;
                if (!isfinite(dist[finish]) || dist[finish] >= 1e8) continue;
                for (int curr = finish; curr != -1; curr = parent[curr]) {
                    if (!tiles[curr].town && owners[curr] == -1) {
                        int adjFoe = 0;
                        for (int nxt : neighbors[curr]) {
                            adjFoe += owners[nxt] == 1 - myId;
                        }
                        threat[curr] += (adjFoe > 0 ? 12 : 5);
                    }
                }
            }
        }
        return threat;
    }

    int chooseAttack(const vector<int> &track, const vector<bool> &protectedRegions) const {
        int remaining = 101 - turn;
        Income base = income(track, -1, true);
        vector<int> mine(regions.size()), foe(regions.size()), affected(regions.size());

        // Who profits from the routes this region is destined to carry. affected[]
        // below only sees connections that are already live, so a region the
        // opponent has quietly taken over on a busy future trunk looks worthless
        // until it starts paying them — by which point the track is already theirs.
        vector<int> destinyEdge(regions.size(), 0);
        for (int cell = 0; cell < (int)tiles.size(); ++cell) {
            if (blocked(cell) || tiles[cell].town) continue;
            mine[tiles[cell].region] += track[cell] == myId;
            foe[tiles[cell].region] += track[cell] == 1 - myId;
            if (destiny[cell] > 0) {
                if (track[cell] == 1 - myId) destinyEdge[tiles[cell].region] += destiny[cell];
                else if (track[cell] == myId) destinyEdge[tiles[cell].region] -= destiny[cell];
            }
        }
        for (const ActivePath &path : base.paths) {
            vector<bool> seen(regions.size(), false);
            for (int cell : path.cells) {
                int r = tiles[cell].region;
                if (!seen[r]) affected[r] += path.foe - path.mine;
                seen[r] = true;
            }
        }

        vector<pair<double, int>> candidates;
        for (int r = 0; r < (int)regions.size(); ++r) {
            const Region &region = regions[r];
            int hits = 4 - region.instability;
            if (region.hasTown || region.inked || hits <= 0 || hits > remaining || protectedRegions[r])
                continue;
            if (mine[r] == 0 && foe[r] == 0 && affected[r] == 0) continue;

            double estimate = (P_ATK_AFF * max(0, affected[r]) +
                               P_ATK_TRK * (foe[r] - A_MINE_MUL * mine[r]) +
                               P_ATK_DEST * destinyEdge[r]) / pow(hits, P_ATK_HEXP);
            candidates.emplace_back(estimate, r);
        }
        sort(candidates.rbegin(), candidates.rend());

        double bestValue = A_THRESH;
        int best = -1, examined = 0;
        for (auto [estimate, r] : candidates) {
            (void)estimate;
            if (++examined > A_EXAMINE || elapsedMs() > 38.0) break;
            int hits = 4 - regions[r].instability;
            Income after = income(track, r);
            int gain = after.net() - base.net();

            if (hits == 1 && !anyConnectionPossible(r)) {
                if (myScore + after.mine > foeScore + after.foe) {
                    return r; // Immediate winning kill
                }
                continue; // Do not cut connection if tied/losing
            }
            if (gain < 0) continue;

            double value = 0;
            if (hits == 1) {
                int horizon = min(remaining, A_H1);
                value = gain * horizon + A_H1_FOE * foe[r] - A_H1_MINE * mine[r] + P_ATK_DEST * destinyEdge[r];
                if (foe[r] > 0) value += A_H1_BONUS * foe[r];
            } else if (hits == 2) {
                int horizon = min(remaining - 1, A_H2);
                value = (gain * horizon + A_H2_FOE * foe[r] - A_H2_MINE * mine[r] + P_ATK_DEST * destinyEdge[r]) / 2.0;
                if (r == lastAttack) value *= A_H2_STICK;
            } else {
                int horizon = min(remaining - hits + 1, A_H3);
                value = (gain * horizon + A_H3_FOE * foe[r] - A_H3_MINE * mine[r] + P_ATK_DEST * destinyEdge[r]) / (hits * A_H3_DIV);
                if (r == lastAttack) value *= A_H3_STICK;
            }

            if (value > bestValue) {
                bestValue = value;
                best = r;
            }
        }
        return best;
    }

    double stepCost(int cell, int mode, const vector<int> &threat) const {
        const Tile &tile = tiles[cell];
        int instability = regions[tile.region].instability;
        double cost;
        int owner = owners[cell];
        bool hasTown = regions[tile.region].hasTown;

        if (mode == 2) {
            // Mode 2: Shortcut / Hijack
            cost = 1.0;
            if (!tile.town) {
                if (owner == -1) cost += 0.15 * (tile.terrain + 1);
                else if (owner == 1 - myId) cost += 0.8;
            }
            cost += 0.2 * instability;
        } else if (mode == 4) {
            // Mode 4: Trunk seeking. Steer onto the geometry several connections
            // are all headed for, so one stretch of track collects from each of
            // them at once instead of serving a single pair.
            if (tile.town) cost = 0.05;
            else if (owner == myId) cost = 0.05;
            else if (owner == 2) cost = 0.2;
            else if (owner == 1 - myId) cost = 2.5;
            else cost = tile.terrain + 1.0;
            cost /= 1.0 + 1.2 * destiny[cell];
            cost += 0.3 * instability * instability;
        } else if (mode == 3) {
            // Mode 3: Rapid Connection (fewest missing cells)
            if (tile.town) cost = 0.02;
            else if (owner == myId) cost = 0.05;
            else if (owner == 2) cost = 0.15;
            else if (owner == 1 - myId) cost = 4.0;
            else cost = 6.0 + (tile.terrain + 1) * 1.5;
            cost += 0.3 * instability * instability;
        } else if (mode == 1) {
            // Mode 1: Safe & Pure Ownership (avoid foe & unstable regions, prefer town zones)
            if (tile.town) cost = 0.05;
            else if (owner == myId) cost = 0.06;
            else if (owner == 2) cost = 0.25;
            else if (owner == 1 - myId) cost = 4.0;
            else {
                cost = tile.terrain + 1.0;
                if (threat[cell] > 0 && tile.terrain == 2) cost += 2.0;
            }
            cost += 1.0 * instability * instability;
            if (hasTown && !tile.town) cost *= 0.85;
        } else {
            // Mode 0: Balanced & Clash-Aware
            if (tile.town) cost = 0.05;
            else if (owner == myId) cost = 0.08;
            else if (owner == 2) cost = 0.18;
            else if (owner == 1 - myId) cost = 1.8;
            else {
                cost = tile.terrain + 1.0;
                if (threat[cell] > 0 && tile.terrain == 0) cost *= 0.75;
                if (threat[cell] > 0 && tile.terrain == 2) cost += 1.5;
            }
            cost += 0.4 * instability * instability;
        }

        if (owner == -1 && !tile.town)
            cost += ((cell * 37 + myId * 71) % 101) * 0.0001;

        return cost;
    }

    void consider(const vector<int> &path, int excluded, const Income &base, Plan &best, vector<Plan> &altPlans) const {
        if (path.empty()) return;
        vector<int> projected = owners;
        int pieces[4] = {}, paint = 0, risk = 0;
        bool allImmune = true;
        for (int cell : path) {
            if (blocked(cell, excluded)) return;
            risk = max(risk, regions[tiles[cell].region].instability);
            if (!regions[tiles[cell].region].hasTown) allImmune = false;
            if (!tiles[cell].town && projected[cell] == -1) {
                int cost = tiles[cell].terrain + 1;
                ++pieces[cost];
                paint += cost;
                projected[cell] = myId;
            }
        }
        if (paint == 0) return;
        int turns = pieces[3] + pieces[2] + (max(0, pieces[1] - pieces[2]) + 2) / 3;
        int remaining = 101 - turn;
        if (turns > remaining) return;
        Income after = income(projected, excluded);
        int gain = after.net() - base.net();
        if (gain <= 0) return;

        double horizonBase = predictingMiddle ? 16.0 : P_HOR_BASE;
        double riskWeight = predictingMiddle ? 0.5 : P_RISK_W;
        double turnsExponent = predictingMiddle ? 1.30 : P_TURNS_EXP;
        double paintWeight = predictingMiddle ? 0.03 : P_PAINT_W;
        double fastBonus = predictingMiddle ? 1.20 : P_FAST_BONUS;
        double hysteresis = predictingMiddle ? 1.08 : P_HYST;
        double maxHorizon = allImmune ? (remaining - turns + 1) :
                            min((double)(remaining - turns + 1),
                                horizonBase + (100 - turn) / P_HOR_DIV);
        double lifetime = maxHorizon / (1.0 + riskWeight * risk * risk);

        double value = gain * lifetime / pow(turns + 0.5, turnsExponent) - paintWeight * paint;
        if (turns == 1) value *= fastBonus;
        if (path == previousPath) value *= hysteresis;

        Plan plan = {path, value, paint, turns};
        if (value > best.value) best = plan;

        if (!predictingMiddle)
            for (const Plan &alternative : altPlans)
                if (alternative.path == path) return;
        if (altPlans.size() < (predictingMiddle ? 4u : 12u)) {
            altPlans.push_back(plan);
        } else {
            auto minIt = min_element(altPlans.begin(), altPlans.end(),
                                     [](const Plan &a, const Plan &b) { return a.value < b.value; });
            if (value > minIt->value) *minIt = plan;
        }
    }

    pair<Plan, vector<Plan>> choosePlan(int excluded, const vector<int> &threat) const {
        Plan best;
        vector<Plan> altPlans;
        Income base = income(owners, excluded);
        consider(previousPath, excluded, base, best, altPlans);

        // The route each connection is physically destined to settle on. Track laid
        // here can never be undercut by a shorter route later, unlike a cheap detour
        // that stops earning the moment anyone completes the direct line.
        for (const vector<int> &destined : destinedPaths) {
            if (elapsedMs() > (predictingMiddle ? 30.0 : 21.0)) break;
            consider(destined, excluded, base, best, altPlans);
        }

        const double inf = numeric_limits<double>::infinity();
        for (int mode = 0; mode < 5; ++mode) {
            for (const Town &town : towns) {
                if (town.requests.empty() || blocked(town.cell, excluded)) continue;
                if (elapsedMs() > 29.0) return {best, altPlans};

                fill(dijkstraDist.begin(), dijkstraDist.end(), inf);
                fill(dijkstraParent.begin(), dijkstraParent.end(), -1);

                using State = pair<double, int>;
                priority_queue<State, vector<State>, greater<State>> pending;
                dijkstraDist[town.cell] = 0;
                pending.emplace(0, town.cell);

                while (!pending.empty()) {
                    auto [cost, cell] = pending.top();
                    pending.pop();
                    if (cost > dijkstraDist[cell]) continue;

                    for (int next : neighbors[cell]) {
                        if (blocked(next, excluded)) continue;
                        double nextCost = cost + stepCost(next, mode, threat);
                        if (nextCost + 1e-9 < dijkstraDist[next]) {
                            dijkstraDist[next] = nextCost;
                            dijkstraParent[next] = cell;
                            pending.emplace(nextCost, next);
                        }
                    }
                }

                for (int target : town.requests) {
                    int finish = towns[target].cell;
                    if (!isfinite(dijkstraDist[finish])) continue;
                    vector<int> path;
                    for (int cell = finish; cell != -1; cell = dijkstraParent[cell])
                        path.push_back(cell);
                    reverse(path.begin(), path.end());
                    consider(path, excluded, base, best, altPlans);
                    if (elapsedMs() > 29.0) return {best, altPlans};
                }
            }
        }

        // Force routes through the busiest trunk cells. A point-to-point search
        // optimises each pair in isolation and so will never propose the detour
        // that makes one stretch of track serve several connections; consider()
        // then throws the detour out again if it does not actually pay.
        for (int waypoint : trunkWaypoints) {
            if (blocked(waypoint, excluded) || elapsedMs() > 33.0) break;
            fill(dijkstraDist.begin(), dijkstraDist.end(), inf);
            fill(dijkstraParent.begin(), dijkstraParent.end(), -2);
            using State = pair<double, int>;
            priority_queue<State, vector<State>, greater<State>> pending;
            dijkstraDist[waypoint] = 0;
            dijkstraParent[waypoint] = -1;
            pending.emplace(0, waypoint);
            while (!pending.empty()) {
                auto [cost, cell] = pending.top();
                pending.pop();
                if (cost > dijkstraDist[cell]) continue;
                for (int next : neighbors[cell]) {
                    if (blocked(next, excluded)) continue;
                    double nextCost = cost + stepCost(next, 0, threat);
                    if (nextCost + 1e-9 < dijkstraDist[next]) {
                        dijkstraDist[next] = nextCost;
                        dijkstraParent[next] = cell;
                        pending.emplace(nextCost, next);
                    }
                }
            }
            for (const Town &town : towns) {
                if (town.requests.empty() || dijkstraParent[town.cell] == -2) continue;
                for (int target : town.requests) {
                    int finish = towns[target].cell;
                    if (dijkstraParent[finish] == -2) continue;
                    vector<int> path, tail;
                    vector<bool> used(tiles.size(), false);
                    for (int cell = town.cell; cell != -1; cell = dijkstraParent[cell]) {
                        if (!used[cell]) path.push_back(cell);
                        used[cell] = true;
                    }
                    for (int cell = finish; cell != -1; cell = dijkstraParent[cell]) tail.push_back(cell);
                    reverse(tail.begin(), tail.end());
                    for (int cell : tail) {
                        if (!used[cell]) path.push_back(cell);
                        used[cell] = true;
                    }
                    consider(path, excluded, base, best, altPlans);
                    if (elapsedMs() > 33.0) return {best, altPlans};
                }
            }
        }
        return {best, altPlans};
    }

    uint64_t random64() {
        rngState ^= rngState << 7;
        rngState ^= rngState >> 9;
        return rngState;
    }

    vector<int> packPath(const vector<int> &path, int player, const vector<int> &threat) const {
        vector<int> byCost[4];
        vector<bool> seen(tiles.size(), false);
        for (int cell : path) {
            if (cell < 0 || cell >= (int)tiles.size() || seen[cell]) continue;
            seen[cell] = true;
            if (!tiles[cell].town && owners[cell] == -1 && !blocked(cell))
                byCost[tiles[cell].terrain + 1].push_back(cell);
        }
        auto urgencyFor = [&](int cell) {
            int value = 30 * destiny[cell];
            for (int next : neighbors[cell]) {
                if (tiles[next].town || owners[next] == player) value += 80;
                if (owners[next] == 1 - player) value += 25;
            }
            if (player == myId) value += threat[cell] * 4;
            return value;
        };
        for (int cost = 1; cost <= 3; ++cost)
            stable_sort(byCost[cost].begin(), byCost[cost].end(),
                        [&](int a, int b) { return urgencyFor(a) > urgencyFor(b); });

        vector<int> result;
        if (!byCost[3].empty()) return {byCost[3][0]};
        if (!byCost[2].empty()) {
            result.push_back(byCost[2][0]);
            if (!byCost[1].empty()) result.push_back(byCost[1][0]);
            return result;
        }
        for (int i = 0; i < min(3, (int)byCost[1].size()); ++i)
            result.push_back(byCost[1][i]);
        return result;
    }

    int approximateAttack(int player, const vector<int> &placements) const {
        vector<bool> protectedRegion(regions.size(), false);
        for (int cell : placements) protectedRegion[tiles[cell].region] = true;
        double bestValue = 0.2;
        int best = -1;
        for (int r = 0; r < (int)regions.size(); ++r) {
            const Region &region = regions[r];
            int hits = 4 - region.instability;
            if (region.hasTown || region.inked || hits <= 0 || protectedRegion[r]) continue;
            double friendly = 0, enemy = 0;
            for (int cell : region.cells) {
                if (tiles[cell].town) continue;
                double weight = 1.0 + 0.7 * destiny[cell];
                if (owners[cell] == player) friendly += weight;
                else if (owners[cell] == 1 - player) enemy += weight;
            }
            double value = (enemy - 1.35 * friendly) / pow((double)hits, 1.25);
            if (value > bestValue) {
                bestValue = value;
                best = r;
            }
        }
        return best;
    }

    vector<MacroAction> opponentActions(const vector<int> &threat) const {
        struct RouteOption {
            double value;
            vector<int> path;
        };
        vector<RouteOption> routes;
        const int player = 1 - myId;
        const double inf = numeric_limits<double>::infinity();
        vector<double> dist(tiles.size());
        vector<int> parent(tiles.size());

        auto perspectiveCost = [&](int cell, int mode) {
            if (tiles[cell].town) return 0.03;
            int owner = owners[cell];
            double cost;
            if (mode == 2) {
                cost = 1.0 + (owner == -1 ? 0.18 * (tiles[cell].terrain + 1) : 0.0);
                if (owner == myId) cost += 0.8;
            } else if (mode == 1) {
                if (owner == player) cost = 0.05;
                else if (owner == 2) cost = 0.2;
                else if (owner == myId) cost = 4.0;
                else cost = tiles[cell].terrain + 1.0;
                cost += 0.8 * regions[tiles[cell].region].instability;
            } else if (mode == 3) {
                if (owner == player) cost = 0.05;
                else if (owner == 2) cost = 0.15;
                else if (owner == myId) cost = 4.0;
                else cost = 6.0 + 1.5 * (tiles[cell].terrain + 1);
            } else {
                if (owner == player) cost = 0.07;
                else if (owner == 2) cost = 0.18;
                else if (owner == myId) cost = 1.8;
                else cost = tiles[cell].terrain + 1.0;
                if (mode == 4) cost /= 1.0 + 1.2 * destiny[cell];
                cost += 0.35 * regions[tiles[cell].region].instability;
            }
            return cost;
        };

        int routeSearches = 0;
        constexpr int maxRouteSearches = 24;
        for (int mode = 0; mode < 5; ++mode) {
            for (const Town &town : towns) {
                if (town.requests.empty() || blocked(town.cell)) continue;
                if (elapsedMs() > P_TREE_STOP_MS - 5.0) break;
                if (routeSearches++ >= maxRouteSearches) break;
                fill(dist.begin(), dist.end(), inf);
                fill(parent.begin(), parent.end(), -1);
                using State = pair<double, int>;
                priority_queue<State, vector<State>, greater<State>> pq;
                dist[town.cell] = 0;
                pq.emplace(0, town.cell);
                while (!pq.empty()) {
                    auto [d, u] = pq.top();
                    pq.pop();
                    if (d > dist[u]) continue;
                    for (int v : neighbors[u]) {
                        if (blocked(v)) continue;
                        double nd = d + perspectiveCost(v, mode);
                        if (nd + 1e-9 < dist[v]) {
                            dist[v] = nd;
                            parent[v] = u;
                            pq.emplace(nd, v);
                        }
                    }
                }
                for (int target : town.requests) {
                    int finish = towns[target].cell;
                    if (!isfinite(dist[finish])) continue;
                    vector<int> path;
                    int missingPaint = 0, multiplicity = 0;
                    for (int cell = finish; cell != -1; cell = parent[cell]) {
                        path.push_back(cell);
                        if (!tiles[cell].town && owners[cell] == -1)
                            missingPaint += tiles[cell].terrain + 1;
                        multiplicity += destiny[cell];
                    }
                    reverse(path.begin(), path.end());
                    double value = multiplicity - 1.4 * missingPaint - 0.1 * dist[finish];
                    routes.push_back({value, std::move(path)});
                }
            }
        }
        for (const vector<int> &path : destinedPaths) {
            int missingPaint = 0, multiplicity = 0;
            for (int cell : path) {
                if (!tiles[cell].town && owners[cell] == -1)
                    missingPaint += tiles[cell].terrain + 1;
                multiplicity += destiny[cell];
            }
            routes.push_back({multiplicity - 1.4 * missingPaint, path});
        }
        sort(routes.begin(), routes.end(),
             [](const RouteOption &a, const RouteOption &b) { return a.value > b.value; });

        vector<MacroAction> result;
        unordered_map<string, bool> used;
        auto add = [&](vector<int> placement, int attack, double prior, vector<int> continuation = {}) {
            sort(placement.begin(), placement.end());
            string key = to_string(attack) + ":";
            for (int cell : placement) key += to_string(cell) + ",";
            if (!used.emplace(key, true).second) return;
            result.push_back({std::move(placement), attack, prior, std::move(continuation)});
        };
        if (!predictedFoeAction.placements.empty() || predictedFoeAction.attack != -1)
            add(predictedFoeAction.placements, predictedFoeAction.attack, 100.0,
                predictedFoeAction.continuation);
        add({}, approximateAttack(player, {}), 0.0);
        int checked = 0;
        for (const RouteOption &route : routes) {
            if (++checked > 80 || result.size() >= 6) break;
            vector<int> placement = packPath(route.path, player, threat);
            if (placement.empty()) continue;
            int attack = approximateAttack(player, placement);
            add(std::move(placement), attack, route.value, route.path);
        }
        add({}, -1, -1.0);
        return result;
    }


    void predictMiddleBotAction() {
        predictedFoeAction = {};
        if (predictingMiddle || elapsedMs() > 20.0) return;
        int requestCount = 0;
        for (const Town &town : towns) requestCount += town.requests.size();
        if (requestCount > 48) return;
        int savedId = myId, savedMyScore = myScore, savedFoeScore = foeScore;
        int savedLastAttack = lastAttack;
        vector<int> savedPreviousPath = previousPath;
        auto outerStarted = started;

        myId = 1 - myId;
        swap(myScore, foeScore);
        lastAttack = foeLastAttack;
        previousPath = foePreviousPath;
        predictingMiddle = true;
        suppressOutput = true;
        gameTurn();
        predictedFoeAction = lastDecision;
        foeLastAttack = lastAttack;
        foePreviousPath = previousPath;

        myId = savedId;
        myScore = savedMyScore;
        foeScore = savedFoeScore;
        lastAttack = savedLastAttack;
        previousPath = std::move(savedPreviousPath);
        predictingMiddle = false;
        suppressOutput = false;
        started = outerStarted;
    }



    // Each player's tree stores that player's own action sequence. A node has
    // no fixed board: the other tree can produce a different reply on each visit.
    struct SearchState {
        array<int8_t, 600> track{};
        array<uint8_t, 600> instability{};
        array<int, 600> routeBalance{};
        int score = 0, ply = 0, rate = 0;
        array<int, 5> vulnerableIncome{};
        int safeIncome = 0;
        bool terminal = false;
    };
    struct SearchMove {
        array<int, 3> cells{{-1, -1, -1}};
        int count = 0, attack = -1, route = -1;
        double prior = 0;
    };
    struct SearchRoute {
        vector<int> cells;
        double prior = 0;
        double incomeScale = 1.0;
    };
    struct SearchEdge {
        int visits = 0, child = -1;
        double total = 0;
    };
    struct SearchNode {
        int visits = 0;
        array<SearchEdge, 18> edges{};
    };
    struct SearchContext {
        array<vector<SearchRoute>, 2> routes;
        array<vector<SearchMove>, 2> rootMoves;
        array<vector<SearchNode>, 2> trees;
        double normalization = 20;
    };

    bool simBlocked(const SearchState &s, int cell) const {
        int r = tiles[cell].region;
        return s.instability[r] >= 4 && !regions[r].hasTown;
    }

    SearchState searchState() const {
        SearchState s;
        s.score = myScore - foeScore;
        for (int c = 0; c < (int)tiles.size(); ++c) s.track[c] = owners[c];
        for (int r = 0; r < (int)regions.size(); ++r)
            s.instability[r] = regions[r].inked ? 4 : regions[r].instability;
        return s;
    }

    int searchIncome(SearchState &s) const {
        array<int, 600> balance;
        s.routeBalance.fill(0);
        s.vulnerableIncome.fill(0);
        s.safeIncome = 0;
        int total = 0;
        for (const Town &town : towns) {
            if (town.requests.empty() || simBlocked(s, town.cell)) continue;
            fill(bfsParent.begin(), bfsParent.end(), -2);
            int head = 0, tail = 0;
            bfsQueue[tail++] = town.cell;
            bfsParent[town.cell] = -1;
            balance[town.cell] = 0;
            while (head < tail) {
                int c = bfsQueue[head++];
                for (int n : neighbors[c]) {
                    if (bfsParent[n] != -2 || simBlocked(s, n)) continue;
                    if (!tiles[n].town && s.track[n] == -1) continue;
                    bfsParent[n] = c;
                    balance[n] = balance[c] + (tiles[n].town ? 0 :
                        (s.track[n] == myId) - (s.track[n] == 1 - myId));
                    bfsQueue[tail++] = n;
                }
            }
            for (int target : town.requests) {
                int c = towns[target].cell;
                if (bfsParent[c] != -2) {
                    int net = balance[c];
                    total += net;
                    ++stampGeneration;
                    int lifetime = 5;
                    for (; c != -1; c = bfsParent[c]) {
                        int r = tiles[c].region;
                        if (regionStamp[r] != stampGeneration) {
                            regionStamp[r] = stampGeneration;
                            s.routeBalance[r] += net;
                        }
                        if (!regions[r].hasTown) lifetime = min(lifetime, 4 - (int)s.instability[r]);
                    }
                    if (lifetime == 5) s.safeIncome += net;
                    else s.vulnerableIncome[lifetime] += net;
                }
            }
        }
        return total;
    }

    bool searchConnectionPossible(const SearchState &s) const {
        array<int, 600> component;
        component.fill(-1);
        for (const Town &town : towns) {
            int start = town.cell;
            if (component[start] != -1 || simBlocked(s, start)) continue;
            int head = 0, tail = 0;
            bfsQueue[tail++] = start;
            component[start] = start;
            while (head < tail) {
                int c = bfsQueue[head++];
                for (int n : neighbors[c]) {
                    if (component[n] != -1 || simBlocked(s, n)) continue;
                    component[n] = start;
                    bfsQueue[tail++] = n;
                }
            }
        }
        for (const Town &town : towns)
            for (int target : town.requests)
                if (component[town.cell] != -1 &&
                    component[town.cell] == component[towns[target].cell]) return true;
        return false;
    }

    void searchStep(SearchState &s, const SearchMove &a, const SearchMove &b) const {
        if (s.terminal) return;
        // Validate both bundles against the same pre-turn board, then commit.
        bool valid[2][3] = {};
        const SearchMove *moves[2] = {&a, &b};
        for (int p = 0; p < 2; ++p) {
            int budget = 3;
            for (int k = 0; k < moves[p]->count; ++k) {
                int c = moves[p]->cells[k];
                if (c < 0 || c >= (int)tiles.size() || tiles[c].town ||
                    simBlocked(s, c) || s.track[c] != -1) continue;
                bool duplicate = false;
                for (int j = 0; j < k; ++j)
                    duplicate |= valid[p][j] && moves[p]->cells[j] == c;
                int cost = tiles[c].terrain + 1;
                if (!duplicate && cost <= budget) {
                    valid[p][k] = true;
                    budget -= cost;
                }
            }
        }
        for (int p = 0; p < 2; ++p) for (int k = 0; k < moves[p]->count; ++k) {
            if (!valid[p][k]) continue;
            int c = moves[p]->cells[k];
            s.track[c] = s.track[c] == -1 ? (p == 0 ? myId : 1 - myId) : 2;
        }
        int attacks[2] = {a.attack, b.attack};
        bool inked = false;
        for (int p = 0; p < 2; ++p) {
            int r = attacks[p];
            if (r >= 0 && r < (int)regions.size() && !regions[r].hasTown &&
                s.instability[r] < 4) {
                ++s.instability[r];
                if (s.instability[r] == 4) {
                    inked = true;
                    for (int c : regions[r].cells) s.track[c] = -1;
                }
            }
        }
        s.rate = searchIncome(s);
        s.score += s.rate;
        ++s.ply;
        s.terminal = turn + s.ply > 100 || (inked && !searchConnectionPossible(s));
    }

    SearchMove searchPack(const SearchState &s, const SearchRoute &route,
                          int player, int ordering = 0) const {
        SearchMove result;
        vector<pair<int, int>> candidates[4];
        array<bool, 600> seen{};
        for (int c : route.cells) {
            if (seen[c] || tiles[c].town || s.track[c] != -1 || simBlocked(s, c)) continue;
            seen[c] = true;
            int urgency = ((c * 37 + player * 71) % 101);
            for (int n : neighbors[c]) {
                if (tiles[n].town || s.track[n] == player) urgency += S_PK_OWN;
                if (s.track[n] == 1 - player) urgency += S_PK_FOE;
            }
            if (ordering == 1) urgency = S_PK_DEST * destiny[c] +
                S_PK_TOWN * regions[tiles[c].region].hasTown + urgency / 8;
            if (ordering == 2) urgency = -urgency;
            candidates[tiles[c].terrain + 1].push_back({-urgency, c});
        }
        for (int cost = 1; cost <= 3; ++cost) sort(candidates[cost].begin(), candidates[cost].end());
        int budget = 3;
        for (int cost = 3; cost >= 1; --cost) {
            for (auto [unused, c] : candidates[cost]) {
                (void)unused;
                if (budget < cost) break;
                result.cells[result.count++] = c;
                budget -= cost;
            }
        }
        return result;
    }

    double searchRouteValue(const SearchState &s, const SearchRoute &route, int player) const {
        int pieces[4] = {}, owned = 0, enemy = 0, missing = 0, risk = 0;
        bool immune = true;
        for (int c : route.cells) {
            if (simBlocked(s, c)) return -1e6;
            int r = tiles[c].region;
            risk = max(risk, (int)s.instability[r]);
            immune &= regions[r].hasTown;
            if (tiles[c].town) continue;
            if (s.track[c] == player) ++owned;
            else if (s.track[c] == 1 - player) ++enemy;
            else if (s.track[c] == -1) {
                ++missing;
                ++pieces[tiles[c].terrain + 1];
            }
        }
        if (!missing) return -1e6;
        int turns = pieces[3] + pieces[2] + (max(0, pieces[1] - pieces[2]) + 2) / 3;
        int remaining = 101 - turn - s.ply;
        if (turns > remaining) return -1e6;
        double net = owned + missing - enemy;
        double horizon = min(immune ? S_RV_IMM_HOR : S_RV_HOR, remaining - turns + 1);
        return net * route.incomeScale * horizon / (pow(turns + 0.5, S_RV_TEXP) * (1 + S_RV_RISK * risk * risk))
             + S_RV_PRIOR * route.prior;
    }

    int searchAttack(const SearchState &s, const SearchMove &move, int player, int rank,
                     const SearchContext &ctx) const {
        if (rank == 2) return -1;
        array<bool, 600> protect{};
        for (int k = 0; k < move.count; ++k) protect[tiles[move.cells[k]].region] = true;
        if (move.route >= 0) {
            const auto &route = ctx.routes[player == myId ? 0 : 1][move.route];
            for (int c : route.cells) protect[tiles[c].region] = true;
        }
        vector<pair<double, int>> candidates;
        for (int r = 0; r < (int)regions.size(); ++r) {
            int hits = 4 - s.instability[r];
            if (hits <= 0 || regions[r].hasTown || protect[r] || hits > 101 - turn - s.ply) continue;
            double own = 0, foe = 0;
            for (int c : regions[r].cells) {
                double w = 1 + S_ATK_DEST * destiny[c];
                if (s.track[c] == player) own += w;
                if (s.track[c] == 1 - player) foe += w;
            }
            double lostNet = s.routeBalance[r] * (player == myId ? -1.0 : 1.0);
            double value = (S_ATK_LOST * lostNet + foe - S_ATK_OWN * own) / (hits * hits);
            if (value > 0) candidates.emplace_back(value, r);
        }
        if (candidates.empty()) return -1;
        sort(candidates.rbegin(), candidates.rend());
        return candidates[min(rank, (int)candidates.size() - 1)].second;
    }

    SearchMove searchPolicy(const SearchState &s, int player, int option,
                            const SearchContext &ctx) const {
        const auto &routes = ctx.routes[player == myId ? 0 : 1];
        vector<pair<double, int>> ranked;
        for (int i = 0; i < (int)routes.size(); ++i) {
            double v = searchRouteValue(s, routes[i], player);
            if (v > 0) ranked.emplace_back(v, i);
        }
        sort(ranked.rbegin(), ranked.rend());
        SearchMove move;
        int routeRank = option / 3;
        if (!ranked.empty()) {
            int r = ranked[min(routeRank, (int)ranked.size() - 1)].second;
            move = searchPack(s, routes[r], player);
            move.route = r;
        }
        move.attack = searchAttack(s, move, player, option % 3, ctx);
        return move;
    }

    double searchEvaluation(const SearchState &s, const SearchContext &ctx) const {
        if (s.terminal) return s.score > 0 ? 1.0 : (s.score < 0 ? -1.0 : 0.0);
        double value = s.score - (myScore - foeScore);
        int horizon = min(S_EV_HOR, max(0, 101 - turn - s.ply));
        // Town-only connections cannot be destroyed. Other routes may survive
        // only until the next disruption cycle; don't capitalize them forever.
        value += s.safeIncome * horizon;
        for (int hits = 1; hits <= 4; ++hits)
            value += s.vulnerableIncome[hits] * min(horizon, hits + S_EV_VULN_PAD);
        double potential[2] = {};
        for (int p = 0; p < 2; ++p) {
            for (const SearchRoute &route : ctx.routes[p])
                potential[p] = max(potential[p], searchRouteValue(s, route, p ? 1 - myId : myId));
        }
        value += S_EV_POT * (potential[0] - potential[1]);
        return value / (ctx.normalization + abs(value));
    }

    MacroAction treeSmitsimax(const MacroAction &base, const Plan &plan,
                              const vector<Plan> &alternatives, const vector<int> &threat) {
        if (!P_TREE_ENABLE) return base;
        if (tiles.size() > 600 || regions.size() > 600 || elapsedMs() > P_TREE_MS) return base;
        if (none_of(owners.begin(), owners.end(), [&](int owner) { return owner == 1 - myId; }))
            return base;
        SearchContext ctx;
        auto addRoute = [&](int p, const vector<int> &path, double prior) {
            if (path.empty()) return;
            for (auto &r : ctx.routes[p]) if (r.cells == path) return;
            ctx.routes[p].push_back({path, prior});
        };
        addRoute(0, base.continuation, base.prior);
        for (auto &alt : alternatives) addRoute(0, alt.path, alt.value);
        auto foes = opponentActions(threat);
        addRoute(1, predictedFoeAction.continuation, predictedFoeAction.prior);
        for (auto &foe : foes) addRoute(1, foe.continuation, foe.prior);
        for (auto &path : destinedPaths) {
            addRoute(0, path, 0);
            addRoute(1, path, 0);
        }
        // Either player can exploit a route first proposed from the other side.
        auto ours = ctx.routes[0], theirs = ctx.routes[1];
        for (auto &r : ours) addRoute(1, r.cells, r.prior);
        for (auto &r : theirs) addRoute(0, r.cells, r.prior);
        SearchState initial = searchState();
        initial.rate = searchIncome(initial);
        ctx.normalization = max(S_NORM_MIN, abs(initial.rate) * S_NORM_RATE + S_NORM_ADD);
        auto addMove = [&](int p, SearchMove move) {
            sort(move.cells.begin(), move.cells.begin() + move.count);
            for (const auto &m : ctx.rootMoves[p])
                if (m.count == move.count && m.cells == move.cells && m.attack == move.attack) return;
            if (ctx.rootMoves[p].size() < 18) ctx.rootMoves[p].push_back(move);
        };
        auto convert = [&](const MacroAction &action) {
            SearchMove move;
            for (int c : action.placements) if (move.count < 3) move.cells[move.count++] = c;
            move.attack = action.attack;
            move.prior = action.prior;
            return move;
        };
        addMove(0, convert(base));
        addMove(1, convert(predictedFoeAction));
        for (auto &f : foes) addMove(1, convert(f));
        for (int p = 0; p < 2; ++p) if (!ctx.routes[p].empty()) {
            int player = p ? 1 - myId : myId;
            for (int ordering = 0; ordering < 3; ++ordering) {
                SearchMove m = searchPack(initial, ctx.routes[p][0], player, ordering);
                m.route = 0;
                m.attack = p ? predictedFoeAction.attack : base.attack;
                for (int k = 0; k < m.count; ++k)
                    if (tiles[m.cells[k]].region == m.attack) m.attack = -1;
                addMove(p, m);
            }
        }
        for (int p = 0; p < 2; ++p) {
            int player = p ? 1 - myId : myId;
            SearchMove baseMove = ctx.rootMoves[p][0];
            SearchState placed = initial;
            for (int k = 0; k < baseMove.count; ++k) placed.track[baseMove.cells[k]] = player;
            int placedRate = searchIncome(placed);
            vector<pair<double, int>> attackOptions;
            for (int r = 0; r < (int)regions.size(); ++r) {
                if (elapsedMs() > P_TREE_STOP_MS - 5.0) break;
                if (regions[r].hasTown || initial.instability[r] >= 4) continue;
                bool protectedCell = false;
                for (int k = 0; k < baseMove.count; ++k)
                    protectedCell |= tiles[baseMove.cells[k]].region == r;
                if (protectedCell) continue;
                SearchState after = placed;
                after.instability[r] = 4;
                int gain = (searchIncome(after) - placedRate) * (p ? -1 : 1);
                if (gain < 0) continue;
                int own = 0, foe = 0;
                for (int c : regions[r].cells) {
                    own += placed.track[c] == player;
                    foe += placed.track[c] == 1 - player;
                }
                int hits = 4 - initial.instability[r];
                double value = (gain * min(S_ROOTATK_HOR, 102 - turn - hits) + S_ROOTATK_FOE * foe - own) / hits;
                if (value > 0) attackOptions.emplace_back(value, r);
            }
            sort(attackOptions.rbegin(), attackOptions.rend());
            for (int k = 0; k < min(4, (int)attackOptions.size()); ++k) {
                SearchMove m = baseMove;
                m.attack = attackOptions[k].second;
                addMove(p, m);
            }
            vector<pair<double, int>> ranked;
            for (int r = 0; r < (int)ctx.routes[p].size(); ++r) {
                if (elapsedMs() > P_TREE_STOP_MS - 5.0) break;
                double v = searchRouteValue(initial, ctx.routes[p][r], player);
                if (v <= 0) continue;
                SearchState finished = initial;
                int pieces[4] = {};
                for (int c : ctx.routes[p][r].cells) {
                    if (!tiles[c].town && finished.track[c] == -1) {
                        finished.track[c] = player;
                        ++pieces[tiles[c].terrain + 1];
                    }
                }
                int gain = (searchIncome(finished) - initial.rate) * (p ? -1 : 1);
                int localNet = 0;
                for (int c : ctx.routes[p][r].cells) if (!tiles[c].town)
                    localNet += (finished.track[c] == player) - (finished.track[c] == 1 - player);
                ctx.routes[p][r].incomeScale = localNet > 0 ? max(0.0, (double)gain / localNet) : 0.0;
                if (gain <= 0) continue;
                int turns = pieces[3] + pieces[2] + (max(0, pieces[1] - pieces[2]) + 2) / 3;
                v = gain / pow(turns + 0.5, S_ROOTR_TEXP);
                ranked.emplace_back(v, r);
            }
            sort(ranked.rbegin(), ranked.rend());
            int added = 0;
            for (auto [v, r] : ranked) {
                if (v <= 0 || ++added > 4) break;
                if (!ranked.empty() && v < S_ROOTR_CUT * ranked[0].first) continue;
                SearchMove m = searchPack(initial, ctx.routes[p][r], player);
                m.route = r;
                m.prior = v;
                m.attack = p ? predictedFoeAction.attack : base.attack;
                for (int k = 0; k < m.count; ++k) if (tiles[m.cells[k]].region == m.attack) m.attack = -1;
                addMove(p, m);
                m.attack = searchAttack(initial, m, player, 0, ctx);
                addMove(p, m);
                m.attack = -1;
                addMove(p, m);
            }
            // Attack alternatives can be decisive even when the build is fixed.
            SearchMove m = ctx.rootMoves[p][0];
            for (int k = 0; k < 3; ++k) {
                m.attack = searchAttack(initial, m, player, k, ctx);
                addMove(p, m);
            }
            ctx.trees[p].reserve(2048);
            ctx.trees[p].emplace_back();
        }
        // Candidate generation is deliberately broad, but a 24-simulation
        // search cannot compare 18 root actions reliably. Rank cheaply from
        // the already-computed route and disruption features, then retain the
        // fallback plus the best alternatives. Avoiding extra connectivity
        // scans here is important: otherwise pruning itself consumes enough of
        // the wall-clock budget to make the iteration count non-deterministic.
        auto rootPriority = [&](int p, const SearchMove &move) {
            int player = p ? 1 - myId : myId;
            double value = move.prior;
            if (move.route >= 0 && move.route < (int)ctx.routes[p].size())
                value = searchRouteValue(initial, ctx.routes[p][move.route], player);
            int r = move.attack;
            if (r >= 0 && r < (int)regions.size() && !regions[r].hasTown) {
                int hits = 4 - initial.instability[r];
                double own = 0, foe = 0;
                for (int c : regions[r].cells) {
                    double weight = 1 + S_ATK_DEST * destiny[c];
                    own += weight * (initial.track[c] == player);
                    foe += weight * (initial.track[c] == 1 - player);
                }
                double lostNet = initial.routeBalance[r] * (player == myId ? -1.0 : 1.0);
                if (hits > 0)
                    value += (S_ATK_LOST * lostNet + foe - S_ATK_OWN * own) / (hits * hits);
            }
            return value;
        };
        auto compactRoot = [&](int p) {
            auto &moves = ctx.rootMoves[p];
            if ((int)moves.size() <= P_ROOT_ACTIONS) return;
            vector<pair<double, int>> ranked;
            for (int i = 1; i < (int)moves.size(); ++i)
                ranked.emplace_back(rootPriority(p, moves[i]), i);
            stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
                return a.first > b.first;
            });
            vector<SearchMove> kept;
            kept.reserve(P_ROOT_ACTIONS);
            kept.push_back(moves[0]);
            for (auto [value, index] : ranked) {
                (void)value;
                if ((int)kept.size() == P_ROOT_ACTIONS) break;
                kept.push_back(moves[index]);
            }
            moves = std::move(kept);
        };
        static_assert(P_ROOT_ACTIONS >= 2 && P_ROOT_ACTIONS <= 18,
                      "P_ROOT_ACTIONS must fit SearchNode::edges");
        static_assert(P_TREE_BRANCH >= 1 && P_TREE_BRANCH <= 18,
                      "P_TREE_BRANCH must fit SearchNode::edges");
        compactRoot(0);
        compactRoot(1);
        if (elapsedMs() > P_TREE_STOP_MS - 3.0) return base;

        // The first simulated turn repeats frequently. Cache those exact
        // transitions so the saved connectivity scans can be spent deeper in
        // the tree instead of recomputing identical roots.
        const int ourRootCount = ctx.rootMoves[0].size();
        const int foeRootCount = ctx.rootMoves[1].size();
        vector<SearchState> rootCache(ourRootCount * foeRootCount);
        vector<uint8_t> rootReady(ourRootCount * foeRootCount);
        vector<SearchState> controlCache(foeRootCount);
        vector<uint8_t> controlReady(foeRootCount);
        auto select = [&](int p, int node, int count) {
            SearchNode &n = ctx.trees[p][node];
            int start = random64() % count;
            for (int j = 0; j < count; ++j) {
                int k = (start + j) % count;
                if (n.edges[k].visits == 0) return k;
            }
            int best = 0;
            double bestValue = -1e30, logN = log(n.visits + 1.0);
            for (int k = 0; k < count; ++k) {
                const auto &e = n.edges[k];
                // Paired root advantages have a smaller scale than absolute
                // leaf values, so their exploration bonus must be smaller too.
                double exploration = p == 0 && node == 0 ? S_ROOT_EXPLORE : P_TREE_EXPLORE;
                double q = e.total / e.visits + exploration * sqrt(logN / e.visits);
                if (node == 0 && k == 0) q += S_FIRST_PRIOR / sqrt(e.visits);
                if (q > bestValue) bestValue = q, best = k;
            }
            return best;
        };
        int iterations = 0;
        const int depthLimit = min(P_TREE_DEPTH, 101 - turn);
        while (iterations < P_TREE_ITERS && elapsedMs() < P_TREE_STOP_MS) {
            SearchState state = initial;
            SearchState control = initial;
            array<vector<pair<int, int>>, 2> visited;
            int nodes[2] = {0, 0};
            for (int depth = 0; depth < depthLimit && !state.terminal; ++depth) {
                int choices[2];
                SearchMove moves[2];
                // Both choices are made before either action touches the state.
                for (int p = 0; p < 2; ++p) {
                    int count = depth == 0 ? ctx.rootMoves[p].size() : P_TREE_BRANCH;
                    choices[p] = select(p, nodes[p], count);
                    if (p == 1 && depth == 0 && !predictedFoeAction.placements.empty() &&
                        random64() % 100 < (middleEvidence >= 4 && middleEvidence >= 3 * middleMisses ? 80u : 45u))
                        choices[p] = 0;
                    moves[p] = depth == 0 ? ctx.rootMoves[p][choices[p]] :
                        searchPolicy(state, p ? 1 - myId : myId, choices[p], ctx);
                    visited[p].emplace_back(nodes[p], choices[p]);
                }
                SearchMove controlMoves[2];
                controlMoves[0] = depth == 0 ? ctx.rootMoves[0][0] :
                    searchPolicy(control, myId, choices[0], ctx);
                controlMoves[1] = depth == 0 ? ctx.rootMoves[1][choices[1]] :
                    searchPolicy(control, 1 - myId, choices[1], ctx);
                if (depth == 0) {
                    int cacheIndex = choices[0] * foeRootCount + choices[1];
                    if (!rootReady[cacheIndex]) {
                        rootCache[cacheIndex] = initial;
                        searchStep(rootCache[cacheIndex], moves[0], moves[1]);
                        rootReady[cacheIndex] = 1;
                    }
                    state = rootCache[cacheIndex];
                    if (!controlReady[choices[1]]) {
                        controlCache[choices[1]] = initial;
                        searchStep(controlCache[choices[1]], controlMoves[0], controlMoves[1]);
                        controlReady[choices[1]] = 1;
                    }
                    control = controlCache[choices[1]];
                } else {
                    searchStep(state, moves[0], moves[1]);
                    searchStep(control, controlMoves[0], controlMoves[1]);
                }
                for (int p = 0; p < 2; ++p) {
                    int child = ctx.trees[p][nodes[p]].edges[choices[p]].child;
                    if (child == -1) {
                        child = ctx.trees[p].size();
                        ctx.trees[p][nodes[p]].edges[choices[p]].child = child;
                        ctx.trees[p].emplace_back();
                    }
                    nodes[p] = child;
                }
            }
            double score = searchEvaluation(state, ctx);
            double advantage = score - searchEvaluation(control, ctx);
            for (int p = 0; p < 2; ++p) for (auto [node, choice] : visited[p]) {
                auto &n = ctx.trees[p][node];
                ++n.visits;
                ++n.edges[choice].visits;
                n.edges[choice].total += p ? -score : (node == 0 ? advantage : score);
            }
            ++iterations;
        }
        // A deadline exit uses the precomputed legal action. Besides protecting
        // output time, this avoids choosing from a tree truncated at an
        // arbitrary point in its root exploration order.
        if (elapsedMs() >= P_TREE_STOP_MS ||
            iterations < (int)ctx.rootMoves[0].size() * 2) return base;
        int best = 0;
        for (int i = 1; i < (int)ctx.rootMoves[0].size(); ++i)
            if (ctx.trees[0][0].edges[i].visits > ctx.trees[0][0].edges[best].visits) best = i;
        const SearchMove &m = ctx.rootMoves[0][best];
        if (base.placements.empty() && m.count == 0 && m.attack == -1) return base;
        SearchState baseNow = initial, selectedNow = initial;
        searchStep(baseNow, ctx.rootMoves[0][0], SearchMove{});
        searchStep(selectedNow, m, SearchMove{});
        if (baseNow.rate > initial.rate && selectedNow.rate < baseNow.rate) return base;
        MacroAction result;
        for (int k = 0; k < m.count; ++k) result.placements.push_back(m.cells[k]);
        result.attack = m.attack;
        result.prior = m.prior;
        result.continuation = m.route >= 0 ? ctx.routes[0][m.route].cells : plan.path;
#ifdef SEARCH_DIAGNOSTICS
        cerr << "simulations=" << iterations << " search_ms=" << elapsedMs()
             << " chosen=" << best << " candidates=" << ctx.rootMoves[0].size() << '\n';
#endif
        return result;
    }


    void gameTurn() {
        started = chrono::steady_clock::now();
        // Opponent modelling shares our real turn budget. Starting its internal
        // clock partway through the turn forces the recursive policy evaluation
        // to use a small slice instead of consuming another full 50 ms.
        if (predictingMiddle) started -= chrono::milliseconds(25);
        else started -= chrono::milliseconds(5);
        updateDestiny();
        vector<int> threat = predictOpponentThreat();

        vector<bool> protectedRegions(regions.size(), false);
        int attack = chooseAttack(owners, protectedRegions);

        auto [plan, altPlans] = choosePlan(attack, threat);
        if (plan.path.empty() && attack != -1 && elapsedMs() < 22.0) {
            auto res = choosePlan(-1, threat);
            plan = res.first;
            altPlans = res.second;
        }

        vector<string> actions;
        vector<int> projected = owners;
        int budget = 3;

        vector<int> primaryMissing;
        for (int cell : plan.path) {
            protectedRegions[tiles[cell].region] = true;
            if (!tiles[cell].town && owners[cell] == -1) {
                primaryMissing.push_back(cell);
            }
        }

        auto urgency = [&](int cell) {
            int score = 0;
            for (int next : neighbors[cell]) {
                if (tiles[next].town || owners[next] == myId) score += 500;
                if (owners[next] == 1 - myId) score += 200;
            }
            // Deliberately not weighted by destiny here: ordering placement by
            // multiplicity scatters paint across a plan's high-value cells and
            // delays the completion that actually starts the income.
            score += threat[cell] * 50;
            score += ((cell * 37 + myId * 71) % 101);
            return score;
        };

        stable_sort(primaryMissing.begin(), primaryMissing.end(),
                    [&](int a, int b) { return urgency(a) > urgency(b); });

        auto takeFromCells = [&](vector<int> &cellList) {
            vector<int> byCost[4];
            for (int cell : cellList) {
                if (projected[cell] == -1) {
                    byCost[tiles[cell].terrain + 1].push_back(cell);
                }
            }
            for (int cost = 1; cost <= 3; ++cost) {
                stable_sort(byCost[cost].begin(), byCost[cost].end(),
                            [&](int a, int b) { return urgency(a) > urgency(b); });
            }

            if (budget == 3 && !byCost[3].empty()) {
                int cell = byCost[3].front();
                actions.push_back("PLACE_TRACKS " + to_string(cell % width) + " " + to_string(cell / width));
                projected[cell] = myId;
                budget -= 3;
                return;
            }
            if (budget == 3 && !byCost[2].empty() && !byCost[1].empty()) {
                int c2 = byCost[2].front();
                int c1 = byCost[1].front();
                actions.push_back("PLACE_TRACKS " + to_string(c2 % width) + " " + to_string(c2 / width));
                actions.push_back("PLACE_TRACKS " + to_string(c1 % width) + " " + to_string(c1 / width));
                projected[c2] = myId;
                projected[c1] = myId;
                budget -= 3;
                return;
            }
            if (budget == 3 && byCost[1].size() >= 3) {
                for (int i = 0; i < 3; ++i) {
                    int c = byCost[1][i];
                    actions.push_back("PLACE_TRACKS " + to_string(c % width) + " " + to_string(c / width));
                    projected[c] = myId;
                }
                budget -= 3;
                return;
            }

            for (int cost = 3; cost >= 1; --cost) {
                for (int cell : byCost[cost]) {
                    if (cost <= budget && projected[cell] == -1) {
                        actions.push_back("PLACE_TRACKS " + to_string(cell % width) + " " + to_string(cell / width));
                        projected[cell] = myId;
                        budget -= cost;
                    }
                }
            }
        };

        // 0. If this turn's paint can finish a connection outright, do that first.
        //    A connection starts paying the moment it completes, so a turn of delay
        //    costs its full rate for every turn remaining. Long-run plan value is
        //    worth nothing next to income already on the board.
        if (budget > 0 && elapsedMs() < 34.0) {
            const double inf = numeric_limits<double>::infinity();
            int baseNet = income(projected, attack).net();
            int bestGain = 0;
            vector<int> bestSet;
            for (const Town &town : towns) {
                if (town.requests.empty() || blocked(town.cell, attack)) continue;
                if (elapsedMs() > 36.0) break;
                fill(dijkstraDist.begin(), dijkstraDist.end(), inf);
                fill(dijkstraParent.begin(), dijkstraParent.end(), -1);
                using State = pair<double, int>;
                priority_queue<State, vector<State>, greater<State>> pq;
                dijkstraDist[town.cell] = 0;
                pq.emplace(0, town.cell);
                while (!pq.empty()) {
                    auto [d, u] = pq.top();
                    pq.pop();
                    if (d > dijkstraDist[u]) continue;
                    for (int v : neighbors[u]) {
                        if (blocked(v, attack)) continue;
                        // Cells we already hold are free; only the gaps cost paint.
                        double w = (tiles[v].town || projected[v] != -1) ? 0.0 : tiles[v].terrain + 1;
                        if (d + w + 1e-9 < dijkstraDist[v]) {
                            dijkstraDist[v] = d + w;
                            dijkstraParent[v] = u;
                            pq.emplace(dijkstraDist[v], v);
                        }
                    }
                }
                for (int target : town.requests) {
                    int finish = towns[target].cell;
                    if (!isfinite(dijkstraDist[finish]) || dijkstraDist[finish] > budget + 1e-9) continue;
                    vector<int> need = {};
                    vector<int> proj = projected;
                    for (int c = finish; c != -1; c = dijkstraParent[c])
                        if (!tiles[c].town && proj[c] == -1) {
                            need.push_back(c);
                            proj[c] = myId;
                        }
                    if (need.empty()) continue;
                    // Completing a line that runs mostly through their track would
                    // pay them more than us, so only take it if the net improves.
                    int gain = income(proj, attack).net() - baseNet;
                    if (gain > bestGain) {
                        bestGain = gain;
                        bestSet = need;
                    }
                }
            }
            for (int cell : bestSet) {
                int cost = tiles[cell].terrain + 1;
                if (cost > budget) continue;
                actions.push_back("PLACE_TRACKS " + to_string(cell % width) + " " + to_string(cell / width));
                projected[cell] = myId;
                protectedRegions[tiles[cell].region] = true;
                budget -= cost;
            }
        }

        // 1. Build primary plan
        takeFromCells(primaryMissing);

        // 2. Spillover into secondary plans to prevent wasted paint
        if (budget > 0) {
            for (const auto &alt : altPlans) {
                if (budget == 0) break;
                if (alt.path == plan.path) continue;
                vector<int> altMissing;
                for (int cell : alt.path) {
                    if (!tiles[cell].town && projected[cell] == -1) {
                        protectedRegions[tiles[cell].region] = true;
                        altMissing.push_back(cell);
                    }
                }
                takeFromCells(altMissing);
            }
        }

        // 3. Fill leftover paint, but never blindly. Track placed off the scoring
        //    geometry is not merely wasted: it can shorten a route so that it now
        //    runs through the opponent's cells, handing them income we would
        //    otherwise have had to ourselves. So a filler cell must sit on some
        //    connection's destined path, and must be checked against the exact
        //    income() before it is committed.
        if (budget > 0 && elapsedMs() < 36.0) {
            int remaining = 101 - turn;
            int baseNet = income(projected, attack).net();
            vector<int> filler;
            for (int cell = 0; cell < (int)tiles.size(); ++cell) {
                if (tiles[cell].town || projected[cell] != -1 || blocked(cell, attack)) continue;
                if (tiles[cell].terrain + 1 > budget) continue;
                filler.push_back(cell);
            }
            // Paint does not carry over, so leaving it unspent is a pure loss;
            // the discipline is to spend all of it but never on a cell that
            // measurably hurts. Destined cells come first because they are the
            // ones certain to sit on a scoring route later.
            // Rank by points-per-paint: three plains each carrying a connection
            // beat one mountain carrying the same, for the same three paint.
            stable_sort(filler.begin(), filler.end(), [&](int a, int b) {
                double ra = destiny[a] / (double)(tiles[a].terrain + 1);
                double rb = destiny[b] / (double)(tiles[b].terrain + 1);
                if (ra != rb) return ra > rb;
                return urgency(a) > urgency(b);
            });
            int checked = 0;
            for (int cell : filler) {
                if (budget <= 0 || ++checked > 26 || elapsedMs() > 41.0) break;
                int cost = tiles[cell].terrain + 1;
                if (cost > budget) continue;
                projected[cell] = myId;
                int after = income(projected, attack).net();
                // Reject anything that gives the opponent a better route than we
                // had, and near the end reject anything that does not pay off now,
                // since there is no longer time for the investment to mature.
                if (after < baseNet || (after == baseNet && remaining < 3)) {
                    projected[cell] = -1;
                    continue;
                }
                baseNet = after;
                actions.push_back("PLACE_TRACKS " + to_string(cell % width) + " " + to_string(cell / width));
                protectedRegions[tiles[cell].region] = true;
                budget -= cost;
            }
        }

        // Final attack evaluation can recompute whole-board income many times.
        // If the turn is already late, retain the earlier legal target (or drop
        // it if construction now protects that region) and print immediately.
        if (elapsedMs() < P_TREE_STOP_MS + 3.0)
            attack = chooseAttack(projected, protectedRegions);
        else if (attack < 0 || attack >= (int)regions.size() || protectedRegions[attack])
            attack = -1;
        if (attack != -1) {
            actions.push_back("DISRUPT " + to_string(regions[attack].id));
        }
        lastAttack = attack;
        previousPath = plan.path;

        MacroAction baseAction;
        baseAction.attack = attack;
        baseAction.prior = plan.value;
        baseAction.continuation = plan.path;
        for (int cell = 0; cell < (int)tiles.size(); ++cell)
            if (owners[cell] == -1 && projected[cell] == myId)
                baseAction.placements.push_back(cell);
        if (P_PREDICT_FOE && !predictingMiddle) predictMiddleBotAction();
        MacroAction selected = predictingMiddle ? baseAction :
                               treeSmitsimax(baseAction, plan, altPlans, threat);
        lastDecision = selected;

        if (suppressOutput) return;
        lastAttack = selected.attack;
        previousPath = selected.continuation;
        previousFoePrediction = predictedFoeAction;
        previousOwnAction = selected;

        actions.clear();
        for (int cell : selected.placements)
            actions.push_back("PLACE_TRACKS " + to_string(cell % width) + " " +
                              to_string(cell / width));
        if (selected.attack != -1)
            actions.push_back("DISRUPT " + to_string(regions[selected.attack].id));

        if (actions.empty()) cout << "WAIT";
        for (int i = 0; i < (int)actions.size(); ++i) {
            if (i) cout << ';';
            cout << actions[i];
        }
        cout << endl;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game game;
    if (!game.init()) return 0;
    while (game.parse()) game.gameTurn();
    return 0;
}
