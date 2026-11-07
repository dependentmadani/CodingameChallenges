#pragma GCC optimize("Ofast,inline,omit-frame-pointer")  // no unroll-loops: it costs ~30% compile time on CodinGame for ~2% speed
#pragma GCC target("avx2,bmi,bmi2,lzcnt,popcnt")
// Fall Challenge 2026 (TryAngle-Catch) bot
#include <bits/stdc++.h>
using namespace std;
using Clock = chrono::steady_clock;

static const int MAXN = 50;
static const int MAXU = 200;
static const int MAXT = 128;  // triangles (paths can add triangles in higher leagues)

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
uint64_t NBM[MAXN];   // neighbor mask per house
int adjS[MAXN + 1], adjL[MAXN * MAXN];  // flat adjacency: neighbors of v are adjL[adjS[v] .. adjS[v+1])
uint64_t TRIM[MAXT];    // corner mask per triangle
extern double P_GAMMA;
double POW4[1024];    // P_GAMMA^(i/4), for potential costs (multiples of 0.25)
inline double powGamma(double cost) { int i = (int)(cost * 4 + 0.5); return i < 1024 && i >= 0 ? POW4[i] : pow(P_GAMMA, cost); }
vector<array<bool, 3>> pureCorner;
vector<vector<vector<int>>> occSets;  // per triangle: house sets whose occupation owns all three corners  // corner whose neighbors are only the other two corners (owned by surround for free)

mt19937 rng(12345);

// ---------------- parameters (overridable via argv for tuning)
double P_TRI = 19.66;       // owned triangle
double P_UNIT = 9.8012;       // unit (scaled by game phase)
double P_POT = 7.8352;        // capture potential
double P_GAMMA = 0.68429;      // potential decay per distance
double P_THREAT = 0.96904;     // my triangle threatened
double P_FRONT = 0.0093967;     // frontier pull per distance
int SPAWN_END = 150;       // stop routine spawning after this turn
double SPAWN_CAP = 1.0;    // routine spawning while my units < SPAWN_CAP * N
double TIME_BUDGET = 40.0; // ms; peaks ~2.7 ms above budget on CodinGame, limit is 50 ms
double PRED_W = 1.156;       // weight of evaluation against predicted opponent moves
double PRED_FRAC = 0.20437;    // share of the time budget spent predicting the opponent

struct Spawn { int tri; int corner; };
struct Attack { int tri, target; bool on; int cons[3]; int savedSpawn; };
struct Plan {
    int nUnits = 0;
    int from[MAXU];
    vector<int> opts[MAXU];
    int dest[MAXU];
    bool alive[MAXU];
    vector<Spawn> spawns;
    int spawnOf[MAXT];  // index into spawns per triangle, -1 if none
    vector<Attack> atks;
};
Plan plans[2];

// what one side does, as seen by the other side's evaluation
struct View {
    int cnt[MAXN];            // unit counts after moves/spawns
    bool used[MAXT];            // triangles used (spawn/attack) this turn
    vector<int> dests[MAXN];  // per start node: destinations of its units (to apply kills)
    bool killed[MAXN];        // nodes where the other side's units get killed by attacks
};
int LEAGUE = 2;
int ATK_SIDES = 3;     // bit 0: my attacks, bit 1: predicted opponent attacks
double P_ATK = 5.0;  // extra cost per attack
int PRED_ITER = 0;     // re-predict the opponent against my first plan
int P_PURE = 1;
int P_PAT = 0;         // occupation-set capture potential
double P_BLOCK = 0.93246;  // penalty per locked used triangle, scaled by how many of its corners my units occupy
int P_CYCLE_RACE = 1;          // scale the cycle bonus by who wins the race back to the triangle
double P_CYCLE_LOSE = 0.4998;     // ... share kept when the other side gets there first
double P_CYCLE_TIE = 0.18332;      // ... and on a tie
int raceState[MAXT];             // per triangle, from the last evalTerms: 0 I'm first, 1 tie, 2 other side first
double P_CYCLE_UNSAFE = 0.67579;  // share of the cycle bonus when the other side can reach every corner
double P_CYCLE = 6.4452;  // bonus per used triangle made capturable again
double P_STACK = 0.34291;  // probability of a whole-stack move mutation
int P_OUTNUM = 0;      // potential needs enough units to outnumber the opponent on a corner
double P_INF = 2.0363;    // triangles controlled by influence (units within one step)
int ROBUST = 1;            // plan against several opponent replies
int NASH = 0;              // replace the fixed reply weights by the equilibrium of a small matrix game
int NASH_ROUNDS = 2;       // rounds of (my best plan, opponent best reply) added to the matrix
double NASH_STEP = 0.1;    // share of the budget for each search in a round
int NASH_SYM = 1;          // payoff = my eval - opponent eval (else my eval only)
int NASH_STATIC = 1;       // the "opponent stays" plan is a candidate
double NASH_MIX = 1.0;     // final opponent mix = NASH_MIX * equilibrium + (1 - NASH_MIX) * predicted move
double RW_STATIC = 0, RW_PRED = 1.9463, RW_BR = 1.4172, RW_ADV = 0.3912;  // their weights
int P_RACE = 0;            // weigh capture potential by who reaches the triangle first
double P_RACE_LOSE = 0.2;  // potential share kept for triangles the other side reaches first
double P_RACE_TIE = 0.6;   // ... and for ties
double P_ROB = 0.011267;    // discount (share of a triangle) for captures the other side could have blocked
double P_DENY = 0.17945;   // penalty per neutral triangle the other side could take in the worst case
int reachStart[2][MAXN];
double lastTerms[5];  // last evaluation's main terms: triangles, units, potential, threat, frontier (diagnostics)  // units a side can bring onto each house next turn from its start positions
double P_NODE = 0.20632;   // owned houses (incl. surround-owned)
double SA_T0 = 1.0;    // annealing start temperature (0 = hill climbing)
double P_MULTI = 0.53321;  // probability of moving a small group of nearby units at once
double P_CHAIN = 0.0;  // probability of a chain mutation: a unit steps to a neighbor, a unit there steps on, and so on
int CHAIN_MAX = 4;     // longest chain
double P_SAFE = 0;   // surround risk weight (in units)

// ---------------- opening-phase weights
// While the sides are far apart the game is a growth race, so the evaluation can use its own weights.
// early[i] < -1e8 means "same as the normal weight".
int EARLY_T = 12;       // use the early weights up to this turn (0 = off)
int EARLY_DIST = 3;     // ... and only while no enemy unit is this close to one of mine
struct EarlyParam { const char* name; double* p; double early; double normal; };
vector<EarlyParam> earlyParams;
void initEarlyParams() {
    // tuned on "units at turns 5-7 against a do-nothing opponent"
    earlyParams = {{"e_tri", &P_TRI, 114.5, 0}, {"e_unit", &P_UNIT, 21.05, 0}, {"e_pot", &P_POT, 23.62, 0},
                   {"e_gamma", &P_GAMMA, 0.5899, 0}, {"e_cycle", &P_CYCLE, 6.061, 0}, {"e_node", &P_NODE, 0.9572, 0},
                   {"e_inf", &P_INF, 0.0737, 0}, {"e_block", &P_BLOCK, 1.684, 0}, {"e_front", &P_FRONT, 0.005674, 0},
                   {"e_threat", &P_THREAT, 0.1797, 0}};
}
bool earlyOn = false;
void setEarly(bool on) {
    if (on == earlyOn) return;
    for (auto& e : earlyParams) {
        if (e.early < -1e8) continue;
        if (on) { e.normal = *e.p; *e.p = e.early; } else *e.p = e.normal;
    }
    earlyOn = on;
    for (int i = 0; i < 1024; i++) POW4[i] = pow(P_GAMMA, i * 0.25);
}

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
#ifdef CHECK_FAST
void finalizeSimRef(int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1, FinalizeOut& fo) {
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
#endif

// Same result as finalizeSimRef, with bit masks over houses.
void finalizeSimFast(int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1, FinalizeOut& fo) {
    int T = tris.size();
    uint64_t occMe = 0, occOp = 0, advMe = 0, advOp = 0;
    for (int v = 0; v < N; v++) {
        uint64_t b = 1ULL << v;
        if (cnt[v] > 0) occMe |= b;
        if (ocnt[v] > 0) occOp |= b;
        if (cnt[v] > ocnt[v]) advMe |= b;
        else if (ocnt[v] > cnt[v]) advOp |= b;
    }
    for (int t = 0; t < T; t++) {
        if (owner[t] != -1) { cc0[t] = owner[t] != 0; cc1[t] = owner[t] != 1; }
        else {
            cc0[t] = cc0[t] || !(TRIM[t] & occMe);
            cc1[t] = cc1[t] || !(TRIM[t] & occOp);
        }
    }
    int killedMe = 0, killedOp = 0;
    for (uint64_t m = occMe | occOp; m; m &= m - 1) {
        int v = __builtin_ctzll(m);
        uint64_t nb = NBM[v];
        if (!nb) continue;
        if (cnt[v] > 0 && !(nb & ~advOp)) { killedMe += cnt[v]; cnt[v] = 0; }
        if (ocnt[v] > 0 && !(nb & ~advMe)) { killedOp += ocnt[v]; ocnt[v] = 0; }
    }
    int* own = fo.own;
    if (killedMe || killedOp) {
        occMe = occOp = advMe = advOp = 0;
        for (int v = 0; v < N; v++) {
            uint64_t b = 1ULL << v;
            if (cnt[v] > 0) occMe |= b;
            if (ocnt[v] > 0) occOp |= b;
            if (cnt[v] > ocnt[v]) advMe |= b;
            else if (ocnt[v] > cnt[v]) advOp |= b;
        }
        for (int t = 0; t < T; t++) if (owner[t] == -1) {
            cc0[t] = cc0[t] || !(TRIM[t] & occMe);
            cc1[t] = cc1[t] || !(TRIM[t] & occOp);
        }
    }
    uint64_t ownMe = 0, ownOp = 0;
    for (int v = 0; v < N; v++) {
        uint64_t nb = NBM[v], b = 1ULL << v;
        int o;
        if (nb && !(nb & ~advMe)) o = 0;
        else if (nb && !(nb & ~advOp)) o = 1;
        else o = (advMe & b) ? 0 : (advOp & b) ? 1 : -1;
        own[v] = o;
        if (o == 0) ownMe |= b; else if (o == 1) ownOp |= b;
    }
    int myTri = 0, opTri = 0;
    for (int t = 0; t < T; t++) {
        uint64_t tm = TRIM[t];
        if (cc0[t] && !(tm & ~ownMe)) owner[t] = 0;
        if (cc1[t] && !(tm & ~ownOp)) owner[t] = 1;
        if (owner[t] == 0) myTri++;
        else if (owner[t] == 1) opTri++;
        if (owner[t] != -1) { cc0[t] = owner[t] != 0; cc1[t] = owner[t] != 1; }
    }
    fo.myTri = myTri; fo.opTri = opTri; fo.killedMe = killedMe; fo.killedOp = killedOp;
}

#ifdef CHECK_FAST
void finalizeSim(int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1, FinalizeOut& fo) {
    int c1[MAXN], o1[MAXN], w1[MAXT]; bool a1[MAXT], b1[MAXT]; FinalizeOut f1;
    int T = tris.size();
    memcpy(c1, cnt, sizeof(int) * N); memcpy(o1, ocnt, sizeof(int) * N);
    memcpy(w1, owner, sizeof(int) * T); memcpy(a1, cc0, T); memcpy(b1, cc1, T);
    finalizeSimRef(c1, o1, w1, a1, b1, f1);
    finalizeSimFast(cnt, ocnt, owner, cc0, cc1, fo);
    bool same = f1.myTri == fo.myTri && f1.opTri == fo.opTri && f1.killedMe == fo.killedMe && f1.killedOp == fo.killedOp &&
                !memcmp(c1, cnt, sizeof(int) * N) && !memcmp(o1, ocnt, sizeof(int) * N) && !memcmp(w1, owner, sizeof(int) * T) &&
                !memcmp(a1, cc0, T) && !memcmp(b1, cc1, T) && !memcmp(f1.own, fo.own, sizeof(int) * N);
    if (!same) { cerr << "FAST FINALIZE MISMATCH" << endl; abort(); }
}
#else
inline void finalizeSim(int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1, FinalizeOut& fo) { finalizeSimFast(cnt, ocnt, owner, cc0, cc1, fo); }
#endif

// static evaluation terms of a finalized position for side s
#ifdef CHECK_FAST
double evalTermsRef(int s, const int* cnt, const int* ocnt, const int* owner, const bool* cc0, const bool* cc1, const FinalizeOut& fo, bool dbg) {
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
    memset(raceState, 0, sizeof raceState);
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
        if ((P_RACE || P_CYCLE_RACE) && cc1[t] && owner[t] == -1) {
            // how fast could the other side take it? (same greedy assignment, outnumbering my units)
            int worst = 0, sum = 0;
            bool ok = true;
            for (int i = 0; i < ons; i++) ospare[ostacks[i]] = ocnt[ostacks[i]];
            for (int k = 0; k < 3; k++) { int c = tr.n[k]; if (own[c] == 1 && ocnt[c] > 0) ospare[c]--; }
            uint64_t oavail = 0;
            for (int j = 0; j < ons; j++) if (ospare[ostacks[j]] > 0) oavail |= 1ULL << ostacks[j];
            for (int k = 0; k < 3 && ok; k++) {
                int c = tr.n[k];
                if (own[c] == 1) continue;
                int bd;
                int bu = nearestAvailable(c, oavail, bd);
                if (bu < 0) { ok = false; break; }
                if (--ospare[bu] == 0) oavail &= ~(1ULL << bu);
                int d = bd + cnt[c];
                worst = max(worst, d);
                sum += d;
            }
            if (ok) {
                double oc = worst + 0.25 * sum;
                raceState[t] = oc < cost - 0.5 ? 2 : (oc <= cost + 0.5 ? 1 : 0);
                if (P_RACE) {
                    if (raceState[t] == 2) V *= P_RACE_LOSE;
                    else if (raceState[t] == 1) V *= P_RACE_TIE;
                }
            }
        }
        pot += V * powGamma(cost);
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
#endif

#ifdef CHECK_FAST
double evalTermsFast(int s, const int* cnt, const int* ocnt, const int* owner, const bool* cc0, const bool* cc1, const FinalizeOut& fo, bool dbg);
double evalTerms(int s, const int* cnt, const int* ocnt, const int* owner, const bool* cc0, const bool* cc1, const FinalizeOut& fo, bool dbg) {
    double a = evalTermsRef(s, cnt, ocnt, owner, cc0, cc1, fo, false);
    int rs[MAXT]; memcpy(rs, raceState, sizeof rs);
    double b = evalTermsFast(s, cnt, ocnt, owner, cc0, cc1, fo, dbg);
    if (a != b || memcmp(rs, raceState, sizeof rs)) { cerr << "FAST EVAL MISMATCH " << a << " " << b << endl; abort(); }
    return b;
}
#define evalTerms evalTermsFast
#endif
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
    memset(raceState, 0, sizeof raceState);
    int ostacks[MAXN], ons = 0, ospare[MAXN];
    for (int v = 0; v < N; v++) if (ocnt[v] > 0) ostacks[ons++] = v;
    int spare[MAXN];
    // spare / ospare hold the unit counts between triangles; each triangle undoes its changes
    for (int i = 0; i < ns; i++) spare[stacks[i]] = cnt[stacks[i]];
    for (int i = 0; i < ons; i++) ospare[ostacks[i]] = ocnt[ostacks[i]];
    uint64_t availAll = 0, oavailAll = 0;
    for (int i = 0; i < ns; i++) availAll |= 1ULL << stacks[i];
    for (int i = 0; i < ons; i++) oavailAll |= 1ULL << ostacks[i];
    static int undoV[1024];
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
            for (int i = 0; i < ns; i++) spare[stacks[i]] = cnt[stacks[i]];
            if (cost == 0 || cost >= 1e9) continue;
        } else {
            int need[3], nn = 0;
            for (int k = 0; k < 3; k++) if (own[tr.n[k]] != 0 && !(P_PURE && pureCorner[t][k])) need[nn++] = tr.n[k];
            if (nn == 0) continue;
            int undoN = 0;
            uint64_t available = availAll;
            for (int k = 0; k < 3; k++) {
                int c = tr.n[k];
                if (own[c] == 0 && cnt[c] > 0) { undoV[undoN++] = c; if (--spare[c] == 0) available &= ~(1ULL << c); }
            }
            int worst = 0, sum = 0;
            bool ok = true;
            for (int i = 0; i < nn; i++) {
                int c = need[i];
                int bd;
                int bu = nearestAvailable(c, available, bd);
                if (bu < 0) { ok = false; break; }
                undoV[undoN++] = bu;
                if (--spare[bu] == 0) available &= ~(1ULL << bu);
                int d = bd + ocnt[c];  // need to outnumber opponent units there
                if (P_OUTNUM && ocnt[c] > 0) {
                    // bring ocnt[c] + 1 distinct units: cost is the farthest of them
                    d = bd;
                    for (int extra = 0; extra < ocnt[c] && ok; extra++) {
                        int bd2;
                        int bu2 = nearestAvailable(c, available, bd2);
                        if (bu2 < 0) ok = false;
                        else { undoV[undoN++] = bu2; if (--spare[bu2] == 0) available &= ~(1ULL << bu2); d = max(d, bd2); }
                    }
                    if (!ok) break;
                    d += 1;  // contested: at least one more turn of fighting
                }
                worst = max(worst, d);
                sum += d;
            }
            for (int j = 0; j < undoN; j++) spare[undoV[j]]++;
            if (!ok) continue;
            cost = worst + 0.25 * sum;
        }
        double V = tris[t].owner == 1 - s ? 2.0 : 1.0;
        if ((P_RACE || P_CYCLE_RACE) && cc1[t] && owner[t] == -1) {
            // how fast could the other side take it? (same greedy assignment, outnumbering my units)
            int worst = 0, sum = 0;
            bool ok = true;
            int oundoN = 0;
            static int oundoV[8];
            uint64_t oavail = oavailAll;
            for (int k = 0; k < 3; k++) {
                int c = tr.n[k];
                if (own[c] == 1 && ocnt[c] > 0) { oundoV[oundoN++] = c; if (--ospare[c] == 0) oavail &= ~(1ULL << c); }
            }
            for (int k = 0; k < 3 && ok; k++) {
                int c = tr.n[k];
                if (own[c] == 1) continue;
                int bd;
                int bu = nearestAvailable(c, oavail, bd);
                if (bu < 0) { ok = false; break; }
                oundoV[oundoN++] = bu;
                if (--ospare[bu] == 0) oavail &= ~(1ULL << bu);
                int d = bd + cnt[c];
                worst = max(worst, d);
                sum += d;
            }
            for (int j = 0; j < oundoN; j++) ospare[oundoV[j]]++;
            if (ok) {
                double oc = worst + 0.25 * sum;
                raceState[t] = oc < cost - 0.5 ? 2 : (oc <= cost + 0.5 ? 1 : 0);
                if (P_RACE) {
                    if (raceState[t] == 2) V *= P_RACE_LOSE;
                    else if (raceState[t] == 1) V *= P_RACE_TIE;
                }
            }
        }
        pot += V * powGamma(cost);
    }
    score += P_POT * pot;
    lastTerms[2] = P_POT * pot;

    // threats: other side can capture my owned triangle next turn
    // one pass over the neighbors: other side's reach (threats) and influence (mine minus theirs)
    int reach[MAXN], infl[MAXN];
    for (int v = 0; v < N; v++) {
        int m = cnt[v], o = ocnt[v];
        for (int j = adjS[v], e = adjS[v + 1]; j < e; j++) { int n = adjL[j]; m += cnt[n]; o += ocnt[n]; }
        reach[v] = o;
        infl[v] = m - o;
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
#ifdef CHECK_FAST
#undef evalTerms
#endif


// evaluate plan of side s against the other side's view
// stage A: unit counts and triangle states after this turn's attacks, moves and spawns (before the end of turn)
inline double evalStageA(int s, const Plan& p, const View& v, int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1) {
    static bool used[MAXT];
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
    return atkPenalty;
}

// end of turn (surround kills, captures) and evaluation of the resulting position
inline double evalAfter(int s, int* cnt, int* ocnt, int* owner, bool* cc0, bool* cc1, double atkPenalty, bool dbg) {
    int T = tris.size();
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
    double base = evalTerms(s, cnt, ocnt, owner, cc0, cc1, fo, dbg);  // also fills raceState
    double cycle = 0;
    if (P_CYCLE != 0)  // used triangles this move makes capturable again (all corners vacated)
        for (int t = 0; t < T; t++)
            if (!tris[t].cc[s] && tris[t].owner != s && cc0[t] && owner[t] != 0) {
                // only worth it if the other side cannot take it before I come back:
                // safe when some corner is out of its reach next turn
                const Tri& tr = tris[t];
                const int* rs = reachStart[1 - s];
                bool safe = rs[tr.n[0]] == 0 || rs[tr.n[1]] == 0 || rs[tr.n[2]] == 0;
                double f = safe ? 1.0 : P_CYCLE_UNSAFE;
                if (P_CYCLE_RACE) f *= raceState[t] == 2 ? P_CYCLE_LOSE : (raceState[t] == 1 ? P_CYCLE_TIE : 1.0);
                cycle += P_CYCLE * f;
            }
    if (P_BLOCK != 0)  // my units still sitting on corners of used (locked) triangles delay their recapture
        for (int t = 0; t < T; t++)
            if (owner[t] == -1 && !cc0[t]) {
                const Tri& tr = tris[t];
                int occ = (cnt[tr.n[0]] > 0) + (cnt[tr.n[1]] > 0) + (cnt[tr.n[2]] > 0);
                cycle -= P_BLOCK * occ / 3.0;
            }
    return base - atkPenalty + cycle + robust;
}

double evaluate(int s, const Plan& p, const View& v, bool dbg = false) {
    int cnt[MAXN], ocnt[MAXN];
    static int owner[MAXT];
    static bool cc0[MAXT], cc1[MAXT];
    double atk = evalStageA(s, p, v, cnt, ocnt, owner, cc0, cc1);
    return evalAfter(s, cnt, ocnt, owner, cc0, cc1, atk, dbg);
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

// learned move prior per side and house (option k: 0 stay, k > 0 adj[v][k - 1]), filled each turn
double LM_PROP = 0.8;  // share of unit mutations that draw the destination from the learned prior
int LM_START = 0;      // start each side's plan from the learned most likely move
int LM_PROP_SIDES = 3; // bit 0: my search, bit 1: the opponent prediction
float lmProb[2][MAXN][MAXN + 1];
bool lmReady = false;
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
    // chain mutation: unit 0 steps from its house to a neighbor, a unit starting there steps to a
    // further neighbor, and so on, so a line of units shifts without leaving holes behind
    auto tryChain = [&]() {
        if (p.nUnits < 2) return;
        int j = rng() % p.nUnits;
        if (!p.alive[j]) return;
        int len = 2 + rng() % max(1, CHAIN_MAX - 1);
        int k = 0, prev = -1;
        while (k < len && j >= 0) {
            int a = p.from[j];
            int cand[MAXN], nc = 0;
            for (int n : adj[a]) if (n != prev) cand[nc++] = n;
            if (!nc) break;
            int b = cand[rng() % nc];
            moved[k] = j; oldDest[k] = p.dest[j]; k++;
            p.dest[j] = b;
            // next link: a unit starting at b that is not already in the chain
            int nj = -1, seen = 0;
            for (int i = 0; i < p.nUnits; i++) {
                if (!p.alive[i] || p.from[i] != b) continue;
                bool dup = false;
                for (int z = 0; z < k; z++) dup |= moved[z] == i;
                if (!dup && rng() % (++seen) == 0) nj = i;
            }
            prev = a; j = nj;
        }
        if (k < 2) { for (int q = k - 1; q >= 0; q--) p.dest[moved[q]] = oldDest[q]; return; }
        double e = evalAll();
        if (accept(e)) { cur = e; if (cur > best) snapshot(); }
        else for (int q = k - 1; q >= 0; q--) p.dest[moved[q]] = oldDest[q];
    };
    while (elapsed() < deadlineMs) {
        temp = SA_T0 * (1.0 - (elapsed() - startMs) / span);
        for (int rep = 0; rep < 8; rep++) {
            iters++;
            if (P_CHAIN > 0 && (rng() & 0xFFFF) < P_CHAIN * 65536) { tryChain(); continue; }
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
                    if (lmReady && LM_PROP > 0 && (LM_PROP_SIDES >> s & 1) && (rng() & 0xFFFF) < LM_PROP * 65536) {
                        const float* pr = lmProb[s][p.from[u]];
                        double r = (rng() & 0xFFFFFF) / double(0x1000000), c = 0;
                        int k = 0, last = p.opts[u].size() - 1;
                        for (; k < last; k++) { c += pr[k]; if (r < c) break; }
                        p.dest[u] = p.opts[u][k];
                    } else
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
double DEEP_SPLIT = 0.33401;  // share of my budget for the 1-ply search before multi-turn planning
double P_PTS = 1.4223;  // weight of points scored during the horizon
double P_DTRI = 0.22508;  // value of a triangle owned at the horizon on top of its spawn
double DEEP_SA = 0.0;     // annealing start temperature in the multi-turn search
double DEEP_MULTI = 0.0;  // group move probability in the multi-turn search
double DEEP_MIX = 0.9162;  // weight of the 1-ply evaluation of turn 0 inside the multi-turn search
const int DH = 4;
struct Deep {
    int nPlan = 0;              // planned units: alive units of plans[0], then units spawned on turn 0
    int start[MAXU + MAXT];       // start node (spawned units: their spawn corner, set during eval)
    unsigned char mv[MAXU + MAXT][DH];
    int spawnCorner[DH][MAXT];    // turns >= 1
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
    static int pos[MAXU + MAXT];
    static bool alive[MAXU + MAXT];
    int fixedCnt[MAXN] = {};
    int cnt[MAXN], ocnt[MAXN], ostat[MAXN];
    int owner[MAXT];
    bool cc0[MAXT], cc1[MAXT], used[MAXT];
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
        int spawnAt[MAXT], ns = 0;
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
    for (int k = 0; k < DH; k++) for (int t = 0; t < MAXT; t++) deep.spawnCorner[k][t] = 0;
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
// Equilibrium of a zero-sum matrix game by regret matching (CFR+ with linear averaging).
// The row player maximizes A[i][j]; x and y get the average mixed strategies of rows and columns.
void solveMatrix(const vector<vector<double>>& A, vector<double>& x, vector<double>& y, int iters = 1000) {
    int m = A.size(), n = A[0].size();
    vector<double> rx(m, 0), ry(n, 0), sx(m, 0), sy(n, 0), cx(m), cy(n), ux(m), uy(n);
    auto strat = [](const vector<double>& r, vector<double>& c) {
        double t = 0;
        for (size_t i = 0; i < r.size(); i++) { c[i] = max(r[i], 0.0); t += c[i]; }
        for (size_t i = 0; i < r.size(); i++) c[i] = t > 0 ? c[i] / t : 1.0 / r.size();
    };
    for (int it = 0; it < iters; it++) {
        strat(rx, cx); strat(ry, cy);
        double v = 0;
        for (int i = 0; i < m; i++) { ux[i] = 0; for (int j = 0; j < n; j++) ux[i] += A[i][j] * cy[j]; v += cx[i] * ux[i]; }
        for (int j = 0; j < n; j++) { uy[j] = 0; for (int i = 0; i < m; i++) uy[j] -= A[i][j] * cx[i]; }
        for (int i = 0; i < m; i++) rx[i] = max(0.0, rx[i] + ux[i] - v);
        for (int j = 0; j < n; j++) ry[j] = max(0.0, ry[j] + uy[j] + v);
        for (int i = 0; i < m; i++) sx[i] += (it + 1) * cx[i];
        for (int j = 0; j < n; j++) sy[j] += (it + 1) * cy[j];
    }
    double tx = 0, ty = 0;
    for (double a : sx) tx += a;
    for (double a : sy) ty += a;
    x.resize(m); y.resize(n);
    for (int i = 0; i < m; i++) x[i] = sx[i] / tx;
    for (int j = 0; j < n; j++) y[j] = sy[j] / ty;
}

// ---------------- learned opponent move model
// Conditional logit fit on the moves of top-30 bots in CodinGame logs (arena/oppdata + data/oppfit.py):
// each unit of a stack picks stay or a neighbor with probability softmax(LM_W . features of the destination).
const double LM_W[32] = {-0.43573, -0.029961, 0.073691, -0.054639, 0.028361, 0.16637, 0.26843, -0.45684, -0.29534, -0.41169, -0.26475, -0.38447, 0.066204, -0.084506, -0.0019432, -0.31695, -0.062104, -0.338, 0.00071743, 0.18166, 0.055088, -0.16517, 0.046144, 0.10623, 0.25638, 0.062342, 0.24066, 0.084862, 0.084862, 0.050028, -0.14751, -0.028554};
// one-hidden-layer variant (data/oppmlp.py, 16 tanh units): z = u.x + v.tanh(W x + b), x = (f - mu) / sd
const int LM_H = 16;
const double LMN_MU[32] = {0.191139, 1.15251, 0.581341, 4.74843, 2.52343, 1.47666, 4.19844, 0.540501, 0.164086, 0.293684, 0.111517, 0.521573, 0.113474, 0.40601, 0.0366083, 0.163369, 1.34254, 0.0767606, 0.176995, 0.0539797, 0.352859, 0.815308, 0.46042, 0.0308279, 0.14538, 0.063634, 0.360206, 0.340106, 0.340106, 1.10075, 0.270816, 0.155413};
const double LMN_SD[32] = {0.393198, 1.38992, 1.00786, 3.4178, 2.77779, 1.40527, 1.26324, 0.498357, 0.370354, 0.654085, 0.463084, 0.890018, 0.409408, 0.325533, 0.187798, 0.231263, 1.60297, 0.694231, 0.392143, 0.157626, 0.477859, 0.388048, 1.52437, 0.11498, 0.352484, 0.317596, 0.48006, 0.473744, 0.473744, 1.10825, 0.279101, 0.362298};
const double LMN_U[32] = {-1.33115, -0.0567716, 0.0438378, 0.0278538, -0.204203, -0.138709, 0.289398, -0.277307, -0.165529, 0.0978501, -0.0127203, -0.0227847, 0.0469046, 0.139703, 0.0170311, 0.0983887, 0.00443868, 0.0102489, 0.196068, -0.11093, -0.0929213, -0.033651, 0.0857062, -0.0178109, 0.0733842, -0.0247686, 0.22751, 0.101679, 0.101679, -0.125907, -0.220341, 0.0969137};
const double LMN_W[16][32] = {{1.69405, -0.00694939, -0.105288, -1.55453, -0.0570811, 0.0240528, 0.3154, -0.121317, -0.0756921, -0.647272, -0.201203, -0.163915, -0.0685744, -0.2155, 0.0263336, -0.187128, 0.0480497, -0.020864, 1.66231, 1.29479, -0.155453, 0.0457138, 0.625927, 0.305558, 0.994882, -0.119679, -0.061114, -0.0640157, 0.122963, -0.423364, 0.165006, -0.146508}, {0.325047, -0.082847, 0.246646, -0.348579, 0.183883, -0.446971, 0.77564, 0.89965, -0.372568, -0.221106, -0.0656835, -0.366526, 0.0839457, -0.0215471, -0.277109, -0.161231, 0.257485, 0.010695, 0.179601, -0.0423143, -0.386006, -0.247999, -0.153999, -0.0582494, -0.101212, 0.0431906, 0.500207, 0.449439, -0.00956259, -0.307155, 0.208648, 0.0193361}, {0.230641, -0.0973925, 0.101017, 0.232984, 0.107079, 0.65456, -0.271162, -0.79955, -0.1071, 0.145018, -0.0597441, -0.589978, -0.126723, -0.345664, -0.0275234, 0.00774449, -0.477701, -0.194519, -0.398298, 0.581203, -0.16701, 0.0148128, 0.320864, -0.312424, 0.177415, 1.11457, -0.38194, -0.00418296, -0.161083, -0.0510543, 0.0968303, -0.0714185}, {0.0242427, 0.412379, -0.155068, 0.00693651, -0.0622195, -0.148555, 0.633115, -0.0230106, 0.0419891, -0.216745, -0.0376023, -0.104725, 0.010092, -0.203742, -0.0670242, -0.0908225, -0.104174, -0.0680183, -0.155894, -0.00661884, -0.0534067, -0.170433, -0.0739858, -0.0514061, 0.0647478, 0.230794, -0.126062, 0.349584, -0.721277, -0.044656, 0.467192, 0.133593}, {-0.447356, 0.0722585, -0.00724125, 0.0739095, -0.252085, -0.146013, -0.00425226, -0.0767976, 0.022553, -0.038559, -0.0033647, -0.141367, 0.0534965, 0.101694, 0.134311, 0.118496, 1.81746, -0.00344865, 0.167874, -0.114091, -0.203301, 0.227663, -0.192248, -0.0808202, 0.241318, 0.0041178, 0.555996, -0.0391001, -0.0492596, -0.168494, -0.125177, -0.289403}, {0.337071, -0.473057, 0.312647, -0.464275, 0.993286, 0.285769, 0.0372264, 0.0313333, 0.0444889, 0.0202245, 0.161668, 0.0775536, -0.00679653, -0.204404, 0.0226084, -0.0741282, -0.170611, -0.177178, -0.12907, -0.125244, -0.0444136, 0.218443, -0.122733, 0.0940768, -0.0464668, -0.107798, 0.196041, -0.0724637, -0.0612855, 0.179523, 0.0818649, 0.0605094}, {0.570437, 0.26897, 0.0794628, -0.369624, 0.222487, -0.0644392, 0.233458, 0.128405, -0.11747, -0.21382, -0.0494832, 0.374024, -0.0296062, -0.252884, -0.0398507, -0.0538665, -0.400265, 0.37002, -0.258769, -0.064872, -0.106499, -0.0205219, -0.209448, -0.120944, 1.47465, 0.458098, 0.190183, -0.052882, 0.363652, -0.331905, 0.325568, 0.0681098}, {-0.535752, 0.838441, -0.300889, -0.337651, 0.315388, -0.0887503, 0.337171, 1.55607, 0.384266, -0.339921, 0.00231136, -0.517489, 0.0449689, 0.204172, -0.298971, 0.140874, 0.0554682, 0.0694516, -0.836, 0.116651, -0.300589, 0.244639, -0.0278707, -0.12194, 0.177237, 0.155647, 0.0673063, 0.206043, -0.511276, -0.524774, -0.220066, 0.121199}, {0.138584, 0.051598, -0.0205638, -0.216659, -0.0367993, -0.376908, 0.287561, -0.446183, 0.0501603, 0.110253, -0.0242367, 0.298733, 0.00326199, -0.145927, 0.0963136, 0.0838752, 0.579973, -0.0810989, 0.190439, -0.474477, -0.241409, 0.118176, -0.0928556, 0.0117776, 0.0277696, -0.153999, 0.280018, 0.567622, 0.326662, -0.193485, -0.0547093, 0.21159}, {-0.277908, 0.0162709, -0.912236, -0.167514, -0.120294, -0.775399, 0.810294, -1.19302, -0.472421, 0.483086, -0.133701, 0.43236, -0.0379497, -0.903909, 0.204841, 0.20825, -0.0837619, -0.0423448, 0.223633, -0.802636, -0.188063, 0.58845, 0.256802, 0.155356, 0.55518, -0.360546, 0.402481, -0.296077, -0.343671, -0.0125783, 0.327551, -0.121824}, {0.13787, -0.0259432, 0.0293228, 0.0210845, 0.792755, 0.610352, -0.733378, 0.334839, 0.193941, 0.652269, 0.328228, 1.07301, -0.0583102, 0.100312, 0.575341, -0.250097, 0.0992381, 0.0546985, -0.552379, -0.987381, 0.0833334, 0.198929, -0.649523, -0.00831682, 0.375822, 0.297345, -0.00694268, -0.0102137, 0.167656, -0.325161, -0.0552035, 0.137668}, {-0.358612, 0.0732592, -0.172194, 0.0288969, -0.0168381, 0.523137, -0.0826915, -0.352641, -0.40673, 0.167901, -0.141625, -0.227588, -0.119799, 0.495268, 0.860245, -0.690001, 0.568329, -0.252775, -0.143629, 0.264581, -0.0273868, -0.388054, 0.607348, 0.0789069, 0.454091, 0.0939572, -1.01723, 0.320663, 0.359273, 0.0999777, -0.117613, -0.259542}, {0.00848499, 0.0299038, -0.0729905, 0.139054, 0.123211, -0.0293015, -0.470472, 0.65086, 0.4434, -0.0487639, 0.0244358, -0.185081, -0.211034, 0.27009, -0.00674575, 0.244391, -0.19422, -1.00337, -0.29411, 0.410122, -0.150145, -0.34292, -0.528103, -0.241534, 0.0233888, 0.839913, -0.13127, 0.527651, 0.444754, 0.225736, -0.466646, -0.953681}, {-0.0475997, 0.0593912, 1.07102, -1.79963, 1.34193, 0.0823112, -0.250047, 0.0393023, 0.602255, -0.189491, 0.839007, -0.0936593, -0.354605, 0.188002, 0.0265427, 0.345468, 0.0169205, 0.0718657, 0.0258372, -0.00513049, 0.226906, 0.135466, 0.134265, -0.24145, -0.0773602, -0.0336733, -0.134295, 0.127766, -0.342952, -0.140177, -0.421724, -0.21183}, {0.444601, 0.129374, -0.142268, 0.624043, -0.548181, -0.0547642, 0.21592, -0.422389, -0.397262, -0.182338, -0.196086, -0.621265, 0.00324006, -0.427852, 0.112331, 0.0940577, -0.0596067, -0.121539, 0.9246, 1.52531, 0.0479828, 0.0744378, 0.40986, -0.137617, -0.517431, -0.414238, 0.191116, -0.325151, -0.737506, 0.0181441, 0.0919835, -0.212109}, {-0.117633, -0.0581243, 0.0120595, 0.10183, -0.192427, -0.353988, 0.245294, 0.979734, -0.156161, 0.0634987, -0.0900487, 0.569492, -0.0500917, 0.115189, -0.0224252, 0.0691987, -0.265115, -0.0168049, -0.118414, 0.0118718, 0.0939247, -0.0405328, 0.192735, 0.0418709, 0.158305, 0.274828, -0.0384586, 0.0140038, -0.179403, 0.204365, 0.141076, -0.0199469}};
const double LMN_B[16] = {-0.159184, -4.28194e-05, -0.361087, 1.53573, -0.694412, 0.392718, -0.025101, -0.407095, -0.206792, -0.574276, 0.0227573, -0.547901, -0.945298, 0.444848, 0.66038, -0.382926};
const double LMN_V[16] = {1.21612, -0.364901, -0.367334, 1.2365, -0.646419, 0.577339, 0.562345, 0.40079, -0.547757, -0.335807, -0.21504, 0.307404, 0.150813, 0.21323, 0.388358, -0.585065};
int LM_KIND = 1;         // 0 linear model, 1 hidden-layer model
int LM_SEED = 0;         // start the opponent prediction search from the learned model's most likely move
double LM_WEIGHT = 1.0;  // total weight of learned-model views among the opponent views
int LM_SAMPLES = 4;      // sampled opponent moves (each one view)
int LM_MODE = 1;         // also the most likely allocation (largest remainder per stack)

// probabilities of side s's units at house a for options a, adj[a][0], adj[a][1], ...
void learnedProbs(int s, int a, const int* own, const int* dO, double* pr) {
    const int* Us = U[s]; const int* Uo = U[1 - s];
    int nopt = 1 + adj[a].size();
    double mx = -1e30;
    for (int k = 0; k < nopt; k++) {
        int d = k == 0 ? a : adj[a][k - 1];
        double f[32] = {};
        bool stay = k == 0;
        f[0] = stay; f[1] = Us[d]; f[2] = Uo[d];
        int oh = 0, dang = 0;
        for (int n : adj[d]) { f[3] += Us[n]; f[4] += Uo[n]; oh += Uo[n] > 0; dang += Uo[n] > Us[n]; }
        f[5] = oh; f[6] = adj[d].size();
        f[7] = own[d] == 0; f[8] = own[d] == 1;
        for (int t = 0; t < (int)tris.size(); t++) {
            if (!(TRIM[t] >> d & 1)) continue;
            int to = tris[t].owner < 0 ? -1 : (tris[t].owner == s ? 0 : 1);  // relative to the mover
            f[9] += to == 0; f[10] += to == 1;
            bool cS = true, cO = true;
            for (int c = 0; c < 3; c++) if (tris[t].n[c] != d) { cS &= own[tris[t].n[c]] == 0; cO &= own[tris[t].n[c]] == 1; }
            f[11] += cS && to != 0; f[12] += cO && to != 1;
        }
        double best = 0; int full = 0;
        for (int v : adj[d]) {
            if (Uo[v] <= 0) continue;
            int have = 0;
            for (int n : adj[v]) if (n != d && Us[n] > Uo[n]) have++;
            double prg = (have + 1.0) / adj[v].size();
            best = max(best, prg);
            if (have + 1 == (int)adj[v].size() && Us[d] + 1 > Uo[d]) full = 1;
        }
        f[13] = best; f[14] = full;
        f[15] = adj[d].empty() ? 0 : (double)dang / adj[d].size();
        int dd = min(dO[d], 10);
        f[16] = dd; f[17] = dd - min(dO[a], 10);
        f[18] = stay ? log(1.0 + Us[a]) : 0;
        f[19] = stay ? turnNo / 100.0 : 0;
        f[20] = Us[d] == 0;
        f[21] = Us[d] + (stay ? 0 : 1) > Uo[d];
        f[22] = stay * f[4]; f[23] = stay * f[15]; f[24] = stay * f[7]; f[25] = stay * f[9];
        f[26] = dd == 1; f[27] = dd == 0; f[28] = Uo[d] > 0; f[29] = min(Us[d], 4); f[30] = best * best; f[31] = f[21] * (Uo[d] > 0);
        double z = 0;
        if (LM_KIND == 1) {
            double x[32];
            for (int i = 0; i < 32; i++) { x[i] = (f[i] - LMN_MU[i]) / LMN_SD[i]; z += LMN_U[i] * x[i]; }
            for (int h = 0; h < LM_H; h++) {
                double a = LMN_B[h];
                for (int i = 0; i < 32; i++) a += LMN_W[h][i] * x[i];
                z += LMN_V[h] * tanh(a);
            }
        } else {
            for (int i = 0; i < 32; i++) z += LM_W[i] * f[i];
        }
        pr[k] = z; mx = max(mx, z);
    }
    double t = 0;
    for (int k = 0; k < nopt; k++) { pr[k] = exp(pr[k] - mx); t += pr[k]; }
    for (int k = 0; k < nopt; k++) pr[k] /= t;
}

// learned-model plans for side s: keep the spawns and attacks of base, replace the moves
// (mode = largest-remainder allocation per stack, else one random sample)
void learnedPlan(int s, const Plan& base, Plan& out, bool mode) {
    out = base;
    int own[MAXN], dO[MAXN];
    for (int v = 0; v < N; v++) own[v] = nodeOwner(U[s], U[1 - s], v);
    for (int v = 0; v < N; v++) {
        dO[v] = 99;
        for (int w = 0; w < N; w++) if (U[1 - s][w] > 0) dO[v] = min(dO[v], D[v][w]);
    }
    double pr[MAXN];
    int idx[MAXU];
    for (int a = 0; a < N; a++) {
        int m = 0;
        for (int i = 0; i < out.nUnits; i++) if (out.alive[i] && out.from[i] == a) idx[m++] = i;
        if (!m) continue;
        learnedProbs(s, a, own, dO, pr);
        int nopt = 1 + adj[a].size();
        if (mode) {
            double rem[MAXN]; int take[MAXN], tot = 0;
            for (int k = 0; k < nopt; k++) { double e = pr[k] * m; take[k] = (int)e; rem[k] = e - take[k]; tot += take[k]; }
            while (tot < m) { int b = 0; for (int k = 1; k < nopt; k++) if (rem[k] > rem[b]) b = k; take[b]++; rem[b] = -1; tot++; }
            int u = 0;
            for (int k = 0; k < nopt; k++) for (int c = 0; c < take[k]; c++) out.dest[idx[u++]] = k == 0 ? a : adj[a][k - 1];
        } else {
            for (int q = 0; q < m; q++) {
                double r = (rng() & 0xFFFFFF) / double(0x1000000), c = 0;
                int k = 0;
                for (; k < nopt - 1; k++) { c += pr[k]; if (r < c) break; }
                out.dest[idx[q]] = k == 0 ? a : adj[a][k - 1];
            }
        }
    }
}

// fill lmProb for both sides from the current units
void computeLmTables() {
    for (int s = 0; s < 2; s++) {
        int own[MAXN], dO[MAXN];
        for (int v = 0; v < N; v++) own[v] = nodeOwner(U[s], U[1 - s], v);
        for (int v = 0; v < N; v++) {
            dO[v] = 99;
            for (int w = 0; w < N; w++) if (U[1 - s][w] > 0) dO[v] = min(dO[v], D[v][w]);
        }
        double pr[MAXN + 1];
        for (int a = 0; a < N; a++) {
            if (!U[s][a]) continue;
            learnedProbs(s, a, own, dO, pr);
            for (int k = 0; k <= (int)adj[a].size(); k++) lmProb[s][a][k] = pr[k];
        }
    }
    lmReady = true;
}

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
    // commands are ';'-separated; parsed with sscanf (std::regex costs seconds of compile time on CodinGame)
    vector<array<int, 3>> moves, spawns;
    {
        size_t st = 0;
        while (st <= line.size()) {
            size_t en = line.find(';', st);
            if (en == string::npos) en = line.size();
            string cmd = line.substr(st, en - st);
            int x, y, z;
            size_t m = cmd.find("MOVE "), sp = cmd.find("SPAWN ");
            if (m != string::npos && sscanf(cmd.c_str() + m, "MOVE %d %d %d", &x, &y, &z) == 3) moves.push_back({x, y, z});
            else if (sp != string::npos && sscanf(cmd.c_str() + sp, "SPAWN %d %d %d", &x, &y, &z) == 3) spawns.push_back({x, y, z});
            st = en + 1;
        }
    }
    for (auto& mv : moves) {
        int a = mv[0], b = mv[1], k = mv[2];
        for (int i = 0; i < q.nUnits && k > 0; i++)
            if (q.from[i] == a && q.dest[i] == a) {
                int d = b;
                if (find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) {  // multi-step: first step on a shortest path
                    for (int n : adj[a]) if (D[n][b] < D[a][b]) { d = n; break; }
                }
                q.dest[i] = d; k--;
            }
    }
    for (auto& sv : spawns) {
        int c = sv[0], b = sv[1], d = sv[2];
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
struct OState { int cnt[MAXN]; int owner[MAXT]; bool cc0[MAXT], cc1[MAXT]; double pts, score; int act; };
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
            int spawnAt[MAXT], nsp = 0;
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


// ---------------- rollout opening
void buildGraphDerived();
// On turn 1 the sides are far apart, so my next turns depend only on my own moves. Play ROLL_H turns of my
// own search several times against a static opponent (each run differs by the search's randomness), keep the
// run that ends with the most units + owned triangles, and follow it while the real game matches
// (applyOpening). The simulation uses the same end-of-turn rules as the search (finalizeSim).
int ROLL = 1;               // 0 = off
int ROLL_H = 7;             // simulated turns
double ROLL_TURN = 0.2;     // search time per simulated turn, as a share of TIME_BUDGET
double ROLL_TOTAL = 14;     // total time on turn 1, in TIME_BUDGETs
double ROLL_TRI = 1.0;      // weight of owned triangles in the final score
int rollStats[3];           // rollouts, plan turns followed (diagnostics)

void rolloutOpening(Clock::time_point t0) {
    auto elapsed = [&]() { return chrono::duration<double, milli>(Clock::now() - t0).count(); };
    int T = tris.size();
    if (T > MAXT) return;
    // real state, restored after every rollout
    int U0[MAXN], U1[MAXN];
    memcpy(U0, U[0], sizeof(int) * N); memcpy(U1, U[1], sizeof(int) * N);
    vector<Tri> tris0 = tris;
    int turn0 = turnNo;
    bool early0 = earlyOn;
    double best = -1e18;
    vector<OAction> bestActs;
    vector<array<int, MAXN>> bestCnt;
    vector<vector<int>> bestOwn;
    double until = elapsed() + ROLL_TOTAL * TIME_BUDGET;
    int runs = 0;
    while (elapsed() + ROLL_H * ROLL_TURN * TIME_BUDGET < until || runs == 0) {
        runs++;
        memcpy(U[0], U0, sizeof(int) * N); memcpy(U[1], U1, sizeof(int) * N);
        tris = tris0; turnNo = turn0;
        vector<OAction> acts;
        vector<array<int, MAXN>> expCnt;
        vector<vector<int>> expOwn;
        int owner[MAXT]; bool cc0[MAXT], cc1[MAXT];
        FinalizeOut fo{};
        for (int h = 0; h < ROLL_H; h++) {
            array<int, MAXN> c{};
            for (int v = 0; v < N; v++) c[v] = U[0][v];
            expCnt.push_back(c);
            vector<int> own;
            for (int t = 0; t < T; t++) if (tris[t].owner == 0) own.push_back(t);
            expOwn.push_back(own);
            // my search on the simulated position, against a static opponent
            buildGraphDerived();
            {
                bool far = EARLY_T > 0 && turnNo <= EARLY_T;
                for (int u = 0; u < N && far; u++)
                    if (U[0][u])
                        for (int v = 0; v < N; v++)
                            if (U[1][v] && D[u][v] <= EARLY_DIST) { far = false; break; }
                setEarly(far);
            }
            initPlan(0);
            initPlan(1);
            if (LM_PROP > 0) computeLmTables();
            static View sv;
            staticView(1, sv);
            vector<pair<double, View*>> views{{1.0, &sv}};
            auto ts = Clock::now();
            int it = 0;
            double b = ROLL_TURN * TIME_BUDGET;
            if (DEEP_H >= 2) { optimize(0, views, b * 0.75, ts, it); deepOptimize(views, b, ts, it); }
            else optimize(0, views, b, ts, it);
            // apply: spawns use their triangles, moves, spawned units, end of turn
            const Plan& p = plans[0];
            OAction act;
            act.parent = -1;
            for (int t = 0; t < T; t++) { owner[t] = tris[t].owner; cc0[t] = tris[t].cc[0]; cc1[t] = tris[t].cc[1]; }
            int cnt[MAXN], ocnt[MAXN];
            memcpy(cnt, U[0], sizeof(int) * N); memcpy(ocnt, U[1], sizeof(int) * N);
            for (auto& sp : p.spawns) if (sp.corner >= 0) { act.spawns.push_back({sp.tri, sp.corner}); owner[sp.tri] = -1; cc0[sp.tri] = false; }
            for (int i = 0; i < p.nUnits; i++)
                if (p.alive[i] && p.dest[i] != p.from[i]) { act.moves.push_back({p.from[i], p.dest[i]}); cnt[p.from[i]]--; cnt[p.dest[i]]++; }
            for (auto& [t, k] : act.spawns) cnt[tris[t].n[k]]++;
            finalizeSim(cnt, ocnt, owner, cc0, cc1, fo);
            acts.push_back(act);
            memcpy(U[0], cnt, sizeof(int) * N);
            for (int t = 0; t < T; t++) { tris[t].owner = owner[t]; tris[t].cc[0] = cc0[t]; tris[t].cc[1] = cc1[t]; }
            turnNo++;
        }
        int units = 0;
        for (int v = 0; v < N; v++) units += U[0][v];
        double score = units + ROLL_TRI * fo.myTri + 1e-3 * (rng() % 100);
        if (score > best) { best = score; bestActs = acts; bestCnt = expCnt; bestOwn = expOwn; }
    }
    // restore the real position
    memcpy(U[0], U0, sizeof(int) * N); memcpy(U[1], U1, sizeof(int) * N);
    tris = tris0; turnNo = turn0;
    buildGraphDerived();
    setEarly(early0);
    initPlan(0);
    initPlan(1);
    if (LM_PROP > 0) computeLmTables();
    // hand the plan to applyOpening
    openActs = bestActs;
    openPlan.clear();
    for (int i = 0; i < (int)openActs.size(); i++) openPlan.push_back(i);
    openExpectCnt = bestCnt;
    openExpectOwn = bestOwn;
    openActive = !openPlan.empty();
    rollStats[0] = runs;
    cerr << "rollout opening: " << runs << " runs, best " << best << ", t " << elapsed() << endl;
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

void buildGraphDerived();
// ---------------- path actions v2: every candidate is scored by simulating this turn on the edited graph
// A path action uses one of my triangles (one unit per corner, the triangle turns neutral) and is applied after
// moves and spawns, before the end of turn: its surround kills and captures happen in the same turn.
int PATH_V2 = 1;
double PATH_MARGIN = 15.0;  // required gain over my plan without a path action
double PATH_FRAC = 0.08;   // share of the turn budget reserved for path candidates
double PATH_SYM = 1.0;     // weight of the opponent's evaluation change (its view of the same position) in the path gain
int PATH_KINDS = 3;        // bit 0: ADD_PATH candidates, bit 1: REMOVE_PATH candidates
bool pathsSeen = false;    // path actions exist in this league (linkable paths were listed at least once)

// triangle list of the current adj, like the referee: 3-cliques with no house inside; surviving triangles keep their state
void rebuildTriangles(const vector<Tri>& old) {
    for (int i = 0; i < N; i++) { NBM[i] = 0; for (int n : adj[i]) NBM[i] |= 1ULL << n; }
    vector<Tri> res;
    for (int a = 0; a < N; a++)
        for (int b : adj[a]) {
            if (b <= a) continue;
            for (int c : adj[b]) {
                if (c <= b || !((NBM[a] >> c) & 1)) continue;
                bool inside = false;
                for (int n = 0; n < N && !inside; n++) inside = insideTri(n, a, b, c);
                if (inside) continue;
                Tri t{{a, b, c}, -1, {true, true}};
                uint64_t m = (1ULL << a) | (1ULL << b) | (1ULL << c);
                for (auto& o : old)
                    if (((1ULL << o.n[0]) | (1ULL << o.n[1]) | (1ULL << o.n[2])) == m) { t.owner = o.owner; t.cc[0] = o.cc[0]; t.cc[1] = o.cc[1]; break; }
                res.push_back(t);
            }
        }
    tris = res;
}

inline void addEdge(int a, int b) { adj[a].push_back(b); adj[b].push_back(a); }
inline void removeEdge(int a, int b) {
    adj[a].erase(find(adj[a].begin(), adj[a].end(), b));
    adj[b].erase(find(adj[b].begin(), adj[b].end(), a));
}

// my plan p with triangle t used for a path action: its spawn/attacks cancelled, one staying unit per corner consumed
bool usePlanTriangle(Plan& p, int t) {
    const Tri& tr = tris[t];
    if (p.spawnOf[t] >= 0) p.spawns[p.spawnOf[t]].corner = -1;
    for (auto& a : p.atks) if (a.tri == t) a.on = false;
    for (int k = 0; k < 3; k++) {
        int c = tr.n[k], pick = -1;
        for (int i = 0; i < p.nUnits; i++)
            if (p.alive[i] && p.from[i] == c && (pick < 0 || p.dest[i] == c)) { pick = i; if (p.dest[i] == c) break; }
        if (pick < 0) return false;
        p.alive[pick] = false;
    }
    return true;
}

// my evaluation minus PATH_SYM x the opponent's evaluation of the same stage-A position (arrays are copied)
double symEval(const int* cnt, const int* ocnt, const int* owner, const bool* cc0, const bool* cc1, double atk) {
    int T = tris.size();
    int c[MAXN], o[MAXN], w[MAXT];
    bool a0[MAXT], a1[MAXT];
    memcpy(c, cnt, sizeof(int) * N); memcpy(o, ocnt, sizeof(int) * N);
    memcpy(w, owner, sizeof(int) * T); memcpy(a0, cc0, T); memcpy(a1, cc1, T);
    double v = evalAfter(0, c, o, w, a0, a1, atk, false);
    if (PATH_SYM != 0) {
        memcpy(c, ocnt, sizeof(int) * N); memcpy(o, cnt, sizeof(int) * N);
        for (int t = 0; t < T; t++) w[t] = owner[t] == -1 ? -1 : 1 - owner[t];
        memcpy(a0, cc1, T); memcpy(a1, cc0, T);
        v -= PATH_SYM * evalAfter(1, c, o, w, a0, a1, 0, false);
    }
    return v;
}

double pathGain = 0;  // last chosen path action's gain (debug)

void choosePathV2(const vector<pair<double, View*>>& views, Clock::time_point t0, double deadlineMs) {
    pathAct = {false, -1, -1, -1, 0};
    if (!PATH_V2 || !pathsSeen || turnNo > PATH_END) return;
    auto elapsed = [&]() { return chrono::duration<double, milli>(Clock::now() - t0).count(); };
    double base = 0;
    for (auto& [w, v] : views) {
        int cnt[MAXN], ocnt[MAXN], owner[MAXT];
        bool cc0[MAXT], cc1[MAXT];
        double atk = evalStageA(0, plans[0], *v, cnt, ocnt, owner, cc0, cc1);
        base += w * symEval(cnt, ocnt, owner, cc0, cc1, atk);
    }
    const vector<Tri> origTris = tris;
    vector<int> origAdj[MAXN];
    for (int v = 0; v < N; v++) origAdj[v] = adj[v];
    int nv = views.size();
    int T0 = origTris.size();
    // stage A results per view on the original graph
    static int cntA[8][MAXN], ocntA[8][MAXN], ownerA[8][MAXT];
    static bool cc0A[8][MAXT], cc1A[8][MAXT];
    double atkA[8];
    PathAction best{false, -1, -1, -1, PATH_MARGIN};
    Plan bestPlan;
    bool stop = false;
    for (int t = 0; t < T0 && !stop && nv <= 8; t++) {
        const Tri& tr = origTris[t];
        if (tr.owner != 0 || !U[0][tr.n[0]] || !U[0][tr.n[1]] || !U[0][tr.n[2]]) continue;
        Plan q = plans[0];
        if (!usePlanTriangle(q, t)) continue;
        for (int i = 0; i < nv; i++) {
            atkA[i] = evalStageA(0, q, *views[i].second, cntA[i], ocntA[i], ownerA[i], cc0A[i], cc1A[i]);
            ownerA[i][t] = -1;  // the triangle used for the path action turns neutral, and I can't recapture it yet
            cc0A[i][t] = false;
        }
        uint64_t triMask = (1ULL << tr.n[0]) | (1ULL << tr.n[1]) | (1ULL << tr.n[2]);
        for (int k = 0; k < 3 && !stop; k++) {
            int from = tr.n[k];
            vector<pair<bool, int>> cands;
            if (PATH_KINDS & 1) for (auto& [a, b] : linkable) if (a == from || b == from) cands.push_back({true, a == from ? b : a});
            if (PATH_KINDS & 2) for (int to : origAdj[from]) if (!((triMask >> to) & 1)) cands.push_back({false, to});
            for (auto& [add, to] : cands) {
                if (elapsed() > deadlineMs) { stop = true; break; }
                if (add) addEdge(from, to); else removeEdge(from, to);
                rebuildTriangles(origTris);
                buildGraphDerived();
                int T1 = tris.size();
                if (T1 > MAXT) { for (int v = 0; v < N; v++) adj[v] = origAdj[v]; tris = origTris; continue; }
                // map the stage-A triangle states onto the new triangle list
                int map1[MAXT];
                for (int j = 0; j < T1; j++) {
                    uint64_t m = TRIM[j];
                    map1[j] = -1;
                    for (int i = 0; i < T0; i++)
                        if (((1ULL << origTris[i].n[0]) | (1ULL << origTris[i].n[1]) | (1ULL << origTris[i].n[2])) == m) { map1[j] = i; break; }
                }
                double val = 0;
                for (int i = 0; i < nv; i++) {
                    int cnt[MAXN], ocnt[MAXN], owner[MAXT];
                    bool cc0[MAXT], cc1[MAXT];
                    memcpy(cnt, cntA[i], sizeof(int) * N);
                    memcpy(ocnt, ocntA[i], sizeof(int) * N);
                    for (int j = 0; j < T1; j++) {
                        int o = map1[j];
                        if (o >= 0) { owner[j] = ownerA[i][o]; cc0[j] = cc0A[i][o]; cc1[j] = cc1A[i][o]; }
                        else { owner[j] = -1; cc0[j] = cc1[j] = true; }
                    }
                    val += views[i].first * symEval(cnt, ocnt, owner, cc0, cc1, atkA[i]);
                }
                double gain = val - base;
                if (gain > best.value) { best = {add, t, from, to, gain}; bestPlan = q; }
                for (int v = 0; v < N; v++) adj[v] = origAdj[v];
                tris = origTris;
            }
        }
    }
    for (int v = 0; v < N; v++) adj[v] = origAdj[v];
    tris = origTris;
    buildGraphDerived();
    if (best.tri < 0) return;
    plans[0] = bestPlan;
    pathAct = best;
    if (getenv("PATH_DEBUG")) cerr << "PATHDBG turn " << turnNo << " " << (best.add ? "add " : "remove ") << best.from << "-" << best.to << " gain " << best.value << " base " << base << endl;
    pathGain = best.value;
}

// everything derived from the graph (adj) and the triangle list: masks, special corners, distances, reach
void buildGraphDerived() {
    for (int i = 0; i < N; i++) { NBM[i] = 0; for (int n : adj[i]) NBM[i] |= 1ULL << n; }
    adjS[0] = 0;
    for (int i = 0; i < N; i++) { adjS[i + 1] = adjS[i]; for (int n : adj[i]) adjL[adjS[i + 1]++] = n; }
    int T = tris.size();
    for (int i = 0; i < T && i < MAXT; i++) TRIM[i] = (1ULL << tris[i].n[0]) | (1ULL << tris[i].n[1]) | (1ULL << tris[i].n[2]);
    pureCorner.assign(T, {false, false, false});
    for (int i = 0; i < T; i++)
        for (int k = 0; k < 3; k++) {
            int c = tris[i].n[k];
            bool pure = !adj[c].empty();
            for (int n : adj[c]) if (n != tris[i].n[(k + 1) % 3] && n != tris[i].n[(k + 2) % 3]) pure = false;
            pureCorner[i][k] = pure;
        }
    occSets.assign(T, {});
    for (int i = 0; i < T && P_PAT; i++) {
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
}

int main(int argc, char** argv) {
    initEarlyParams();
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
        else if (k == "block") P_BLOCK = v;
        else if (k == "cycleunsafe") P_CYCLE_UNSAFE = v;
        else if (k == "cyclerace") P_CYCLE_RACE = (int)v;
        else if (k == "cyclelose") P_CYCLE_LOSE = v;
        else if (k == "cycletie") P_CYCLE_TIE = v;
        else if (k == "stack") P_STACK = v;
        else if (k == "outnum") P_OUTNUM = (int)v;
        else if (k == "inf") P_INF = v;
        else if (k == "rob") P_ROB = v;
        else if (k == "race") P_RACE = (int)v;
        else if (k == "robust") ROBUST = (int)v;
        else if (k == "nash") NASH = (int)v;
        else if (k == "lw") LM_WEIGHT = v;
        else if (k == "lsamples") LM_SAMPLES = (int)v;
        else if (k == "lmode") LM_MODE = (int)v;
        else if (k == "lkind") LM_KIND = (int)v;
        else if (k == "lseed") LM_SEED = (int)v;
        else if (k == "lprop") LM_PROP = v;
        else if (k == "lpropsides") LM_PROP_SIDES = (int)v;
        else if (k == "lstart") LM_START = (int)v;
        else if (k == "nashrounds") NASH_ROUNDS = (int)v;
        else if (k == "nashstep") NASH_STEP = v;
        else if (k == "nashsym") NASH_SYM = (int)v;
        else if (k == "nashstatic") NASH_STATIC = (int)v;
        else if (k == "nashmix") NASH_MIX = v;
        else if (k == "early") EARLY_T = (int)v;
        else if (k == "earlydist") EARLY_DIST = (int)v;
        else if (k.rfind("e_", 0) == 0) { bool f = false; for (auto& e : earlyParams) if (k == e.name) { e.early = v; f = true; } if (!f) cerr << "unknown option " << k << endl; }
        else if (k == "roll") ROLL = (int)v;
        else if (k == "rollh") ROLL_H = (int)v;
        else if (k == "rollturn") ROLL_TURN = v;
        else if (k == "rolltotal") ROLL_TOTAL = v;
        else if (k == "rolltri") ROLL_TRI = v;
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
        else if (k == "pathv2") PATH_V2 = (int)v;
        else if (k == "pathmargin") PATH_MARGIN = v;
        else if (k == "pathfrac") PATH_FRAC = v;
        else if (k == "pathsym") PATH_SYM = v;
        else if (k == "pathkinds") PATH_KINDS = (int)v;
        else if (k == "pathend") PATH_END = (int)v;
        else if (k == "pathunits") PATH_MIN_UNITS = (int)v;
        else if (k == "sa") SA_T0 = v;
        else if (k == "node") P_NODE = v;
        else if (k == "multi") P_MULTI = v;
        else if (k == "chain") P_CHAIN = v;
        else if (k == "chainmax") CHAIN_MAX = (int)v;
        else if (k == "deep") DEEP_H = min(DH, (int)v);
        else if (k == "deepsplit") DEEP_SPLIT = v;
        else if (k == "pts") P_PTS = v;
        else if (k == "dtri") P_DTRI = v;
        else if (k == "mix") DEEP_MIX = v;
        else if (k == "dsa") DEEP_SA = v;
        else if (k == "dmulti") DEEP_MULTI = v;
        else if (k == "safe") P_SAFE = v;
    }
    for (int i = 0; i < 1024; i++) POW4[i] = pow(P_GAMMA, i * 0.25);
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
        int lc; cin >> lc;
        linkable.clear();
        for (int i = 0; i < lc; i++) { int a, b; cin >> a >> b; linkable.push_back({a, b}); }
        if (lc > 0) pathsSeen = true;

        buildGraphDerived();
        {
            bool far = EARLY_T > 0 && turnNo <= EARLY_T;
            for (int u = 0; u < N && far; u++)
                if (U[0][u])
                    for (int v = 0; v < N; v++)
                        if (U[1][v] && D[u][v] <= EARLY_DIST) { far = false; break; }
            setEarly(far);
        }
        initPlan(0);
        initPlan(1);
        if (LM_PROP > 0 || LM_START) computeLmTables();
        if (LM_START) {
            static Plan tmp;
            if (LM_START & 1) { learnedPlan(0, plans[0], tmp, true); plans[0] = tmp; }
            if (LM_START & 2) { learnedPlan(1, plans[1], tmp, true); plans[1] = tmp; }
        }
        if (turnNo == 1 && OPEN_DEPTH > 0) openingSearch(t0);
        if (turnNo == 1 && ROLL) rolloutOpening(t0);
        if (!PATH_V2) choosePathAction();
        double budget = turnNo == 1 ? (OPEN_DEPTH > 0 ? OPEN_MS + TIME_BUDGET * 2 : ROLL ? ROLL_TOTAL * TIME_BUDGET + TIME_BUDGET * 2 : TIME_BUDGET * 5) : TIME_BUDGET;
        bool pathTurn = PATH_V2 && pathsSeen && turnNo > 1 && turnNo <= PATH_END;
        if (pathTurn) {  // only when I own a triangle with a unit on every corner
            bool usable = false;
            for (auto& tr : tris) if (tr.owner == 0 && U[0][tr.n[0]] && U[0][tr.n[1]] && U[0][tr.n[2]]) usable = true;
            pathTurn = usable;
        }
        double pathMs = budget * PATH_FRAC;
        if (pathTurn) budget -= pathMs;
        int iters = 0, oppIters = 0;
        static View staticOpp, predOpp, staticMe;
        staticView(1, staticOpp);
        vector<pair<double, View*>> views;
        if (PRED_W > 0) {
            staticView(0, staticMe);
            if (LM_SEED) { static Plan seedP; learnedPlan(1, plans[1], seedP, true); plans[1] = seedP; }
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
                    if (getenv("PRED_L1")) {
                        // distance between predicted and real opponent unit counts after its move, vs "it stays"
                        static View realV;
                        planViewOf(q, realV);
                        int dp = 0, ds = 0, moved = 0;
                        for (int v = 0; v < N; v++) {
                            dp += abs(predOpp.cnt[v] - realV.cnt[v]);
                            ds += abs(staticOpp.cnt[v] - realV.cnt[v]);
                        }
                        for (int i = 0; i < q.nUnits; i++) moved += q.alive[i] && q.dest[i] != q.from[i];
                        static Plan lmp; static View lmv;
                        learnedPlan(1, saved, lmp, true); planViewOf(lmp, lmv);
                        int dl = 0;
                        for (int v = 0; v < N; v++) dl += abs(lmv.cnt[v] - realV.cnt[v]);
                        cerr << "L1 " << turnNo << " pred " << dp << " static " << ds << " moved " << moved << " units " << q.nUnits << " learned " << dl << endl;
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
        if (NASH && PRED_W > 0) {
            // matrix game: candidate plans for both sides (my best plans against the opponent's current
            // mix, the opponent's best replies against mine), scored pairwise, solved for the equilibrium
            const int MM = 8;
            static Plan myPlans[MM], oppPlans[MM + 2];
            static View myViews[MM], oppViews[MM + 2];
            int nm = 0, no = 0;
            Plan predPlan = plans[1];
            if (NASH_STATIC) {
                oppPlans[no] = predPlan;
                Plan& sp = oppPlans[no];
                for (int i = 0; i < sp.nUnits; i++) { sp.dest[i] = sp.from[i]; sp.alive[i] = true; }
                for (auto& x : sp.spawns) x.corner = -1;
                for (auto& a : sp.atks) a.on = false;
                oppViews[no] = staticOpp; no++;
            }
            oppPlans[no] = predPlan; oppViews[no] = predOpp; no++;
            vector<double> x, q(no, 0.0);
            q[no - 1] = 1.0;
            vector<vector<double>> A;
            auto solve = [&]() {
                A.assign(nm, vector<double>(no));
                for (int i = 0; i < nm; i++)
                    for (int j = 0; j < no; j++) {
                        A[i][j] = evaluate(0, myPlans[i], oppViews[j]);
                        if (NASH_SYM) A[i][j] -= evaluate(1, oppPlans[j], myViews[i]);
                    }
                solveMatrix(A, x, q);
            };
            double frac = PRED_FRAC;
            for (int r = 0; r < NASH_ROUNDS && nm < MM; r++) {
                vector<pair<double, View*>> vv;
                for (int j = 0; j < no; j++) if (q[j] > 0.01) vv.push_back({q[j], &oppViews[j]});
                frac += NASH_STEP;
                optimize(0, vv, budget * frac, t0, iters);
                myPlans[nm] = plans[0]; planViewOf(plans[0], myViews[nm]); nm++;
                solve();
                vector<pair<double, View*>> mv;
                for (int i = 0; i < nm; i++) if (x[i] > 0.01) mv.push_back({x[i], &myViews[i]});
                plans[1] = predPlan;
                frac += NASH_STEP;
                optimize(1, mv, budget * frac, t0, oppIters);
                oppPlans[no] = plans[1]; planViewOf(plans[1], oppViews[no]); no++;
                q.push_back(0);
                solve();
            }
            plans[1] = predPlan;
            int bi = 0;
            for (int i = 1; i < nm; i++) if (x[i] > x[bi]) bi = i;
            plans[0] = myPlans[bi];
            views.clear();
            int predIdx = NASH_STATIC ? 1 : 0;
            for (int j = 0; j < no; j++) {
                double w = NASH_MIX * q[j] + (j == predIdx ? 1 - NASH_MIX : 0);
                if (w > 0.01) views.push_back({w, &oppViews[j]});
            }
            if (getenv("NASH_DEBUG")) {
                cerr << "NASH x";
                for (double a : x) cerr << " " << a;
                cerr << " q";
                for (double a : q) cerr << " " << a;
                cerr << endl;
            }
        } else if (ROBUST && PRED_W > 0) {
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
            if (LM_WEIGHT > 0) {
                // learned-model opponent moves, built on the predicted plan's spawns and attacks
                static Plan lp[8];
                static View lv[8];
                int nl = 0;
                if (LM_MODE) { learnedPlan(1, predPlan, lp[nl], true); planViewOf(lp[nl], lv[nl]); nl++; }
                for (int k = 0; k < LM_SAMPLES && nl < 8; k++) { learnedPlan(1, predPlan, lp[nl], false); planViewOf(lp[nl], lv[nl]); nl++; }
                for (int k = 0; k < nl; k++) views.push_back({LM_WEIGHT / nl, &lv[k]});
            }
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
        if (pathTurn) choosePathV2(views, t0, budget + pathMs);
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
            cerr << "path " << (pathAct.add ? "add " : "remove ") << pathAct.from << "-" << pathAct.to << " gain " << pathAct.value << endl;
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

