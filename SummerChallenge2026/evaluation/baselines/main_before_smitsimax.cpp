#include <algorithm>
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
#ifndef P_SMITS_DEPTH
#define P_SMITS_DEPTH 4
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
                               P_ATK_TRK * (foe[r] - 1.5 * mine[r]) +
                               P_ATK_DEST * destinyEdge[r]) / pow(hits, P_ATK_HEXP);
            candidates.emplace_back(estimate, r);
        }
        sort(candidates.rbegin(), candidates.rend());

        double bestValue = 0.05;
        int best = -1, examined = 0;
        for (auto [estimate, r] : candidates) {
            (void)estimate;
            if (++examined > 20 || elapsedMs() > 38.0) break;
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
                int horizon = min(remaining, 25);
                value = gain * horizon + 1.2 * foe[r] - 1.8 * mine[r] + P_ATK_DEST * destinyEdge[r];
                if (foe[r] > 0) value += 2.0 * foe[r];
            } else if (hits == 2) {
                int horizon = min(remaining - 1, 18);
                value = (gain * horizon + 0.8 * foe[r] - 1.5 * mine[r] + P_ATK_DEST * destinyEdge[r]) / 2.0;
                if (r == lastAttack) value *= 1.25;
            } else {
                int horizon = min(remaining - hits + 1, 10);
                value = (gain * horizon + 0.5 * foe[r] - 1.5 * mine[r] + P_ATK_DEST * destinyEdge[r]) / (hits * 1.6);
                if (r == lastAttack) value *= 1.15;
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

        if (altPlans.size() < 4) {
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

        for (int mode = 0; mode < 5; ++mode) {
            for (const Town &town : towns) {
                if (town.requests.empty() || blocked(town.cell)) continue;
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

    int simulatedNetIncome(const vector<int> &track, const vector<bool> &destroyed) const {
        int result = 0;
        for (const Town &town : towns) {
            if (town.requests.empty() || destroyed[tiles[town.cell].region]) continue;
            fill(bfsParent.begin(), bfsParent.end(), -2);
            int head = 0, tail = 0;
            bfsQueue[tail++] = town.cell;
            bfsParent[town.cell] = -1;
            while (head < tail) {
                int cell = bfsQueue[head++];
                for (int next : neighbors[cell]) {
                    if (bfsParent[next] != -2 || destroyed[tiles[next].region]) continue;
                    if (!tiles[next].town && track[next] == -1) continue;
                    bfsParent[next] = cell;
                    bfsQueue[tail++] = next;
                }
            }
            for (int target : town.requests) {
                int cell = towns[target].cell;
                if (bfsParent[cell] == -2) continue;
                for (; cell != -1; cell = bfsParent[cell]) {
                    if (tiles[cell].town) continue;
                    result += track[cell] == myId;
                    result -= track[cell] == 1 - myId;
                }
            }
        }
        return result;
    }

    double macroPayoff(const MacroAction &mineAction, const MacroAction &foeAction) const {
        vector<int> track = owners;
        vector<int> instability(regions.size());
        vector<bool> destroyed(regions.size());
        for (int r = 0; r < (int)regions.size(); ++r) {
            instability[r] = regions[r].instability;
            destroyed[r] = regions[r].inked;
        }

        auto continuationPlacement = [&](const vector<int> &path, int player) {
            vector<int> byCost[4];
            vector<bool> seen(tiles.size(), false);
            for (int cell : path) {
                if (cell < 0 || cell >= (int)tiles.size() || seen[cell]) continue;
                seen[cell] = true;
                if (!tiles[cell].town && track[cell] == -1 && !destroyed[tiles[cell].region])
                    byCost[tiles[cell].terrain + 1].push_back(cell);
            }
            auto order = [&](int a, int b) {
                int va = 20 * destiny[a], vb = 20 * destiny[b];
                for (int next : neighbors[a]) va += tiles[next].town || track[next] == player ? 60 : 0;
                for (int next : neighbors[b]) vb += tiles[next].town || track[next] == player ? 60 : 0;
                return va > vb;
            };
            for (int cost = 1; cost <= 3; ++cost)
                stable_sort(byCost[cost].begin(), byCost[cost].end(), order);
            vector<int> result;
            if (!byCost[3].empty()) return vector<int>{byCost[3][0]};
            if (!byCost[2].empty()) {
                result.push_back(byCost[2][0]);
                if (!byCost[1].empty()) result.push_back(byCost[1][0]);
                return result;
            }
            for (int i = 0; i < min(3, (int)byCost[1].size()); ++i)
                result.push_back(byCost[1][i]);
            return result;
        };

        double value = 0;
        int remaining = 101 - turn;
        int depth = min(P_SMITS_DEPTH, remaining);
        for (int ply = 0; ply < depth; ++ply) {
            vector<int> minePlacement = ply == 0 ? mineAction.placements :
                                        continuationPlacement(mineAction.continuation, myId);
            vector<int> foePlacement = ply == 0 ? foeAction.placements :
                                       continuationPlacement(foeAction.continuation, 1 - myId);
            vector<bool> minePlaced(tiles.size(), false), foePlaced(tiles.size(), false);
            for (int cell : minePlacement)
                if (track[cell] == -1 && !destroyed[tiles[cell].region]) minePlaced[cell] = true;
            for (int cell : foePlacement)
                if (track[cell] == -1 && !destroyed[tiles[cell].region]) foePlaced[cell] = true;
            for (int cell = 0; cell < (int)tiles.size(); ++cell) {
                if (minePlaced[cell] && foePlaced[cell]) track[cell] = 2;
                else if (minePlaced[cell]) track[cell] = myId;
                else if (foePlaced[cell]) track[cell] = 1 - myId;
            }

            int mineAttack = mineAction.attack;
            int foeAttack = foeAction.attack;
            if (mineAttack != -1 && !destroyed[mineAttack] && !regions[mineAttack].hasTown)
                ++instability[mineAttack];
            if (foeAttack != -1 && !destroyed[foeAttack] && !regions[foeAttack].hasTown)
                ++instability[foeAttack];
            for (int r = 0; r < (int)regions.size(); ++r) {
                if (!regions[r].hasTown && instability[r] >= 4) destroyed[r] = true;
            }
            for (int cell = 0; cell < (int)tiles.size(); ++cell)
                if (destroyed[tiles[cell].region]) track[cell] = -1;
            value += simulatedNetIncome(track, destroyed) * (1.0 - 0.035 * ply);
        }

        int netIncome = simulatedNetIncome(track, destroyed);
        value += netIncome * min(12, max(0, remaining - depth));
        vector<double> regionMine(regions.size()), regionFoe(regions.size());
        for (int cell = 0; cell < (int)tiles.size(); ++cell) {
            int r = tiles[cell].region;
            if (tiles[cell].town || destroyed[r]) continue;
            double weight = 0.15 + 0.32 * destiny[cell] / (tiles[cell].terrain + 1.0);
            if (track[cell] == myId) {
                value += weight;
                regionMine[r] += 1.0 + 0.5 * destiny[cell];
            } else if (track[cell] == 1 - myId) {
                value -= weight;
                regionFoe[r] += 1.0 + 0.5 * destiny[cell];
            }
        }
        for (int r = 0; r < (int)regions.size(); ++r) {
            if (regions[r].hasTown || destroyed[r]) continue;
            value += 0.18 * instability[r] * (regionFoe[r] - regionMine[r]);
        }
        return value;
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

    MacroAction smitsimax(const MacroAction &base, const Plan &plan,
                          const vector<Plan> &altPlans, const vector<int> &threat) {
        if ((predictedFoeAction.placements.empty() && predictedFoeAction.attack == -1) ||
            elapsedMs() > 29.0) return base;
        vector<MacroAction> mineActions;
        unordered_map<string, bool> used;
        auto addMine = [&](MacroAction action) {
            sort(action.placements.begin(), action.placements.end());
            string key = to_string(action.attack) + ":";
            for (int cell : action.placements) key += to_string(cell) + ",";
            if (used.emplace(key, true).second) mineActions.push_back(std::move(action));
        };
        addMine(base);
        MacroAction noAttack = base;
        noAttack.attack = -1;
        noAttack.prior -= 0.2;
        addMine(std::move(noAttack));
        if (!predictedFoeAction.placements.empty() && middleEvidence >= 4 &&
            middleEvidence >= 3 * middleMisses) {
            MacroAction counter;
            counter.placements = predictedFoeAction.placements;
            counter.attack = base.attack;
            counter.prior = base.prior;
            counter.continuation = predictedFoeAction.continuation;
            if (counter.attack != -1) {
                for (int cell : counter.placements) {
                    if (tiles[cell].region == counter.attack) {
                        counter.attack = approximateAttack(myId, counter.placements);
                        break;
                    }
                }
            }
            addMine(std::move(counter));
        }
        vector<Plan> plans = altPlans;
        if (!plan.path.empty()) plans.push_back(plan);
        sort(plans.begin(), plans.end(), [](const Plan &a, const Plan &b) { return a.value > b.value; });
        for (const Plan &candidate : plans) {
            if (mineActions.size() >= 6) break;
            vector<int> placement = packPath(candidate.path, myId, threat);
            if (placement.empty()) continue;
            int attack = base.attack;
            for (int cell : placement)
                if (attack != -1 && tiles[cell].region == attack) attack = approximateAttack(myId, placement);
            addMine({std::move(placement), attack, candidate.value, candidate.path});
        }
        vector<MacroAction> foeActions = opponentActions(threat);
        if (mineActions.size() < 2 || foeActions.empty() || elapsedMs() > 35.0) return base;

        const int n = mineActions.size(), m = foeActions.size();
        vector<vector<double>> payoff(n, vector<double>(m));
        double low = numeric_limits<double>::infinity(), high = -low;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) {
                payoff[i][j] = macroPayoff(mineActions[i], foeActions[j]);
                low = min(low, payoff[i][j]);
                high = max(high, payoff[i][j]);
            }
            if (elapsedMs() > 40.0) return base;
        }
        double scale = max(1.0, high - low);
        for (auto &row : payoff)
            for (double &score : row) score = (score - low) / scale * 2.0 - 1.0;

        vector<int> myVisits(n), foeVisits(m);
        vector<double> myValue(n), foeValue(m);
        int total = 0;
        auto select = [&](const vector<int> &visits, const vector<double> &values,
                          const vector<MacroAction> &actions) {
            vector<int> unvisited;
            for (int i = 0; i < (int)visits.size(); ++i)
                if (visits[i] == 0) unvisited.push_back(i);
            if (!unvisited.empty()) return unvisited[random64() % unvisited.size()];
            int best = 0;
            double bestScore = -1e100;
            for (int i = 0; i < (int)visits.size(); ++i) {
                double progressive = 0.03 * tanh(actions[i].prior / 10.0) / (visits[i] + 1.0);
                double ucb = values[i] / visits[i] +
                             0.72 * sqrt(log((double)total + 1.0) / visits[i]) + progressive;
                if (ucb > bestScore) {
                    bestScore = ucb;
                    best = i;
                }
            }
            return best;
        };

        while (total < 4000 && elapsedMs() < 43.0) {
            int a = select(myVisits, myValue, mineActions);
            int b = select(foeVisits, foeValue, foeActions);
            double score = payoff[a][b];
            ++myVisits[a];
            myValue[a] += score;
            ++foeVisits[b];
            foeValue[b] -= score;
            ++total;
        }
        int best = 0;
        double bestModelScore = -1e100;
        for (int i = 0; i < n; ++i) {
            double mean = myVisits[i] ? myValue[i] / myVisits[i] : -1.0;
            // The first opponent candidate is the independently computed action
            // of the known middle-strength policy. Retain some adversarial UCT
            // weight so the choice does not become completely opponent-specific.
            double modelWeight = (middleEvidence >= 4 && middleEvidence >= 3 * middleMisses) ?
                                 0.90 : 0.72;
            double modelScore = modelWeight * payoff[i][0] + (1.0 - modelWeight) * mean;
            if (modelScore > bestModelScore) {
                bestModelScore = modelScore;
                best = i;
            }
        }
        double chosenMean = myVisits[best] ? myValue[best] / myVisits[best] : -1e100;
        double baseMean = myVisits[0] ? myValue[0] / myVisits[0] : -1e100;
        if (best != 0 && chosenMean + 0.015 < baseMean) return base;
        // The rollout deliberately simplifies replanning after this turn. Do not
        // let it discard a clearly superior strategic plan merely because a
        // lower-ranked route looks good inside that short fixed continuation.
        if (best != 0 && base.prior > 0 && mineActions[best].prior < 0.95 * base.prior)
            return base;
        return mineActions[best];
    }

    void gameTurn() {
        started = chrono::steady_clock::now();
        // Opponent modelling shares our real turn budget. Starting its internal
        // clock partway through the turn forces the recursive policy evaluation
        // to use a small slice instead of consuming another full 50 ms.
        if (predictingMiddle) started -= chrono::milliseconds(25);
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

        // Final attack evaluation using projected placements
        attack = chooseAttack(projected, protectedRegions);
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
        if (!predictingMiddle) predictMiddleBotAction();
        MacroAction selected = predictingMiddle ? baseAction :
                               smitsimax(baseAction, plan, altPlans, threat);
        lastDecision = selected;

        if (suppressOutput) return;
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
}

