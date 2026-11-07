// Fall Challenge 2026 (TryAngle-Catch) bot
#include <bits/stdc++.h>
using namespace std;
using Clock = chrono::steady_clock;

static const int MAXN = 50;
static const int MAXU = 200;

struct Tri {
    int n[3];
    int owner;   // -1 neutral, 0 me, 1 opp
    bool cc[2];  // can capture: me, opp
};

int N;
int X[MAXN], Y[MAXN];
int turnNo = 0;
int myScore, oppScore;
int U[2][MAXN];  // units: 0 me, 1 opp
vector<int> adj[MAXN];
int D[MAXN][MAXN];
uint64_t distanceMask[MAXN][MAXN];
int maxReachDistance[MAXN];

// Distances do not change during a turn. Bit sets let every evaluation find
// the nearest available unit without scanning all occupied houses again.
void prepareDistanceMasks() {
    memset(distanceMask, 0, sizeof distanceMask);
    memset(maxReachDistance, 0, sizeof maxReachDistance);
    for (int v = 0; v < N; ++v) for (int u = 0; u < N; ++u) {
        if (D[u][v] >= N) continue;
        distanceMask[v][D[u][v]] |= 1ULL << u;
        maxReachDistance[v] = max(maxReachDistance[v], D[u][v]);
    }
}

inline int nearestAvailable(int target, uint64_t available, int& distance) {
    for (int d = 0; d <= maxReachDistance[target]; ++d) {
        uint64_t candidates = available & distanceMask[target][d];
        if (candidates) { distance = d; return __builtin_ctzll(candidates); }
    }
    distance = 1000;
    return -1;
}
vector<Tri> tris;
vector<array<bool, 3>> pureCorner;
vector<vector<vector<int>>> occSets;  // per triangle: house sets whose occupation owns all three corners  // corner whose neighbors are only the other two corners (owned by surround for free)

mt19937 rng(12345);

// ---------------- parameters (overridable via argv for tuning)
double P_TRI = 10.0;       // owned triangle
double P_UNIT = 4.0;       // unit (scaled by game phase)
double P_POT = 3.0;        // capture potential
double P_GAMMA = 0.5;      // potential decay per distance
double P_THREAT = 5.0;     // my triangle threatened
double P_FRONT = 0.05;     // frontier pull per distance
int SPAWN_END = 150;       // stop routine spawning after this turn
double SPAWN_CAP = 1.0;    // routine spawning while my units < SPAWN_CAP * N
double TIME_BUDGET = 42.0; // ms; leave headroom under the 50 ms turn limit
double PRED_W = 1.0;       // weight of evaluation against predicted opponent moves
double PRED_FRAC = 0.4;    // share of the time budget spent predicting the opponent

struct Spawn { int tri; int corner; };
struct Attack { int tri, target; bool on; int cons[3]; int savedSpawn; };
struct Plan {
    int nUnits = 0;
    int from[MAXU];
    vector<int> opts[MAXU];
    int dest[MAXU];
    bool alive[MAXU];
    vector<Spawn> spawns;
    int spawnOf[64];  // index into spawns per triangle, -1 if none
    vector<Attack> atks;
};
Plan plans[2];

// what one side does, as seen by the other side's evaluation
struct View {
    int cnt[MAXN];            // unit counts after moves/spawns
    bool used[64];            // triangles used (spawn/attack) this turn
    vector<int> dests[MAXN];  // per start node: destinations of its units (to apply kills)
    bool killed[MAXN];        // nodes where the other side's units get killed by attacks
};
int LEAGUE = 2;
int ATK_SIDES = 3;     // bit 0: my attacks, bit 1: predicted opponent attacks
double P_ATK = 8.0;  // extra cost per attack
int PRED_ITER = 0;     // re-predict the opponent against my first plan
int P_PURE = 1;
int P_PAT = 0;         // occupation-set capture potential
double P_CYCLE = 0.0;  // bonus per used triangle made capturable again
double P_STACK = 0.0;  // probability of a whole-stack move mutation
int P_OUTNUM = 0;      // potential needs enough units to outnumber the opponent on a corner
double P_INF = 0.0;    // triangles controlled by influence (units within one step)
int ROBUST = 1;            // plan against several opponent replies
double RW_STATIC = 0.5, RW_PRED = 1.0, RW_BR = 1.0, RW_ADV = 0.5;  // their weights
int P_RACE = 0;            // weigh capture potential by who reaches the triangle first
double P_RACE_LOSE = 0.2;  // potential share kept for triangles the other side reaches first
double P_RACE_TIE = 0.6;   // ... and for ties
double P_ROB = 0.0;    // discount (share of a triangle) for captures the other side could have blocked
double P_DENY = 0.0;   // penalty per neutral triangle the other side could take in the worst case
int reachStart[2][MAXN];
double lastTerms[5];  // last evaluation's main terms: triangles, units, potential, threat, frontier (diagnostics)  // units a side can bring onto each house next turn from its start positions
double P_NODE = 0.0;   // owned houses (incl. surround-owned)
double SA_T0 = 1.0;    // annealing start temperature (0 = hill climbing)
double P_MULTI = 0.3;  // probability of moving a small group of nearby units at once
double P_SAFE = 0.0;   // surround risk weight (in units)

double unitValue() {
    double rem = max(0, 200 - turnNo);
    return P_UNIT * rem / 200.0;
}

// ownership of node v given counts a (side) and b (other): 0 side, 1 other, -1 none
inline int nodeOwner(const int* a, const int* b, int v) {
    if (!adj[v].empty()) {
        bool all0 = true, all1 = true;
        for (int n : adj[v]) {
            if (!(a[n] > b[n])) all0 = false;
            if (!(b[n] > a[n])) all1 = false;
        }
        if (all0) return 0;
        if (all1) return 1;
    }
    if (a[v] > b[v]) return 0;
    if (b[v] > a[v]) return 1;
    return -1;
}

// counts after a plan's attacks, moves and spawns; marks used triangles; killed = nodes where our units die first
void planCounts(const Plan& p, int* cnt, bool* used, const bool* killed) {
    memset(cnt, 0, sizeof(int) * N);
    for (int i = 0; i < p.nUnits; i++)
        if (p.alive[i] && !(killed && killed[p.from[i]])) cnt[p.dest[i]]++;
    for (auto& sp : p.spawns) {
        if (sp.corner < 0) continue;
        cnt[tris[sp.tri].n[sp.corner]]++;
        used[sp.tri] = true;
    }
    for (auto& a : p.atks) if (a.on) used[a.tri] = true;
}

// finalize step for side s's perspective (0 = s, 1 = other): allowed captures, surround kills, captures
struct FinalizeOut { int own[MAXN]; int myTri, opTri, killedMe, killedOp; };
void finalizeSim(int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1, FinalizeOut& fo) {
    int T = tris.size();
    // finalize: allowed captures
    for (int t = 0; t < T; t++) {
        const Tri& tr = tris[t];
        if (owner[t] != -1) { cc0[t] = owner[t] != 0; cc1[t] = owner[t] != 1; }
        else {
            cc0[t] = cc0[t] || (cnt[tr.n[0]] == 0 && cnt[tr.n[1]] == 0 && cnt[tr.n[2]] == 0);
            cc1[t] = cc1[t] || (ocnt[tr.n[0]] == 0 && ocnt[tr.n[1]] == 0 && ocnt[tr.n[2]] == 0);
        }
    }
    // surround
    bool advMe[MAXN], advOp[MAXN];
    for (int v = 0; v < N; v++) { advMe[v] = cnt[v] > ocnt[v]; advOp[v] = ocnt[v] > cnt[v]; }
    int killedMe = 0, killedOp = 0;
    for (int v = 0; v < N; v++) {
        if (adj[v].empty()) continue;
        if (cnt[v] > 0) {
            bool all = true;
            for (int n : adj[v]) if (!advOp[n]) { all = false; break; }
            if (all) { killedMe += cnt[v]; cnt[v] = 0; }
        }
        if (ocnt[v] > 0) {
            bool all = true;
            for (int n : adj[v]) if (!advMe[n]) { all = false; break; }
            if (all) { killedOp += ocnt[v]; ocnt[v] = 0; }
        }
    }
    int* own = fo.own;
    // The referee starts another finalize pass after surround deaths, before
    // capturing. A used triangle may become eligible when its last units die.
    if (killedMe || killedOp) {
        for (int t = 0; t < T; t++) if (owner[t] == -1) {
            const Tri& tr = tris[t];
            cc0[t] = cc0[t] || (cnt[tr.n[0]] == 0 && cnt[tr.n[1]] == 0 && cnt[tr.n[2]] == 0);
            cc1[t] = cc1[t] || (ocnt[tr.n[0]] == 0 && ocnt[tr.n[1]] == 0 && ocnt[tr.n[2]] == 0);
        }
    }
    for (int v = 0; v < N; v++) own[v] = nodeOwner(cnt, ocnt, v);
    int myTri = 0, opTri = 0;
    for (int t = 0; t < T; t++) {
        const Tri& tr = tris[t];
        if (cc0[t] && own[tr.n[0]] == 0 && own[tr.n[1]] == 0 && own[tr.n[2]] == 0) owner[t] = 0;
        if (cc1[t] && own[tr.n[0]] == 1 && own[tr.n[1]] == 1 && own[tr.n[2]] == 1) owner[t] = 1;
        if (owner[t] == 0) myTri++;
        else if (owner[t] == 1) opTri++;
        // Capture triggers another finalize pass too; preserve its eligibility
        // state for the next turn of the multi-turn search.
        if (owner[t] != -1) { cc0[t] = owner[t] != 0; cc1[t] = owner[t] != 1; }
    }
    fo.myTri = myTri; fo.opTri = opTri; fo.killedMe = killedMe; fo.killedOp = killedOp;
}

// static evaluation terms of a finalized position for side s
double evalTerms(int s, const int* cnt, const int* ocnt, const int* owner, const bool* cc0, const bool* cc1, const FinalizeOut& fo, bool dbg) {
    int T = tris.size();
    const int* own = fo.own;
    int myTri = fo.myTri, opTri = fo.opTri;
    int myUnits = 0, opUnits = 0;
    for (int v = 0; v < N; v++) { myUnits += cnt[v]; opUnits += ocnt[v]; }

    double score = P_TRI * (myTri - opTri) + unitValue() * (myUnits - opUnits);
    lastTerms[0] = P_TRI * (myTri - opTri);
    lastTerms[1] = unitValue() * (myUnits - opUnits);
    if (myTri >= 0.8 * T) score += 10000;
    if (P_NODE != 0) {
        int nodes = 0;
        for (int v = 0; v < N; v++) nodes += own[v] == 0 ? 1 : (own[v] == 1 ? -1 : 0);
        score += P_NODE * nodes;
    }

    int stacks[MAXN], ns = 0;
    for (int v = 0; v < N; v++) if (cnt[v] > 0) stacks[ns++] = v;
    // potential for capturable triangles
    double pot = 0;
    int ostacks[MAXN], ons = 0, ospare[MAXN];
    for (int v = 0; v < N; v++) if (ocnt[v] > 0) ostacks[ons++] = v;
    int spare[MAXN];
    for (int t = 0; t < T; t++) {
        if (owner[t] == 0 || !cc0[t]) continue;
        const Tri& tr = tris[t];
        double cost = 1e9;
        if (P_PAT) {
            // cheapest occupation set: each corner owned by a unit on it or by units on all its neighbors
            for (size_t si = 0; si < occSets[t].size(); si++) {
                auto& set = occSets[t][si];
                if (si > 0 && P_PAT == 2) {  // surround-based sets only away from the opponent
                    bool near = false;
                    for (int v : set) {
                        if (ocnt[v]) near = true;
                        for (int n : adj[v]) if (ocnt[n]) near = true;
                    }
                    if (near) continue;
                }
                int need[8], nn = 0;
                for (int v : set) {
                    // corners already owned need nothing; other houses need a unit outnumbering the opponent
                    bool corner = v == tr.n[0] || v == tr.n[1] || v == tr.n[2];
                    if (corner ? own[v] == 0 : cnt[v] > ocnt[v]) continue;
                    need[nn++] = v;
                }
                if (nn == 0) { cost = 0; break; }
                for (int i = 0; i < ns; i++) spare[stacks[i]] = cnt[stacks[i]];
                for (int v : set) if (cnt[v] > 0 && cnt[v] > ocnt[v]) spare[v]--;
                int worst = 0, sum = 0;
                bool ok = true;
                for (int i = 0; i < nn && ok; i++) {
                    int c = need[i], bd = 1000, bu = -1;
                    for (int j = 0; j < ns; j++) {
                        int u = stacks[j];
                        if (spare[u] > 0 && D[u][c] < bd) { bd = D[u][c]; bu = u; }
                    }
                    if (bu < 0) { ok = false; break; }
                    spare[bu]--;
                    int d = bd + ocnt[c];
                    worst = max(worst, d);
                    sum += d;
                }
                if (ok) cost = min(cost, worst + 0.25 * sum);
            }
            if (cost == 0 || cost >= 1e9) continue;
        } else {
            int need[3], nn = 0;
            for (int k = 0; k < 3; k++) if (own[tr.n[k]] != 0 && !(P_PURE && pureCorner[t][k])) need[nn++] = tr.n[k];
            if (nn == 0) continue;
            for (int i = 0; i < ns; i++) spare[stacks[i]] = cnt[stacks[i]];
            for (int k = 0; k < 3; k++) { int c = tr.n[k]; if (own[c] == 0 && cnt[c] > 0) spare[c]--; }
            uint64_t available = 0;
            for (int j = 0; j < ns; ++j)
                if (spare[stacks[j]] > 0) available |= 1ULL << stacks[j];
            int worst = 0, sum = 0;
            bool ok = true;
            for (int i = 0; i < nn; i++) {
                int c = need[i];
                int bd;
                int bu = nearestAvailable(c, available, bd);
                if (bu < 0) { ok = false; break; }
                if (--spare[bu] == 0) available &= ~(1ULL << bu);
                int d = bd + ocnt[c];  // need to outnumber opponent units there
                if (P_OUTNUM && ocnt[c] > 0) {
                    // bring ocnt[c] + 1 distinct units: cost is the farthest of them
                    d = bd;
                    for (int extra = 0; extra < ocnt[c] && ok; extra++) {
                        int bd2;
                        int bu2 = nearestAvailable(c, available, bd2);
                        if (bu2 < 0) ok = false;
                        else { if (--spare[bu2] == 0) available &= ~(1ULL << bu2); d = max(d, bd2); }
                    }
                    if (!ok) break;
                    d += 1;  // contested: at least one more turn of fighting
                }
                worst = max(worst, d);
                sum += d;
            }
            if (!ok) continue;
            cost = worst + 0.25 * sum;
        }
        double V = tris[t].owner == 1 - s ? 2.0 : 1.0;
        if (P_RACE && cc1[t] && owner[t] == -1) {
            // how fast could the other side take it? (same greedy assignment, outnumbering my units)
            int worst = 0, sum = 0;
            bool ok = true;
            for (int i = 0; i < ons; i++) ospare[ostacks[i]] = ocnt[ostacks[i]];
            for (int k = 0; k < 3; k++) { int c = tr.n[k]; if (own[c] == 1 && ocnt[c] > 0) ospare[c]--; }
            for (int k = 0; k < 3 && ok; k++) {
                int c = tr.n[k];
                if (own[c] == 1) continue;
                int bd = 1000, bu = -1;
                for (int j = 0; j < ons; j++) {
                    int u = ostacks[j];
                    if (ospare[u] > 0 && D[u][c] < bd) { bd = D[u][c]; bu = u; }
                }
                if (bu < 0) { ok = false; break; }
                ospare[bu]--;
                int d = bd + cnt[c];
                worst = max(worst, d);
                sum += d;
            }
            if (ok) {
                double oc = worst + 0.25 * sum;
                if (oc < cost - 0.5) V *= P_RACE_LOSE;
                else if (oc <= cost + 0.5) V *= P_RACE_TIE;
            }
        }
        pot += V * pow(P_GAMMA, cost);
    }
    score += P_POT * pot;
    lastTerms[2] = P_POT * pot;

    // threats: other side can capture my owned triangle next turn
    int reach[MAXN];
    for (int v = 0; v < N; v++) {
        int r = ocnt[v];
        for (int n : adj[v]) r += ocnt[n];
        reach[v] = r;
    }
    int threatened = 0;
    for (int t = 0; t < T; t++) {
        if (owner[t] != 0) continue;
        const Tri& tr = tris[t];
        bool all = true;
        int needU = 0;
        for (int k = 0; k < 3; k++) {
            int c = tr.n[k];
            if (reach[c] <= cnt[c]) { all = false; break; }
            needU += cnt[c] + 1;
        }
        if (all && needU <= reach[tr.n[0]] + reach[tr.n[1]] + reach[tr.n[2]]) threatened++;
    }
    score -= P_THREAT * threatened;
    lastTerms[3] = -P_THREAT * threatened;

    // influence: who can bring more units onto each house next turn controls it
    if (P_INF != 0) {
        int infl[MAXN];  // > 0 mine, < 0 theirs
        for (int v = 0; v < N; v++) {
            int m = cnt[v], o = ocnt[v];
            for (int n : adj[v]) { m += cnt[n]; o += ocnt[n]; }
            infl[v] = m - o;
        }
        int ctl = 0;
        for (int t = 0; t < T; t++) {
            const Tri& tr = tris[t];
            int a = infl[tr.n[0]], b = infl[tr.n[1]], c = infl[tr.n[2]];
            if (a > 0 && b > 0 && c > 0 && (owner[t] == 0 || cc0[t])) ctl++;
            else if (a < 0 && b < 0 && c < 0 && owner[t] != 1) ctl--;
        }
        score += P_INF * ctl;
    }

    // surround risk next turn: every neighbor could be taken over by the other side
    if (P_SAFE > 0) {
        int myReach[MAXN];
        for (int v = 0; v < N; v++) {
            int r = cnt[v];
            for (int n : adj[v]) r += cnt[n];
            myReach[v] = r;
        }
        double risk = 0;
        for (int v = 0; v < N; v++) {
            if (adj[v].empty()) continue;
            if (cnt[v] > 0) {
                bool all = true;
                for (int n : adj[v]) if (reach[n] <= cnt[n]) { all = false; break; }
                if (all) risk += cnt[v];
            }
            if (ocnt[v] > 0) {
                bool all = true;
                for (int n : adj[v]) if (myReach[n] <= ocnt[n]) { all = false; break; }
                if (all) risk -= ocnt[v];
            }
        }
        score -= P_SAFE * unitValue() * risk;
    }

    // frontier pull: each unit toward nearest corner of a triangle I don't own
    {
        uint64_t front = 0;
        for (int t = 0; t < T; t++) {
            if (owner[t] == 0) continue;
            for (int k = 0; k < 3; k++) front |= 1ULL << tris[t].n[k];
        }
        int tot = 0;
        for (int j = 0; j < ns; j++) {
            int u = stacks[j];
            int best;
            nearestAvailable(u, front, best);
            if (best < 1000) tot += best * cnt[u];
        }
        score -= P_FRONT * tot;
        lastTerms[4] = -P_FRONT * tot;
    }
    if (dbg) cerr << "eval myTri " << myTri << " opTri " << opTri << " units " << myUnits << "/" << opUnits << " pot " << pot << " thr " << threatened << " killed " << fo.killedMe << "/" << fo.killedOp << endl;
    return score;
}


// evaluate plan of side s against the other side's view
double evaluate(int s, const Plan& p, const View& v, bool dbg = false) {
    int cnt[MAXN], ocnt[MAXN];
    static bool used[64];
    int T = tris.size();
    for (int t = 0; t < T; t++) used[t] = v.used[t];
    planCounts(p, cnt, used, v.killed);
    memcpy(ocnt, v.cnt, sizeof(int) * N);
    double atkPenalty = 0;
    if (!p.atks.empty()) {
        bool hit[MAXN] = {};
        for (auto& a : p.atks)
            if (a.on) atkPenalty += P_ATK;
        for (auto& a : p.atks)
            if (a.on && !hit[a.target]) {
                hit[a.target] = true;
                for (int d : v.dests[a.target]) ocnt[d]--;
            }
    }
    static int owner[64];
    static bool cc0[64], cc1[64];
    for (int t = 0; t < T; t++) {
        int o = tris[t].owner;
        owner[t] = o == -1 ? -1 : (o == s ? 0 : 1);
        cc0[t] = tris[t].cc[s];
        cc1[t] = tris[t].cc[1 - s];
        if (used[t]) {
            if (owner[t] == 0) cc0[t] = false;
            else if (owner[t] == 1) cc1[t] = false;
            owner[t] = -1;
        }
    }
    FinalizeOut fo;
    finalizeSim(cnt, ocnt, owner, cc0, cc1, fo);
    // worst case: the other side concentrates every unit within one step (plus spawns) on one house
    double robust = 0;
    if (P_ROB != 0 || P_DENY != 0) {
        const int* rs = reachStart[1 - s];  // other side's reach from its start positions
        for (int t = 0; t < T; t++) {
            const Tri& tr = tris[t];
            if (P_ROB != 0 && owner[t] == 0 && tris[t].owner != s) {
                // captured this turn: could the other side have held one corner?
                bool blockable = false;
                for (int k = 0; k < 3; k++) if (rs[tr.n[k]] >= cnt[tr.n[k]]) blockable = true;
                if (blockable) robust -= P_ROB * P_TRI;
            }
            if (P_DENY != 0 && owner[t] == -1 && cc1[t]) {
                // neutral triangle the other side could take by outnumbering me on all corners
                bool all = true;
                int needU = 0;
                for (int k = 0; k < 3 && all; k++) {
                    int c = tr.n[k];
                    if (rs[c] <= cnt[c]) all = false;
                    needU += cnt[c] + 1;
                }
                if (all && needU <= rs[tr.n[0]] + rs[tr.n[1]] + rs[tr.n[2]]) robust -= P_DENY;
            }
        }
    }
    double cycle = 0;
    if (P_CYCLE != 0)  // used triangles this move makes capturable again (all corners vacated)
        for (int t = 0; t < T; t++)
            if (!tris[t].cc[s] && tris[t].owner != s && cc0[t] && owner[t] != 0) cycle += P_CYCLE;
    return evalTerms(s, cnt, ocnt, owner, cc0, cc1, fo, dbg) - atkPenalty + cycle + robust;
}

void initPlan(int s) {
    Plan& p = plans[s];
    p.nUnits = 0;
    for (int v = 0; v < N; v++)
        for (int k = 0; k < U[s][v] && p.nUnits < MAXU; k++) {
            int i = p.nUnits++;
            p.from[i] = v;
            p.dest[i] = v;
            p.alive[i] = true;
            p.opts[i].clear();
            p.opts[i].push_back(v);
            for (int n : adj[v]) p.opts[i].push_back(n);
        }
    int total = 0;
    for (int v = 0; v < N; v++) total += U[s][v];
    p.spawns.clear();
    for (int t = 0; t < (int)tris.size(); t++) {
        p.spawnOf[t] = -1;
        if (tris[t].owner != s) continue;
        if (turnNo <= SPAWN_END && total + (int)p.spawns.size() < SPAWN_CAP * N) {
            p.spawnOf[t] = p.spawns.size();
            p.spawns.push_back({t, 0});
        }
    }
    p.atks.clear();
    if (LEAGUE >= 2 && (ATK_SIDES >> s & 1)) {
        for (int t = 0; t < (int)tris.size(); t++) {
            const Tri& tr = tris[t];
            if (tr.owner != s || !U[s][tr.n[0]] || !U[s][tr.n[1]] || !U[s][tr.n[2]]) continue;
            bool cand[MAXN] = {};
            for (int k = 0; k < 3; k++) {
                cand[tr.n[k]] = true;
                for (int n : adj[tr.n[k]]) cand[n] = true;
            }
            for (int v = 0; v < N; v++)
                if (cand[v] && U[1 - s][v] > 0) p.atks.push_back({t, v, false, {-1, -1, -1}, 0});
        }
    }
}

// static view of side o: units stay, no spawns, no attacks
void staticView(int o, View& v) {
    for (int i = 0; i < N; i++) {
        v.cnt[i] = U[o][i];
        v.dests[i].assign(U[o][i], i);
        v.killed[i] = false;
    }
    memset(v.used, 0, sizeof v.used);
}
void planViewOf(const Plan& p, View& v) {
    memset(v.used, 0, sizeof v.used);
    planCounts(p, v.cnt, v.used, nullptr);
    for (int i = 0; i < N; i++) { v.dests[i].clear(); v.killed[i] = false; }
    for (int i = 0; i < p.nUnits; i++) if (p.alive[i]) v.dests[p.from[i]].push_back(p.dest[i]);
    for (auto& a : p.atks) if (a.on) v.killed[a.target] = true;
}
void planView(int o, View& v) { planViewOf(plans[o], v); }

bool attackOn(Plan& p, Attack& a) {
    const Tri& tr = tris[a.tri];
    for (int k = 0; k < 3; k++) {
        int c = tr.n[k], pick = -1;
        for (int i = 0; i < p.nUnits; i++)
            if (p.alive[i] && p.from[i] == c) { pick = i; if (p.dest[i] == c) break; }
        if (pick < 0) {
            for (int j = 0; j < k; j++) p.alive[a.cons[j]] = true;
            return false;
        }
        p.alive[pick] = false;
        a.cons[k] = pick;
    }
    int si = p.spawnOf[a.tri];
    if (si >= 0) { a.savedSpawn = p.spawns[si].corner; p.spawns[si].corner = -1; }
    a.on = true;
    return true;
}
void attackOff(Plan& p, Attack& a) {
    for (int k = 0; k < 3; k++) p.alive[a.cons[k]] = true;
    int si = p.spawnOf[a.tri];
    if (si >= 0) p.spawns[si].corner = a.savedSpawn;
    a.on = false;
}

// hill climb side s's plan against a set of weighted views of the other side
void optimize(int s, const vector<pair<double, View*>>& views, double deadlineMs, Clock::time_point t0, int& iters) {
    Plan& p = plans[s];
    auto evalAll = [&]() {
        double e = 0;
        for (auto& [w, v] : views) e += w * evaluate(s, p, *v);
        return e;
    };
    auto elapsed = [&]() { return chrono::duration<double, milli>(Clock::now() - t0).count(); };
    for (auto& sp : p.spawns) {
        double best = -1e18; int bc = 0;
        for (int c = 0; c < 3; c++) { sp.corner = c; double e = evalAll(); if (e > best) { best = e; bc = c; } }
        sp.corner = bc;
    }
    double cur = evalAll();
    int nS = p.spawns.size(), nA = p.atks.size();
    int choices = p.nUnits + nS + nA;
    if (choices == 0) return;
    // best plan seen (annealing may wander below it)
    double best = cur;
    static int bestDest[MAXU];
    static bool bestAlive[MAXU];
    static vector<Spawn> bestSpawns;
    static vector<Attack> bestAtks;
    auto snapshot = [&]() {
        best = cur;
        memcpy(bestDest, p.dest, sizeof(int) * p.nUnits);
        memcpy(bestAlive, p.alive, sizeof(bool) * p.nUnits);
        bestSpawns = p.spawns;
        bestAtks = p.atks;
    };
    snapshot();
    double startMs = elapsed(), span = max(1.0, deadlineMs - startMs);
    double temp = 0;
    auto accept = [&](double e) {
        if (e >= cur) return true;
        if (temp <= 0) return false;
        return exp((e - cur) / temp) > (rng() & 0xFFFF) / 65536.0;
    };
    static int moved[MAXU], oldDest[MAXU];
    while (elapsed() < deadlineMs) {
        temp = SA_T0 * (1.0 - (elapsed() - startMs) / span);
        for (int rep = 0; rep < 8; rep++) {
            iters++;
            int r = rng() % choices;
            if (r < p.nUnits) {
                if (!p.alive[r]) continue;
                int k = 1;
                moved[0] = r;
                if (P_STACK > 0 && (rng() & 0xFFFF) < P_STACK * 65536) {
                    // move a whole stack (or half of it) to one destination
                    bool half = rng() & 1;
                    int nd = p.opts[r][rng() % p.opts[r].size()];
                    k = 0;
                    for (int j = 0; j < p.nUnits; j++)
                        if (p.alive[j] && p.from[j] == p.from[r] && (!half || (j & 1))) moved[k++] = j;
                    if (k < 2) continue;
                    bool change = false;
                    for (int q = 0; q < k; q++) {
                        oldDest[q] = p.dest[moved[q]];
                        p.dest[moved[q]] = nd;
                        change |= nd != oldDest[q];
                    }
                    if (!change) continue;
                    double e = evalAll();
                    if (accept(e)) { cur = e; if (cur > best) snapshot(); }
                    else for (int q = k - 1; q >= 0; q--) p.dest[moved[q]] = oldDest[q];
                    continue;
                }
                // move one unit, or a small group of nearby units together
                if (P_MULTI > 0 && (rng() & 0xFFFF) < P_MULTI * 65536) {
                    int want = 2 + rng() % 2;
                    for (int tries = 0; tries < 12 && k < want; tries++) {
                        int j = rng() % p.nUnits;
                        if (!p.alive[j] || D[p.from[j]][p.from[r]] > 2) continue;
                        bool dup = false;
                        for (int q = 0; q < k; q++) dup |= moved[q] == j;
                        if (!dup) moved[k++] = j;
                    }
                }
                bool change = false;
                for (int q = 0; q < k; q++) {
                    int u = moved[q];
                    oldDest[q] = p.dest[u];
                    p.dest[u] = p.opts[u][rng() % p.opts[u].size()];
                    change |= p.dest[u] != oldDest[q];
                }
                if (!change) continue;
                double e = evalAll();
                if (accept(e)) { cur = e; if (cur > best) snapshot(); }
                else for (int q = k - 1; q >= 0; q--) p.dest[moved[q]] = oldDest[q];
            } else if (r < p.nUnits + nS) {
                auto& sp = p.spawns[r - p.nUnits];
                int old = sp.corner;
                if (old < 0) continue;  // triangle used by an attack
                int nc = rng() % 3;
                if (nc == old) continue;
                sp.corner = nc;
                double e = evalAll();
                if (accept(e)) { cur = e; if (cur > best) snapshot(); } else sp.corner = old;
            } else {
                Attack& a = p.atks[r - p.nUnits - nS];
                if (a.on) {
                    attackOff(p, a);
                    double e = evalAll();
                    if (e >= cur) { cur = e; if (cur > best) snapshot(); } else attackOn(p, a);
                } else {
                    // only one attack per triangle: switch off a sibling first
                    Attack* sib = nullptr;
                    for (auto& b : p.atks) if (b.on && b.tri == a.tri) sib = &b;
                    if (sib) attackOff(p, *sib);
                    if (!attackOn(p, a)) { if (sib) attackOn(p, *sib); continue; }
                    double e = evalAll();
                    if (e >= cur) { cur = e; if (cur > best) snapshot(); }
                    else { attackOff(p, a); if (sib) attackOn(p, *sib); }
                }
            }
        }
    }
    if (best > cur) {
        memcpy(p.dest, bestDest, sizeof(int) * p.nUnits);
        memcpy(p.alive, bestAlive, sizeof(bool) * p.nUnits);
        p.spawns = bestSpawns;
        p.atks = bestAtks;
    }
}

// ---------------- multi-turn planning for my side (side 0)
// Each planned unit has one move choice per turn (index into stay + neighbors, taken modulo).
// Turn 0 uses plans[0]'s attacks and spawn list; later turns spawn on every owned triangle.
// The other side plays its predicted move on turn 0 and then stays.
int DEEP_H = 2;
double DEEP_SPLIT = 0.6;  // share of my budget for the 1-ply search before multi-turn planning
double P_PTS = 1.0;  // weight of points scored during the horizon
double P_DTRI = 2.0;  // value of a triangle owned at the horizon on top of its spawn
double DEEP_SA = 0.0;     // annealing start temperature in the multi-turn search
double DEEP_MULTI = 0.0;  // group move probability in the multi-turn search
double DEEP_MIX = 0.5;  // weight of the 1-ply evaluation of turn 0 inside the multi-turn search
const int DH = 4;
struct Deep {
    int nPlan = 0;              // planned units: alive units of plans[0], then units spawned on turn 0
    int start[MAXU + 64];       // start node (spawned units: their spawn corner, set during eval)
    unsigned char mv[MAXU + 64][DH];
    int spawnCorner[DH][64];    // turns >= 1
} deep;
int deepBase[MAXU];  // deep unit -> plans[0] unit index
int nDeepBase;

inline int stepNode(int pos, int choice) {
    int d = adj[pos].size();
    int c = choice % (d + 1);
    return c == 0 ? pos : adj[pos][c - 1];
}

double deepEval(const View& v, bool dbg = false) {
    const Plan& p = plans[0];
    int T = tris.size();
    int H = DEEP_H;
    static int pos[MAXU + 64];
    static bool alive[MAXU + 64];
    int fixedCnt[MAXN] = {};
    int cnt[MAXN], ocnt[MAXN], ostat[MAXN];
    int owner[64];
    bool cc0[64], cc1[64], used[64];
    for (int t = 0; t < T; t++) used[t] = v.used[t];
    for (auto& sp : p.spawns) if (sp.corner >= 0) used[sp.tri] = true;
    for (auto& a : p.atks) if (a.on) used[a.tri] = true;
    for (int t = 0; t < T; t++) {
        owner[t] = tris[t].owner;
        cc0[t] = tris[t].cc[0];
        cc1[t] = tris[t].cc[1];
        if (used[t]) {
            if (owner[t] == 0) cc0[t] = false;
            else if (owner[t] == 1) cc1[t] = false;
            owner[t] = -1;
        }
    }
    memcpy(ocnt, v.cnt, sizeof(int) * N);
    double atkPenalty = 0;
    {
        bool hit[MAXN] = {};
        for (auto& a : p.atks)
            if (a.on) {
                atkPenalty += P_ATK;
                if (!hit[a.target]) { hit[a.target] = true; for (int d : v.dests[a.target]) ocnt[d]--; }
            }
    }
    for (int i = 0; i < nDeepBase; i++) {
        int b = deepBase[i];
        pos[i] = p.from[b];
        alive[i] = !v.killed[p.from[b]];
    }
    int nPlan = nDeepBase;
    int myTotal = 0;
    for (int i = 0; i < nDeepBase; i++) myTotal += alive[i];
    double points = 0;
    FinalizeOut fo;
    for (int k = 0; k < H; k++) {
        // spawns on triangles owned at the start of this turn (turn 0: given by plans[0])
        int spawnAt[64], ns = 0;
        if (k > 0) {
            for (int t = 0; t < T; t++)
                if (owner[t] == 0 && turnNo + k <= SPAWN_END && myTotal + ns < SPAWN_CAP * N) {
                    spawnAt[ns++] = tris[t].n[deep.spawnCorner[k][t]];
                    owner[t] = -1;
                    cc0[t] = false;
                }
        }
        // moves
        for (int i = 0; i < nPlan; i++) if (alive[i]) pos[i] = stepNode(pos[i], deep.mv[i][k]);
        if (k == 0) {
            for (auto& sp : p.spawns) {
                if (sp.corner < 0) continue;
                pos[nPlan] = tris[sp.tri].n[sp.corner];
                alive[nPlan] = true;
                nPlan++;
                myTotal++;
            }
        }
        for (int j = 0; j < ns; j++) fixedCnt[spawnAt[j]]++;
        myTotal += ns;
        memcpy(cnt, fixedCnt, sizeof(int) * N);
        for (int i = 0; i < nPlan; i++) if (alive[i]) cnt[pos[i]]++;
        if (k > 0) memcpy(ocnt, ostat, sizeof(int) * N);
        int before[MAXN];
        memcpy(before, cnt, sizeof(int) * N);
        finalizeSim(cnt, ocnt, owner, cc0, cc1, fo);
        if (fo.killedMe) {
            for (int x = 0; x < N; x++)
                if (before[x] > 0 && cnt[x] == 0) {
                    fixedCnt[x] = 0;
                    for (int i = 0; i < nPlan; i++) if (alive[i] && pos[i] == x) { alive[i] = false; myTotal--; }
                }
        }
        memcpy(ostat, ocnt, sizeof(int) * N);
        points += fo.myTri - fo.opTri;
    }
    // an owned triangle at the horizon is worth what it turns into: a spawned unit (plus a little)
    double triFix = (P_TRI - (unitValue() + P_DTRI)) * (fo.myTri - fo.opTri);
    return evalTerms(0, cnt, ocnt, owner, cc0, cc1, fo, dbg) - triFix + P_PTS * points - atkPenalty;
}

void deepOptimize(const vector<pair<double, View*>>& views, double deadlineMs, Clock::time_point t0, int& iters) {
    Plan& p = plans[0];
    auto elapsed = [&]() { return chrono::duration<double, milli>(Clock::now() - t0).count(); };
    nDeepBase = 0;
    for (int i = 0; i < p.nUnits; i++) if (p.alive[i]) deepBase[nDeepBase++] = i;
    int nSpawn0 = 0;
    for (auto& sp : p.spawns) if (sp.corner >= 0) nSpawn0++;
    deep.nPlan = nDeepBase + nSpawn0;
    // turn 0 from the 1-ply plan, later turns stay
    for (int i = 0; i < deep.nPlan; i++) for (int k = 0; k < DH; k++) deep.mv[i][k] = 0;
    for (int i = 0; i < nDeepBase; i++) {
        int b = deepBase[i];
        auto& o = p.opts[b];
        for (int c = 0; c < (int)o.size(); c++) if (o[c] == p.dest[b]) deep.mv[i][0] = c;
    }
    for (int k = 0; k < DH; k++) for (int t = 0; t < 64; t++) deep.spawnCorner[k][t] = 0;
    auto score = [&]() {
        if (DEEP_MIX > 0)
            for (int i = 0; i < nDeepBase; i++) { int b = deepBase[i]; p.dest[b] = stepNode(p.from[b], deep.mv[i][0]); }
        double e = 0;
        for (auto& [w, v] : views) {
            e += w * deepEval(*v);
            if (DEEP_MIX > 0) e += w * DEEP_MIX * evaluate(0, p, *v);
        }
        return e;
    };
    double cur = score();
    int nS = p.spawns.size();
    double best = cur;
    static Deep bestDeep;
    static vector<Spawn> bestSpawns;
    auto snapshot = [&]() { best = cur; bestDeep = deep; bestSpawns = p.spawns; };
    snapshot();
    double startMs = elapsed(), span = max(1.0, deadlineMs - startMs);
    double temp = 0;
    auto accept = [&](double e) {
        if (e >= cur) return true;
        if (temp <= 0) return false;
        return exp((e - cur) / temp) > (rng() & 0xFFFF) / 65536.0;
    };
    int moved[3];
    unsigned char oldMv[3];
    while (elapsed() < deadlineMs) {
        temp = DEEP_SA * (1.0 - (elapsed() - startMs) / span);
        for (int rep = 0; rep < 8; rep++) {
            iters++;
            int r = rng() % (deep.nPlan + nS + 4);
            if (r < deep.nPlan) {
                int k = r < nDeepBase ? rng() % DEEP_H : 1 + rng() % (DEEP_H - 1);
                if (k >= DEEP_H) continue;
                int cnt = 1;
                moved[0] = r;
                if (nDeepBase > 0 && DEEP_MULTI > 0 && (rng() & 0xFFFF) < DEEP_MULTI * 65536) {
                    int want = 2 + rng() % 2;
                    for (int tries = 0; tries < 12 && cnt < want; tries++) {
                        int j = rng() % nDeepBase;
                        if (k == 0 && D[p.from[deepBase[j]]][p.from[deepBase[r < nDeepBase ? r : 0]]] > 2) continue;
                        bool dup = false;
                        for (int q = 0; q < cnt; q++) dup |= moved[q] == j;
                        if (!dup) moved[cnt++] = j;
                    }
                }
                bool change = false;
                for (int q = 0; q < cnt; q++) {
                    oldMv[q] = deep.mv[moved[q]][k];
                    deep.mv[moved[q]][k] = rng() % 8;
                    change |= deep.mv[moved[q]][k] != oldMv[q];
                }
                if (!change) continue;
                double e = score();
                if (accept(e)) { cur = e; if (cur > best) snapshot(); }
                else for (int q = cnt - 1; q >= 0; q--) deep.mv[moved[q]][k] = oldMv[q];
            } else if (r < deep.nPlan + nS) {
                auto& sp = p.spawns[r - deep.nPlan];
                int old = sp.corner;
                if (old < 0) continue;
                int nc = rng() % 3;
                if (nc == old) continue;
                sp.corner = nc;
                double e = score();
                if (accept(e)) { cur = e; if (cur > best) snapshot(); } else sp.corner = old;
            } else {
                int k = 1 + rng() % (DEEP_H - 1);
                if (k >= DEEP_H) continue;
                int t = rng() % tris.size();
                int old = deep.spawnCorner[k][t];
                int nc = rng() % 3;
                if (nc == old) continue;
                deep.spawnCorner[k][t] = nc;
                double e = score();
                if (accept(e)) { cur = e; if (cur > best) snapshot(); } else deep.spawnCorner[k][t] = old;
            }
        }
    }
    if (best > cur) { deep = bestDeep; p.spawns = bestSpawns; }
    // write turn 0 back into plans[0]
    for (int i = 0; i < nDeepBase; i++) {
        int b = deepBase[i];
        p.dest[b] = stepNode(p.from[b], deep.mv[i][0]);
    }
}

// ---------------- path actions (ADD_PATH / REMOVE_PATH, league 3)
// Graph edits are permanent: value them by the triangles they create or destroy on each side.
// A triangle belongs to the side whose units are closer to its corners.
vector<pair<int, int>> linkable;
int PATH_ON = 1;
double PATH_MIN = 4.0;   // minimum net triangle gain to spend a triangle on a path action
int PATH_END = 170;      // no path actions after this turn
int PATH_MIN_UNITS = 6;  // keep growing first
struct PathAction { bool add; int tri, from, to; double value; };
PathAction pathAct{false, -1, -1, -1, 0};

inline bool ccwP(int a, int b, int c) { return (Y[c] - Y[a]) * (X[b] - X[a]) > (Y[b] - Y[a]) * (X[c] - X[a]); }
bool insideTri(int n, int a, int b, int c) {
    if (n == a || n == b || n == c) return false;
    bool c1 = ccwP(n, a, b), c2 = ccwP(n, b, c), c3 = ccwP(n, c, a);
    return !((!c1 || !c2 || !c3) && (c1 || c2 || c3));
}

// opponent reply that minimizes my evaluation of my current plan (plans[0])
void optimizeAdversary(double deadlineMs, Clock::time_point t0, int& iters) {
    Plan& q = plans[1];
    static View tv;
    auto f = [&]() { planViewOf(q, tv); return -evaluate(0, plans[0], tv); };
    auto elapsed = [&]() { return chrono::duration<double, milli>(Clock::now() - t0).count(); };
    double cur = f();
    if (q.nUnits == 0) return;
    while (elapsed() < deadlineMs) {
        for (int rep = 0; rep < 8; rep++) {
            iters++;
            int r = rng() % q.nUnits;
            if (!q.alive[r]) continue;
            int old = q.dest[r];
            int nd = q.opts[r][rng() % q.opts[r].size()];
            if (nd == old) continue;
            q.dest[r] = nd;
            double e = f();
            if (e >= cur) cur = e; else q.dest[r] = old;
        }
    }
}

// diagnostics: turn a recorded command line into a plan (moves to first step, spawns)
void applyActual(Plan& q, const string& line) {
    for (int i = 0; i < q.nUnits; i++) q.dest[i] = q.from[i];
    for (auto& sp : q.spawns) sp.corner = -1;
    for (auto& a : q.atks) a.on = false;
    for (int i = 0; i < q.nUnits; i++) q.alive[i] = true;
    regex rm(R"(MOVE (\d+) (\d+) (\d+))"), rs(R"(SPAWN (\d+) (\d+) (\d+))");
    for (sregex_iterator it(line.begin(), line.end(), rm), e; it != e; ++it) {
        int a = stoi((*it)[1]), b = stoi((*it)[2]), k = stoi((*it)[3]);
        for (int i = 0; i < q.nUnits && k > 0; i++)
            if (q.from[i] == a && q.dest[i] == a) {
                int d = b;
                if (find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) {  // multi-step: first step on a shortest path
                    for (int n : adj[a]) if (D[n][b] < D[a][b]) { d = n; break; }
                }
                q.dest[i] = d; k--;
            }
    }
    for (sregex_iterator it(line.begin(), line.end(), rs), e; it != e; ++it) {
        int c = stoi((*it)[1]), b = stoi((*it)[2]), d = stoi((*it)[3]);
        for (auto& sp : q.spawns) {
            const Tri& tr = tris[sp.tri];
            bool match = true;
            for (int x : {c, b, d}) match &= tr.n[0] == x || tr.n[1] == x || tr.n[2] == x;
            if (match) for (int k = 0; k < 3; k++) if (tr.n[k] == c) sp.corner = k;
        }
    }
}

// ---------------- opening search
// Early on the sides are far apart, so my growth depends only on my own moves. On turn 1, beam-search my
// moves for the next OPEN_DEPTH turns (opponent static, spawns forced on every owned triangle), then play
// the plan while the real position matches it and no enemy unit is close.
int OPEN_DEPTH = 0;         // 0 = off
int OPEN_BEAM = 150;
double OPEN_MS = 700;       // turn-1 time for the search
double OPEN_POT = 0.3;      // weight of capture potential in the beam score
int OPEN_SAFE_DIST = 3;     // abandon the plan when enemy units get this close
struct OAction { int parent; vector<pair<int, int>> moves; vector<pair<int, int>> spawns; };
struct OState { int cnt[MAXN]; int owner[64]; bool cc0[64], cc1[64]; double pts, score; int act; };
vector<OAction> openActs;
vector<int> openPlan;               // action index per turn (turn 1 = index 0)
vector<array<int, MAXN>> openExpectCnt;  // my unit counts expected at the start of each planned turn
vector<vector<int>> openExpectOwn;   // my owned triangles expected at the start of each planned turn
bool openActive = false;

void openingSearch(Clock::time_point t0) {
    int T = tris.size();
    if (T > 64) return;
    OState root;
    for (int v = 0; v < N; v++) root.cnt[v] = U[0][v];
    for (int t = 0; t < T; t++) { root.owner[t] = tris[t].owner; root.cc0[t] = tris[t].cc[0]; root.cc1[t] = tris[t].cc[1]; }
    root.pts = 0; root.score = 0; root.act = -1;
    vector<OState> beam(1, root);
    openActs.clear();
    int ocnt0[MAXN];
    for (int v = 0; v < N; v++) ocnt0[v] = U[1][v];
    auto elapsed = [&]() { return chrono::duration<double, milli>(Clock::now() - t0).count(); };
    double start = elapsed();
    for (int depth = 0; depth < OPEN_DEPTH; depth++) {
        double until = start + OPEN_MS * (depth + 1) / OPEN_DEPTH;
        unordered_map<uint64_t, int> seen;
        vector<OState> next;
        int units[MAXU];
        for (long sample = 0; elapsed() < until || sample < (long)beam.size(); sample++) {
            const OState& st = beam[sample % beam.size()];
            OState ns = st;
            OAction act;
            act.parent = st.act;
            // spawns on triangles owned at the start of the turn
            int spawnAt[64], nsp = 0;
            for (int t = 0; t < T; t++)
                if (ns.owner[t] == 0) {
                    int c = rng() % 3;
                    act.spawns.push_back({t, c});
                    spawnAt[nsp++] = tris[t].n[c];
                    ns.owner[t] = -1;
                    ns.cc0[t] = false;
                }
            // random joint move (stay or step to a neighbor)
            int nu = 0;
            for (int v = 0; v < N; v++) for (int k = 0; k < st.cnt[v] && nu < MAXU; k++) units[nu++] = v;
            memset(ns.cnt, 0, sizeof(int) * N);
            for (int i = 0; i < nu; i++) {
                int v = units[i], d = adj[v].size();
                int c = rng() % (d + 1);
                int to = c == 0 ? v : adj[v][c - 1];
                ns.cnt[to]++;
                if (to != v) act.moves.push_back({v, to});
            }
            for (int j = 0; j < nsp; j++) ns.cnt[spawnAt[j]]++;
            int ocnt[MAXN];
            memcpy(ocnt, ocnt0, sizeof(int) * N);
            FinalizeOut fo;
            finalizeSim(ns.cnt, ocnt, ns.owner, ns.cc0, ns.cc1, fo);
            ns.pts += fo.myTri;
            evalTerms(0, ns.cnt, ocnt, ns.owner, ns.cc0, ns.cc1, fo, false);
            int myUnits = 0;
            for (int v = 0; v < N; v++) myUnits += ns.cnt[v];
            ns.score = myUnits + fo.myTri + OPEN_POT * lastTerms[2] / max(1e-9, P_POT) + 0.05 * ns.pts;
            uint64_t h = 1469598103934665603ULL;
            for (int v = 0; v < N; v++) h = (h ^ ns.cnt[v]) * 1099511628211ULL;
            for (int t = 0; t < T; t++) h = (h ^ (ns.owner[t] + 2 + 4 * ns.cc0[t])) * 1099511628211ULL;
            auto it = seen.find(h);
            if (it != seen.end() && next[it->second].score >= ns.score) continue;
            ns.act = openActs.size();
            openActs.push_back(act);
            if (it != seen.end()) next[it->second] = ns;
            else { seen[h] = next.size(); next.push_back(ns); }
        }
        sort(next.begin(), next.end(), [](const OState& a, const OState& b) { return a.score > b.score; });
        if ((int)next.size() > OPEN_BEAM) next.resize(OPEN_BEAM);
        beam = next;
    }
    // best final state: walk back to recover the action sequence
    openPlan.clear();
    for (int a = beam[0].act; a >= 0; a = openActs[a].parent) openPlan.push_back(a);
    reverse(openPlan.begin(), openPlan.end());
    // replay the plan to record the expected positions at the start of each turn
    openExpectCnt.clear();
    openExpectOwn.clear();
    OState st = root;
    int ocnt0b[MAXN];
    for (int v = 0; v < N; v++) ocnt0b[v] = U[1][v];
    for (int a : openPlan) {
        array<int, MAXN> c{};
        for (int v = 0; v < N; v++) c[v] = st.cnt[v];
        openExpectCnt.push_back(c);
        vector<int> own;
        for (int t = 0; t < T; t++) if (st.owner[t] == 0) own.push_back(t);
        openExpectOwn.push_back(own);
        const OAction& act = openActs[a];
        for (auto& [t, c2] : act.spawns) { st.owner[t] = -1; st.cc0[t] = false; }
        for (auto& [f, to] : act.moves) { st.cnt[f]--; st.cnt[to]++; }
        for (auto& [t, c2] : act.spawns) st.cnt[tris[t].n[c2]]++;
        int ocnt[MAXN];
        memcpy(ocnt, ocnt0b, sizeof(int) * N);
        FinalizeOut fo;
        finalizeSim(st.cnt, ocnt, st.owner, st.cc0, st.cc1, fo);
    }
    openActive = !openPlan.empty();
    cerr << "opening plan: " << openPlan.size() << " turns, score " << beam[0].score << ", actions " << openActs.size() << endl;
}

// apply the planned opening move for this turn to plans[0], if the position still matches
bool applyOpening() {
    if (!openActive) return false;
    int k = turnNo - 1;
    if (k >= (int)openPlan.size()) { openActive = false; return false; }
    for (int v = 0; v < N; v++) if (U[0][v] != openExpectCnt[k][v]) { openActive = false; cerr << "opening off: units differ" << endl; return false; }
    vector<int> own;
    for (int t = 0; t < (int)tris.size(); t++) if (tris[t].owner == 0) own.push_back(t);
    if (own != openExpectOwn[k]) { openActive = false; cerr << "opening off: triangles differ" << endl; return false; }
    for (int v = 0; v < N; v++)
        if (U[1][v])
            for (int u = 0; u < N; u++)
                if (U[0][u] && D[u][v] <= OPEN_SAFE_DIST) { openActive = false; cerr << "opening off: enemy close" << endl; return false; }
    Plan& p = plans[0];
    const OAction& act = openActs[openPlan[k]];
    for (int i = 0; i < p.nUnits; i++) { p.dest[i] = p.from[i]; p.alive[i] = true; }
    for (auto& [f, to] : act.moves)
        for (int i = 0; i < p.nUnits; i++)
            if (p.from[i] == f && p.dest[i] == f) { p.dest[i] = to; break; }
    for (auto& sp : p.spawns) sp.corner = -1;
    for (auto& [t, c] : act.spawns) if (p.spawnOf[t] >= 0) p.spawns[p.spawnOf[t]].corner = c;
    for (auto& a : p.atks) a.on = false;
    return true;
}

void choosePathAction() {
    pathAct = {false, -1, -1, -1, 0};
    if (!PATH_ON || linkable.empty() || turnNo > PATH_END) return;
    int myTotal = 0;
    for (int v = 0; v < N; v++) myTotal += U[0][v];
    if (myTotal < PATH_MIN_UNITS) return;
    // distance from each side's units to each house
    int dist[2][MAXN];
    for (int side = 0; side < 2; side++)
        for (int v = 0; v < N; v++) {
            int b = 1000;
            for (int u = 0; u < N; u++) if (U[side][u]) b = min(b, D[u][v]);
            dist[side][v] = b;
        }
    auto side = [&](int a, int b, int c) {  // +1 mine, -1 theirs, 0 contested
        int m = dist[0][a] + dist[0][b] + dist[0][c], o = dist[1][a] + dist[1][b] + dist[1][c];
        return m < o ? 1 : (o < m ? -1 : 0);
    };
    bool isAdj[MAXN][MAXN] = {};
    for (int v = 0; v < N; v++) for (int n : adj[v]) isAdj[v][n] = true;
    PathAction best{false, -1, -1, -1, PATH_MIN - 1e-9};
    for (int t = 0; t < (int)tris.size(); t++) {
        const Tri& tr = tris[t];
        if (tr.owner != 0 || !U[0][tr.n[0]] || !U[0][tr.n[1]] || !U[0][tr.n[2]]) continue;
        for (int k = 0; k < 3; k++) {
            int from = tr.n[k];
            // ADD_PATH from -> to: new triangles (from, to, x) with x adjacent to both and no house inside
            for (auto& [a, b] : linkable) {
                if (a != from && b != from) continue;
                int to = a == from ? b : a;
                double v = 0;
                for (int x = 0; x < N; x++) {
                    if (!isAdj[from][x] || !isAdj[to][x]) continue;
                    bool empty = true;
                    for (int n = 0; n < N && empty; n++) empty = !insideTri(n, from, to, x);
                    if (empty) v += side(from, to, x);
                }
                if (v > best.value) best = {true, t, from, to, v};
            }
            // REMOVE_PATH from -> to: destroys every triangle using that edge
            for (int to : adj[from]) {
                if (tr.n[0] == to || tr.n[1] == to || tr.n[2] == to) continue;
                double v = 0;
                for (auto& o : tris)
                    if ((o.n[0] == from || o.n[1] == from || o.n[2] == from) && (o.n[0] == to || o.n[1] == to || o.n[2] == to))
                        v -= side(o.n[0], o.n[1], o.n[2]);
                if (v > best.value) best = {false, t, from, to, v};
            }
        }
    }
    if (best.tri < 0) return;
    // commit: one unit per corner is used up, and the triangle cannot spawn or attack this turn
    Plan& p = plans[0];
    const Tri& tr = tris[best.tri];
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < p.nUnits; i++)
            if (p.alive[i] && p.from[i] == tr.n[k]) { p.alive[i] = false; break; }
    int si = p.spawnOf[best.tri];
    if (si >= 0) p.spawns[si].corner = -1;
    vector<Attack> keep;
    for (auto& a : p.atks) if (a.tri != best.tri) keep.push_back(a);
    p.atks = keep;
    pathAct = best;
}

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        string k = argv[i];
        double v = atof(argv[i + 1]);
        if (k == "tri") P_TRI = v;
        else if (k == "unit") P_UNIT = v;
        else if (k == "pot") P_POT = v;
        else if (k == "gamma") P_GAMMA = v;
        else if (k == "threat") P_THREAT = v;
        else if (k == "front") P_FRONT = v;
        else if (k == "spawnend") SPAWN_END = (int)v;
        else if (k == "time") TIME_BUDGET = v;
        else if (k == "cap") SPAWN_CAP = v;
        else if (k == "predw") PRED_W = v;
        else if (k == "predfrac") PRED_FRAC = v;
        else if (k == "league") LEAGUE = (int)v;
        else if (k == "atk") ATK_SIDES = (int)v;
        else if (k == "atkcost") P_ATK = v;
        else if (k == "iter") PRED_ITER = (int)v;
        else if (k == "pure") P_PURE = (int)v;
        else if (k == "pat") P_PAT = (int)v;
        else if (k == "cycle") P_CYCLE = v;
        else if (k == "stack") P_STACK = v;
        else if (k == "outnum") P_OUTNUM = (int)v;
        else if (k == "inf") P_INF = v;
        else if (k == "rob") P_ROB = v;
        else if (k == "race") P_RACE = (int)v;
        else if (k == "robust") ROBUST = (int)v;
        else if (k == "open") OPEN_DEPTH = (int)v;
        else if (k == "openbeam") OPEN_BEAM = (int)v;
        else if (k == "openms") OPEN_MS = v;
        else if (k == "openpot") OPEN_POT = v;
        else if (k == "opensafe") OPEN_SAFE_DIST = (int)v;
        else if (k == "rws") RW_STATIC = v;
        else if (k == "rwp") RW_PRED = v;
        else if (k == "rwb") RW_BR = v;
        else if (k == "rwa") RW_ADV = v;
        else if (k == "racelose") P_RACE_LOSE = v;
        else if (k == "racetie") P_RACE_TIE = v;
        else if (k == "deny") P_DENY = v;
        else if (k == "path") PATH_ON = (int)v;
        else if (k == "pathmin") PATH_MIN = v;
        else if (k == "pathend") PATH_END = (int)v;
        else if (k == "pathunits") PATH_MIN_UNITS = (int)v;
        else if (k == "sa") SA_T0 = v;
        else if (k == "node") P_NODE = v;
        else if (k == "multi") P_MULTI = v;
        else if (k == "deep") DEEP_H = min(DH, (int)v);
        else if (k == "deepsplit") DEEP_SPLIT = v;
        else if (k == "pts") P_PTS = v;
        else if (k == "dtri") P_DTRI = v;
        else if (k == "mix") DEEP_MIX = v;
        else if (k == "dsa") DEEP_SA = v;
        else if (k == "dmulti") DEEP_MULTI = v;
        else if (k == "safe") P_SAFE = v;
    }
    cin >> N;
    for (int i = 0; i < N; i++) { int id; cin >> id >> X[i] >> Y[i]; }
    while (true) {
        if (!(cin >> myScore >> oppScore)) return 0;
        auto t0 = Clock::now();
        turnNo++;
        for (int i = 0; i < N; i++) { int id; cin >> id; cin >> U[0][id] >> U[1][id]; }
        for (int i = 0; i < N; i++) adj[i].clear();
        int pc; cin >> pc;
        for (int i = 0; i < pc; i++) {
            int a, b; cin >> a >> b;
            adj[a].push_back(b); adj[b].push_back(a);
        }
        int tc; cin >> tc;
        tris.assign(tc, Tri());
        for (int i = 0; i < tc; i++) {
            int a, b, c, o, m, q; cin >> a >> b >> c >> o >> m >> q;
            tris[i] = {{a, b, c}, o, {m != 0, q != 0}};
        }
        pureCorner.assign(tc, {false, false, false});
        for (int i = 0; i < tc; i++)
            for (int k = 0; k < 3; k++) {
                int c = tris[i].n[k];
                bool pure = !adj[c].empty();
                for (int n : adj[c]) if (n != tris[i].n[(k + 1) % 3] && n != tris[i].n[(k + 2) % 3]) pure = false;
                pureCorner[i][k] = pure;
            }
        occSets.assign(tc, {});
        for (int i = 0; i < tc; i++) {
            for (int mask = 0; mask < 8; mask++) {  // bit k: corner k owned by surrounding it
                vector<int> set;
                auto add = [&](int v) { if (find(set.begin(), set.end(), v) == set.end()) set.push_back(v); };
                for (int k = 0; k < 3; k++) {
                    int c = tris[i].n[k];
                    if (mask >> k & 1) { if (adj[c].empty()) { set.assign(99, 0); break; } for (int n : adj[c]) add(n); }
                    else add(c);
                }
                if (set.size() > 5) continue;
                bool dominated = false;  // skip supersets of an existing set
                for (auto& o : occSets[i]) {
                    bool sub = true;
                    for (int v : o) if (find(set.begin(), set.end(), v) == set.end()) sub = false;
                    if (sub) dominated = true;
                }
                if (!dominated) occSets[i].push_back(set);
            }
        }
        int lc; cin >> lc;
        linkable.clear();
        for (int i = 0; i < lc; i++) { int a, b; cin >> a >> b; linkable.push_back({a, b}); }

        for (int s = 0; s < N; s++) {
            for (int v = 0; v < N; v++) D[s][v] = 1000;
            D[s][s] = 0;
            int q[MAXN], qh = 0, qt = 0;
            q[qt++] = s;
            while (qh < qt) {
                int c = q[qh++];
                for (int n : adj[c]) if (D[s][n] == 1000) { D[s][n] = D[s][c] + 1; q[qt++] = n; }
            }
        }

        prepareDistanceMasks();
        for (int side = 0; side < 2; side++) {
            bool spawnable[MAXN] = {};
            for (auto& tr : tris) if (tr.owner == side) for (int k = 0; k < 3; k++) spawnable[tr.n[k]] = true;
            for (int v = 0; v < N; v++) {
                int r = U[side][v] + spawnable[v];
                for (int n : adj[v]) r += U[side][n];
                reachStart[side][v] = r;
            }
        }
        initPlan(0);
        initPlan(1);
        if (turnNo == 1 && OPEN_DEPTH > 0) openingSearch(t0);
        choosePathAction();
        double budget = turnNo == 1 ? (OPEN_DEPTH > 0 ? OPEN_MS + TIME_BUDGET * 2 : TIME_BUDGET * 5) : TIME_BUDGET;
        int iters = 0, oppIters = 0;
        static View staticOpp, predOpp, staticMe;
        staticView(1, staticOpp);
        vector<pair<double, View*>> views;
        if (PRED_W > 0) {
            staticView(0, staticMe);
            optimize(1, {{1.0, &staticMe}}, budget * (ROBUST ? 0.25 : PRED_FRAC), t0, oppIters);
            planView(1, predOpp);
            if (getenv("ACTUAL_FILE")) {
                // diagnostic: score the opponent's real move with my evaluation, vs the move my search found for it
                static vector<string> actual;
                if (actual.empty()) { ifstream f(getenv("ACTUAL_FILE")); string l; while (getline(f, l)) actual.push_back(l); }
                if (turnNo <= (int)actual.size()) {
                    Plan saved = plans[1];
                    double ePred = evaluate(1, plans[1], staticMe);
                    double tPred[5]; memcpy(tPred, lastTerms, sizeof tPred);
                    Plan& q = plans[1];
                    applyActual(q, actual[turnNo - 1]);
                    View* against = &staticMe;
                    static View realMe;
                    if (getenv("ACTUAL_ME_FILE")) {
                        static vector<string> mine;
                        if (mine.empty()) { ifstream f(getenv("ACTUAL_ME_FILE")); string l; while (getline(f, l)) mine.push_back(l); }
                        Plan saved0 = plans[0];
                        applyActual(plans[0], mine[turnNo - 1]);
                        planView(0, realMe);
                        plans[0] = saved0;
                        against = &realMe;
                        Plan predQ = saved;
                        ePred = evaluate(1, predQ, realMe);
                        memcpy(tPred, lastTerms, sizeof tPred);
                    }
                    double eAct = evaluate(1, q, *against);
                    cerr << "DIAG " << turnNo << " pred " << ePred << " actual " << eAct << " terms";
                    for (int k = 0; k < 5; k++) cerr << " " << lastTerms[k] - tPred[k];
                    cerr << endl;
                    plans[1] = saved;
                }
            }
            if (PRED_W < 1) views.push_back({1 - PRED_W, &staticOpp});
            views.push_back({PRED_W, &predOpp});
        } else {
            views.push_back({1.0, &staticOpp});
        }
        if (PRED_ITER && PRED_W >= 1) {
            static View myView;
            optimize(0, views, budget * 0.6, t0, iters);
            planView(0, myView);
            if (PRED_ITER == 2) optimize(1, {{1.0, &myView}}, budget * 0.75, t0, oppIters);
            else optimize(1, {{0.5, &staticMe}, {0.5, &myView}}, budget * 0.75, t0, oppIters);
            planView(1, predOpp);
        }
        if (ROBUST && PRED_W > 0) {
            // robust planning: my first plan, then the opponent's best response and an adversarial reply to it;
            // my final move is optimized against a weighted mix of static / predicted / best-response / adversarial
            static View myView, brOpp, advOpp;
            Plan predPlan = plans[1];
            optimize(0, views, budget * 0.4, t0, iters);
            planView(0, myView);
            if (RW_BR > 0) {
                optimize(1, {{1.0, &myView}}, budget * 0.5, t0, oppIters);
                planView(1, brOpp);
                plans[1] = predPlan;
            }
            if (RW_ADV > 0) {
                optimizeAdversary(budget * 0.6, t0, oppIters);
                planView(1, advOpp);
            }
            plans[1] = predPlan;
            views.clear();
            double tot = RW_STATIC + RW_PRED + RW_BR + RW_ADV;
            if (RW_STATIC > 0) views.push_back({RW_STATIC / tot, &staticOpp});
            if (RW_BR > 0) views.push_back({RW_BR / tot, &brOpp});
            if (RW_ADV > 0) views.push_back({RW_ADV / tot, &advOpp});
            views.push_back({RW_PRED / tot, &predOpp});  // last = main view (debug print)
        }
        if (getenv("PRED_DUMP")) {  // predicted opponent moves, for measuring prediction quality
            map<pair<int, int>, int> mv;
            for (int i = 0; i < plans[1].nUnits; i++) if (plans[1].alive[i] && plans[1].dest[i] != plans[1].from[i]) mv[{plans[1].from[i], plans[1].dest[i]}]++;
            cerr << "PRED " << turnNo;
            for (auto& [k, c] : mv) cerr << " " << k.first << ">" << k.second << "x" << c;
            cerr << endl;
        }
        if (DEEP_H >= 2) {
            optimize(0, views, budget * (ROBUST ? 0.75 : DEEP_SPLIT), t0, iters);
            deepOptimize(views, budget, t0, iters);
        } else {
            optimize(0, views, budget, t0, iters);
        }
        evaluate(0, plans[0], views.back().second[0], true);
        if (getenv("HS_OPP") && getenv("HS_ME")) {
            // hindsight metric: my chosen move vs the recorded move, both scored against the opponent's real move
            static vector<string> opp, me;
            if (opp.empty()) {
                ifstream f(getenv("HS_OPP")); string l; while (getline(f, l)) opp.push_back(l);
                ifstream g(getenv("HS_ME")); while (getline(g, l)) me.push_back(l);
            }
            if (turnNo <= (int)opp.size() && turnNo <= (int)me.size()) {
                static View realOpp;
                Plan q = plans[1];
                initPlan(1);
                applyActual(plans[1], opp[turnNo - 1]);
                planViewOf(plans[1], realOpp);
                plans[1] = q;
                Plan mine = plans[0];
                double eMine = evaluate(0, mine, realOpp);
                Plan rec = plans[0];
                // the recorded move needs a fresh plan (all units alive, spawn list as the rules force)
                Plan keep = plans[0];
                initPlan(0);
                applyActual(plans[0], me[turnNo - 1]);
                double eRec = evaluate(0, plans[0], realOpp);
                plans[0] = keep;
                cerr << "HS " << turnNo << " " << eMine << " " << eRec << endl;
            }
        }

        if (applyOpening()) { pathAct.tri = -1; cerr << "opening move" << endl; }
        Plan& p = plans[0];
        map<pair<int, int>, int> mv;
        for (int i = 0; i < p.nUnits; i++) if (p.alive[i] && p.dest[i] != p.from[i]) mv[{p.from[i], p.dest[i]}]++;
        string out;
        for (auto& a : p.atks) {
            if (!a.on) continue;
            const Tri& tr = tris[a.tri];
            out += "ATTACK " + to_string(tr.n[0]) + " " + to_string(tr.n[1]) + " " + to_string(tr.n[2]) + " " + to_string(a.target) + ";";
        }
        if (pathAct.tri >= 0) {
            const Tri& tr = tris[pathAct.tri];
            int o1 = -1, o2 = -1;
            for (int k = 0; k < 3; k++) if (tr.n[k] != pathAct.from) { if (o1 < 0) o1 = tr.n[k]; else o2 = tr.n[k]; }
            out += string(pathAct.add ? "ADD_PATH " : "REMOVE_PATH ") + to_string(pathAct.from) + " " + to_string(o1) + " " + to_string(o2) + " " + to_string(pathAct.to) + ";";
            cerr << "path " << (pathAct.add ? "add" : "remove") << " value " << pathAct.value << endl;
        }
        for (auto& [k, c] : mv) out += "MOVE " + to_string(k.first) + " " + to_string(k.second) + " " + to_string(c) + ";";
        for (auto& sp : p.spawns) {
            if (sp.corner < 0) continue;
            const Tri& tr = tris[sp.tri];
            int a = tr.n[sp.corner], b = tr.n[(sp.corner + 1) % 3], c = tr.n[(sp.corner + 2) % 3];
            out += "SPAWN " + to_string(a) + " " + to_string(b) + " " + to_string(c) + ";";
        }
        if (out.empty()) out = "WAIT";
        else out.pop_back();
        double el = chrono::duration<double, milli>(Clock::now() - t0).count();
        cerr << "turn " << turnNo << " iters " << iters << "/" << oppIters << " t " << el << endl;
        cout << out << endl;
    }
}

