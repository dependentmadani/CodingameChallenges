#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

            double estimate = (14.0 * max(0, affected[r]) +
                               0.8 * (foe[r] - 1.5 * mine[r]) +
                               1.0 * destinyEdge[r]) / pow(hits, 1.35);
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
                value = gain * horizon + 1.2 * foe[r] - 1.8 * mine[r] + 1.0 * destinyEdge[r];
                if (foe[r] > 0) value += 2.0 * foe[r];
            } else if (hits == 2) {
                int horizon = min(remaining - 1, 18);
                value = (gain * horizon + 0.8 * foe[r] - 1.5 * mine[r] + 1.0 * destinyEdge[r]) / 2.0;
                if (r == lastAttack) value *= 1.25;
            } else {
                int horizon = min(remaining - hits + 1, 10);
                value = (gain * horizon + 0.5 * foe[r] - 1.5 * mine[r] + 1.0 * destinyEdge[r]) / (hits * 1.6);
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

        double maxHorizon = allImmune ? (remaining - turns + 1) : min((double)(remaining - turns + 1), 16.0 + (100 - turn) / 2.5);
        double lifetime = maxHorizon / (1.0 + 0.5 * risk * risk);

        double value = gain * lifetime / pow(turns + 0.5, 1.30) - 0.03 * paint;
        if (turns == 1) value *= 1.20;
        if (path == previousPath) value *= 1.08;

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
        for (const vector<int> &destined : destinedPaths)
            consider(destined, excluded, base, best, altPlans);

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

    void gameTurn() {
        started = chrono::steady_clock::now();
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
