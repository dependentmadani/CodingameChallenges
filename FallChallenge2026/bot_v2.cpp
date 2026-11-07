#pragma GCC optimize("Ofast,unroll-loops,inline,omit-frame-pointer")
#pragma GCC target("avx2,bmi,bmi2,lzcnt,popcnt")
// Fall Challenge 2026 (TryAngle-Catch) bot, version 2.
//
// Search: both players get an H-turn plan. Each unit has a destination per turn and moves one
// step toward it along the referee's shortest path; each triangle has an action per turn (hold,
// spawn on a corner, attack the adjacent house with the most enemy units); each spawned unit has
// its own destinations. Plans are scored by simulating H turns with the exact rules. The search
// alternates hill climbing on the opponent's plan (against mine) and on my plan (against theirs),
// warm-started from the previous turn's plans shifted by one turn.
#include <bits/stdc++.h>
using namespace std;
using Clock = chrono::steady_clock;

namespace sim {

constexpr int MAXN = 50, MAXT = 64, MAXU = 320, MAXH = 6;

// ---------------------------------------------------------------- static board
int N, T;
uint64_t NB[MAXN];        // neighbor mask
int DEG[MAXN], ADJ[MAXN][16];
int DIST[MAXN][MAXN];     // 99 = unreachable
int8_t HOP[MAXN][MAXN];   // next house from a toward b (referee order), a itself if none
int TC[MAXT][3];          // triangle corners (input order)
uint64_t TMASK[MAXT];     // corners
uint64_t TADJ[MAXT];      // houses adjacent to at least one corner (attack targets)

void buildGraph() {
    for (int a = 0; a < N; a++) {
        DEG[a] = 0;
        for (int b = 0; b < N; b++) if (NB[a] >> b & 1) ADJ[a][DEG[a]++] = b;
    }
    for (int s = 0; s < N; s++) {
        for (int v = 0; v < N; v++) DIST[s][v] = 99;
        int q[MAXN], h = 0, t = 0;
        q[t++] = s; DIST[s][s] = 0;
        while (h < t) {
            int c = q[h++];
            for (int i = 0; i < DEG[c]; i++) { int n = ADJ[c][i]; if (DIST[s][n] == 99) { DIST[s][n] = DIST[s][c] + 1; q[t++] = n; } }
        }
    }
    for (int a = 0; a < N; a++)
        for (int b = 0; b < N; b++) {
            HOP[a][b] = (int8_t)a;
            if (a == b || DIST[b][a] >= 99) continue;
            for (int i = 0; i < DEG[a]; i++) {
                int n = ADJ[a][i];
                if (DIST[b][n] < DIST[b][a]) { HOP[a][b] = (int8_t)n; break; }
            }
        }
}
void buildTriangles() {
    for (int t = 0; t < T; t++) {
        TMASK[t] = TADJ[t] = 0;
        for (int k = 0; k < 3; k++) { TMASK[t] |= 1ULL << TC[t][k]; TADJ[t] |= NB[TC[t][k]]; }
    }
}

// ---------------------------------------------------------------- state
struct St {
    int16_t u[2][MAXN];  // units
    int16_t rem[2][MAXN];
    int8_t own[MAXT];    // owner: -1, 0, 1
    int8_t iown[MAXT];   // owner at the start of the turn
    uint64_t cc[2];      // triangles each player may capture
    int score[2];
    int turn;            // input turn number of the turn about to be played (1..200)
};

inline uint64_t occMask(const St& s, int p) {
    uint64_t m = 0;
    for (int v = 0; v < N; v++) if (s.u[p][v] > 0) m |= 1ULL << v;
    return m;
}
inline bool canUse(const St& s, int t, int p, int k) {
    if (s.own[t] != p || s.iown[t] != p) return false;
    for (int c = 0; c < 3; c++) if (s.rem[p][TC[t][c]] < k) return false;
    return true;
}
inline void useTri(St& s, int t, int k) {
    int o = s.own[t];
    for (int c = 0; c < 3; c++) {
        int v = TC[t][c];
        int take = min<int>(k, s.u[o][v]);
        s.u[o][v] -= take;
        s.rem[o][v] -= take;
    }
    s.cc[o] &= ~(1ULL << t);
    s.own[t] = -1;
}
inline void startTurn(St& s) { memcpy(s.rem, s.u, sizeof s.u); }

// attack phase: frame by frame, each player applies its next attack that is valid at the frame start
void attackPhase(St& s, const int* atkT0, const int* atkV0, int n0, const int* atkT1, const int* atkV1, int n1) {
    const int* at[2] = {atkT0, atkT1};
    const int* av[2] = {atkV0, atkV1};
    int n[2] = {n0, n1}, idx[2] = {0, 0};
    while (true) {
        int ch[2] = {-1, -1};
        for (int p = 0; p < 2; p++)
            while (idx[p] < n[p]) {
                int k = idx[p]++;
                if (canUse(s, at[p][k], p, 1) && (TADJ[at[p][k]] >> av[p][k] & 1)) { ch[p] = k; break; }
            }
        if (ch[0] < 0 && ch[1] < 0) return;
        for (int p = 0; p < 2; p++) {
            if (ch[p] < 0) continue;
            int t = at[p][ch[p]], v = av[p][ch[p]];
            if (s.own[t] == p) useTri(s, t, 1);  // the referee applies it without a second check
            s.u[1 - p][v] = 0;
            s.rem[1 - p][v] = 0;
        }
    }
}
// one MOVE command (referee semantics); returns the house the units went to, or -1
inline int moveCmd(St& s, int p, int from, int to, int amt) {
    if (from == to || s.rem[p][from] <= 0) return -1;
    int a = min<int>(amt, s.rem[p][from]);
    if (a <= 0) return -1;
    int nx = HOP[from][to];
    if (nx == from) return -1;
    s.u[p][from] -= a;
    s.rem[p][from] -= a;
    s.u[p][nx] += a;
    return nx;
}
// one SPAWN command; returns true if applied
inline bool spawnCmd(St& s, int p, int t, int corner) {
    if (!canUse(s, t, p, 0)) return false;
    useTri(s, t, 0);
    s.u[p][corner]++;
    return true;
}

// end of turn: surround kills (once), captures until stable, then scoring
void finalize(St& s) {
    bool surrDone = false;
    while (true) {
        uint64_t occ0 = occMask(s, 0), occ1 = occMask(s, 1);
        uint64_t occ[2] = {occ0, occ1};
        for (int t = 0; t < T; t++) {
            uint64_t b = 1ULL << t;
            if (s.own[t] >= 0) {
                for (int i = 0; i < 2; i++) { if (s.own[t] != i) s.cc[i] |= b; else s.cc[i] &= ~b; }
            } else {
                for (int i = 0; i < 2; i++) if ((TMASK[t] & occ[i]) == 0) s.cc[i] |= b;
            }
        }
        uint64_t adv0 = 0, adv1 = 0;
        for (int v = 0; v < N; v++) {
            if (s.u[0][v] > s.u[1][v]) adv0 |= 1ULL << v;
            else if (s.u[1][v] > s.u[0][v]) adv1 |= 1ULL << v;
        }
        uint64_t S0 = 0, S1 = 0;  // houses all of whose neighbors have a majority of one player
        for (int v = 0; v < N; v++) {
            if (!NB[v]) continue;
            if ((NB[v] & ~adv0) == 0) S0 |= 1ULL << v;
            else if ((NB[v] & ~adv1) == 0) S1 |= 1ULL << v;
        }
        if (!surrDone) {
            surrDone = true;
            uint64_t k0 = S1 & occ0, k1 = S0 & occ1;  // player 0's units surrounded by 1, and vice versa
            if (k0 | k1) {
                for (uint64_t m = k0; m; m &= m - 1) s.u[0][__builtin_ctzll(m)] = 0;
                for (uint64_t m = k1; m; m &= m - 1) s.u[1][__builtin_ctzll(m)] = 0;
                continue;
            }
        }
        uint64_t O0 = S0 | (adv0 & ~S1), O1 = S1 | (adv1 & ~S0);
        bool changed = false;
        for (int t = 0; t < T; t++) {
            int old = s.own[t];
            if ((s.cc[0] >> t & 1) && (TMASK[t] & ~O0) == 0) s.own[t] = 0;
            if ((s.cc[1] >> t & 1) && (TMASK[t] & ~O1) == 0) s.own[t] = 1;
            changed |= s.own[t] != old;
        }
        if (changed) continue;
        break;
    }
    for (int t = 0; t < T; t++) {
        if (s.own[t] >= 0) s.score[s.own[t]]++;
        s.iown[t] = s.own[t];
    }
    s.turn++;
}

inline int unitsOf(const St& s, int p) { int c = 0; for (int v = 0; v < N; v++) c += s.u[p][v]; return c; }
inline int trisOf(const St& s, int p) { int c = 0; for (int t = 0; t < T; t++) c += s.own[t] == p; return c; }
// referee kill checks at the start of a turn: bit p set when player p is eliminated
inline int killed(const St& s) {
    int r = 0;
    for (int p = 0; p < 2; p++) {
        int u = unitsOf(s, p), tr = trisOf(s, p);
        if (u == 0 && tr == 0) r |= 1 << p;
        else if (u == 1 && tr == 0 && s.score[p] < s.score[1 - p]) r |= 1 << p;
        else if (trisOf(s, 1 - p) >= T * 0.8) r |= 1 << p;
    }
    return r;
}

}  // namespace sim

using namespace sim;

// ---------------------------------------------------------------- parameters
double TIME_BUDGET = 38.0;    // ms per turn
double FIRST_BUDGET = 800.0;  // ms on turn 1
int H = 5;                    // planning horizon
double W_TRI = 2.0;           // value of a triangle owned at the horizon
double W_UNIT = 3.0;          // value of a unit at the horizon (scaled by remaining turns)
double OPP_SHARE = 0.35;      // share of the search spent on the opponent's plan
double W_NULL = 0.25;         // weight of the evaluation against a passive opponent
int ROUNDS = 6;               // alternation rounds per turn
int RADIUS = 3;               // destination radius for mutations
double W_POT = 1.5;           // value of a capturable triangle at the horizon, halved per move needed
uint64_t rngState = 88172645463325252ULL;
inline uint32_t rnd() { rngState ^= rngState << 7; rngState ^= rngState >> 9; return (uint32_t)rngState; }
inline int rndInt(int n) { return (int)((uint64_t)rnd() * n >> 32); }

// ---------------------------------------------------------------- plans
enum { G_HOLD = -1, G_ATTACK = 3 };
struct Plan {
    int nu;                          // base units (present at the root)
    int16_t base[MAXU];              // their start houses
    uint8_t dest[MAXH][MAXU];        // destination per turn
    int8_t tg[MAXH][MAXT];           // triangle action per turn: -1 hold, 0..2 spawn corner, 3 attack
    uint8_t sdest[MAXH][MAXT][MAXH]; // unit spawned on turn k by triangle t: destination on turn j > k
};

St root;
Plan plan[2], nullPlan[2];
uint64_t RING[MAXN][8];          // houses at exactly distance r (r < 7), RING[v][7] = farther
int nearCnt[MAXN][8];          // houses within distance r: nearList[v][0 .. nearCnt[v][r])
int nearList[MAXN][MAXN];

void buildNear() {
    for (int v = 0; v < N; v++) {
        int c = 0;
        for (int r = 0; r < 8; r++) {
            RING[v][r] = 0;
            for (int w = 0; w < N; w++) if (DIST[v][w] == r || (r == 7 && DIST[v][w] >= 7 && DIST[v][w] < 99)) { nearList[v][c++] = w; RING[v][r] |= 1ULL << w; }
            nearCnt[v][r] = c;
        }
    }
}

// simulation scratch: unit positions and liveness per side
struct Units {
    int n;
    int16_t pos[MAXU];
    uint8_t* d[MAXU];  // destination per turn (base: dest[k][i] gathered; spawned: sdest)
    int16_t id[MAXU];  // base index, or -1 - (k * MAXT + t) for a spawned unit
    bool alive[MAXU];
};

double unitValue(int turn) {
    double rem = max(0, 200 - turn);
    return W_UNIT * min(1.0, rem / 120.0);
}

// make the virtual units at each house match the real counts after kills
inline void reconcile(const St& s, int p, Units& U) {
    int16_t seen[MAXN] = {};
    for (int i = 0; i < U.n; i++) {
        if (!U.alive[i]) continue;
        int v = U.pos[i];
        if (seen[v] >= s.u[p][v]) U.alive[i] = false;
        else seen[v]++;
    }
}

struct SimOut { int atkT[MAXT], atkV[MAXT], nAtk; };

// choose attacks for side p on turn k from its plan
inline void chooseAttacks(const St& s, int p, const Plan& P, int k, SimOut& o) {
    o.nAtk = 0;
    for (int t = 0; t < T; t++) {
        if (P.tg[k][t] != G_ATTACK || !canUse(s, t, p, 1)) continue;
        int best = -1, bu = 0;
        for (uint64_t m = TADJ[t]; m; m &= m - 1) {
            int v = __builtin_ctzll(m);
            if (s.u[1 - p][v] > bu) { bu = s.u[1 - p][v]; best = v; }
        }
        if (best >= 0) { o.atkT[o.nAtk] = t; o.atkV[o.nAtk] = best; o.nAtk++; }
    }
}

// capture potential of player p: capturable triangles not owned by p, valued by the moves needed
// to put a majority unit on each corner p does not own yet (nearest unit per corner)
double potential(const St& s, int p) {
    static const double HALF[16] = {1, .5, .25, .125, .0625, .03125, .015625, .0078125, 0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t occ = occMask(s, p);
    if (!occ) return 0;
    int units = unitsOf(s, p);
    double sum = 0;
    for (int t = 0; t < T; t++) {
        if (s.own[t] == p || !(s.cc[p] >> t & 1)) continue;
        int cost = 0, need = 0;
        for (int c = 0; c < 3 && cost < 8; c++) {
            int v = TC[t][c];
            if (s.u[p][v] > s.u[1 - p][v]) continue;
            need++;
            int d = 7;
            for (int r = 0; r < 7; r++) if (occ & RING[v][r]) { d = r; break; }
            cost += max(d, 1) + s.u[1 - p][v];
        }
        if (need == 0 || need > units) continue;
        sum += HALF[min(cost, 15)] * (s.own[t] == 1 - p ? 1.5 : 1.0);
    }
    return W_POT * sum;
}

// Simulate H turns of plans a (me) and b (opponent). Returns the evaluation for player 0.
// When rec != nullptr, records turn-0 commands for player 0.
struct Record { vector<string> cmds; };
double simulate(const Plan& a, const Plan& b, Record* rec = nullptr) {
    static St s;
    s = root;
    static Units U[2];
    const Plan* P[2] = {&a, &b};
    for (int p = 0; p < 2; p++) {
        U[p].n = P[p]->nu;
        for (int i = 0; i < P[p]->nu; i++) { U[p].pos[i] = P[p]->base[i]; U[p].alive[i] = true; U[p].id[i] = i; }
    }
    int h = min(H, 200 - root.turn + 1);
    int k = 0;
    for (; k < h; k++) {
        if (k > 0) {
            int kl = killed(s);
            if (kl) {
                double big = 100000.0 - 100 * k;
                if (kl == 2) return big;
                if (kl == 1) return -big;
                return 0;
            }
        }
        startTurn(s);
        SimOut o[2];
        for (int p = 0; p < 2; p++) chooseAttacks(s, p, *P[p], k, o[p]);
        if (rec && k == 0)
            for (int i = 0; i < o[0].nAtk; i++) {
                int t = o[0].atkT[i];
                rec->cmds.push_back("ATTACK " + to_string(TC[t][0]) + " " + to_string(TC[t][1]) + " " + to_string(TC[t][2]) + " " + to_string(o[0].atkV[i]));
            }
        attackPhase(s, o[0].atkT, o[0].atkV, o[0].nAtk, o[1].atkT, o[1].atkV, o[1].nAtk);
        // moves
        for (int p = 0; p < 2; p++) {
            reconcile(s, p, U[p]);
            Units& u = U[p];
            static int mvCount[MAXN][MAXN];
            static int touched[MAXN * MAXN], nt;
            if (rec && k == 0 && p == 0) nt = 0;
            for (int i = 0; i < u.n; i++) {
                if (!u.alive[i]) continue;
                int dst = u.id[i] >= 0 ? P[p]->dest[k][u.id[i]] : u.d[i][k];
                int from = u.pos[i];
                if (dst == from) continue;
                int nx = moveCmd(s, p, from, dst, 1);
                if (nx < 0) continue;
                u.pos[i] = (int16_t)nx;
                if (rec && k == 0 && p == 0) {
                    if (mvCount[from][nx]++ == 0) touched[nt++] = from * MAXN + nx;
                }
            }
            if (rec && k == 0 && p == 0) {
                for (int i = 0; i < nt; i++) {
                    int f = touched[i] / MAXN, t = touched[i] % MAXN;
                    rec->cmds.push_back("MOVE " + to_string(f) + " " + to_string(t) + " " + to_string(mvCount[f][t]));
                    mvCount[f][t] = 0;
                }
            }
        }
        // spawns
        for (int p = 0; p < 2; p++) {
            const Plan& pl = *P[p];
            for (int t = 0; t < T; t++) {
                int g = pl.tg[k][t];
                if (g < 0 || g > 2) continue;
                int c = TC[t][g];
                if (!spawnCmd(s, p, t, c)) continue;
                if (rec && k == 0 && p == 0)
                    rec->cmds.push_back("SPAWN " + to_string(c) + " " + to_string(TC[t][(g + 1) % 3]) + " " + to_string(TC[t][(g + 2) % 3]));
                Units& u = U[p];
                if (u.n < MAXU) {
                    int i = u.n++;
                    u.pos[i] = (int16_t)c;
                    u.alive[i] = true;
                    u.id[i] = (int16_t)(-1 - (k * MAXT + t));
                    u.d[i] = const_cast<uint8_t*>(pl.sdest[k][t]);
                }
            }
        }
        finalize(s);
    }
    if (k > 0 || h == 0) {
        int kl = killed(s);
        if (kl == 2) return 100000.0 - 100 * k;
        if (kl == 1) return -(100000.0 - 100 * k);
    }
    double e = (s.score[0] - s.score[1]) - (root.score[0] - root.score[1]);
    e += W_TRI * (trisOf(s, 0) - trisOf(s, 1));
    e += unitValue(s.turn) * (unitsOf(s, 0) - unitsOf(s, 1));
    if (W_POT > 0) e += potential(s, 0) - potential(s, 1);
    return e;
}

// ---------------------------------------------------------------- plan setup and mutation
void initPlanFromRoot(Plan& P, int p) {
    P.nu = 0;
    for (int v = 0; v < N; v++)
        for (int c = 0; c < root.u[p][v] && P.nu < MAXU; c++) P.base[P.nu++] = (int16_t)v;
}
// a neighbor of corner c outside triangle t (to free the triangle for recapture), or c itself
int VACATE[MAXT][3];
void buildVacate() {
    for (int t = 0; t < T; t++)
        for (int c = 0; c < 3; c++) {
            int v = TC[t][c], best = v;
            for (int i = 0; i < DEG[v]; i++) {
                int n = ADJ[v][i];
                if (!(TMASK[t] >> n & 1)) { best = n; break; }
            }
            VACATE[t][c] = best;
        }
}
void defaultGenes(Plan& P) {
    for (int k = 0; k < MAXH; k++) {
        for (int i = 0; i < P.nu; i++) P.dest[k][i] = (uint8_t)P.base[i];
        for (int t = 0; t < T; t++) {
            P.tg[k][t] = (int8_t)rndInt(3);
            for (int j = 0; j < MAXH; j++) P.sdest[k][t][j] = (uint8_t)VACATE[t][P.tg[k][t]];
        }
    }
}
// passive opponent: units stay, triangles held
void passiveGenes(Plan& P) {
    for (int k = 0; k < MAXH; k++) {
        for (int i = 0; i < P.nu; i++) P.dest[k][i] = (uint8_t)P.base[i];
        for (int t = 0; t < T; t++) {
            P.tg[k][t] = G_HOLD;
            for (int j = 0; j < MAXH; j++) P.sdest[k][t][j] = (uint8_t)TC[t][0];
        }
    }
}

inline int randomDestNear(int v, int r) { return nearList[v][rndInt(nearCnt[v][min(r, 7)])]; }

// send the nearest units (by start house) to the corners of a triangle, from turn k on
void captureMutation(Plan& P, int p) {
    if (P.nu == 0) return;
    // a triangle near a random unit
    int i0 = rndInt(P.nu), v0 = P.base[i0];
    int cand[MAXT], nc = 0;
    for (int t = 0; t < T; t++) {
        int d = min(DIST[v0][TC[t][0]], min(DIST[v0][TC[t][1]], DIST[v0][TC[t][2]]));
        if (d <= RADIUS) cand[nc++] = t;
    }
    if (!nc) return;
    int t = cand[rndInt(nc)];
    int k = rndInt(3) == 0 ? 0 : rndInt(H);
    bool usedU[MAXU] = {};
    for (int c = 0; c < 3; c++) {
        int v = TC[t][c];
        if (root.u[p][v] > root.u[1 - p][v] && rndInt(2)) continue;  // often keep a corner that is already held
        int best = -1, bd = 99;
        for (int i = 0; i < P.nu; i++) {
            if (usedU[i]) continue;
            int d = DIST[P.base[i]][v] + rndInt(2);
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) return;
        usedU[best] = true;
        for (int j = k; j < H; j++) P.dest[j][best] = (uint8_t)v;
    }
}

void mutate(Plan& P, int p) {
    int r = rndInt(100);
    if (r < 20) { captureMutation(P, p); return; }
    if (r < 55 && P.nu > 0) {
        // one unit: new destination from turn k on (or only on turn k)
        int i = rndInt(P.nu);
        int k = rndInt(3) == 0 ? 0 : rndInt(H);
        int d = randomDestNear(P.base[i], RADIUS + k);
        bool all = rndInt(10) < 7;
        for (int j = k; j < (all ? H : k + 1); j++) P.dest[j][i] = (uint8_t)d;
    } else if (r < 70 && P.nu > 0) {
        // every unit on the same house: same new destination
        int i = rndInt(P.nu);
        int v = P.base[i];
        int k = rndInt(H);
        int d = randomDestNear(v, RADIUS + k);
        for (int q = 0; q < P.nu; q++)
            if (P.base[q] == v) for (int j = k; j < H; j++) P.dest[j][q] = (uint8_t)d;
    } else if (r < 90) {
        // triangle action
        int t = rndInt(T), k = rndInt(H);
        int g = rndInt(5) - 1;  // -1..3
        P.tg[k][t] = (int8_t)g;
        if (g >= 0 && g <= 2) for (int j = 0; j < MAXH; j++) P.sdest[k][t][j] = (uint8_t)VACATE[t][g];
    } else {
        // spawned unit destination
        int t = rndInt(T), k = rndInt(H - 1);
        int j = k + 1 + rndInt(H - k - 1);
        int c = TC[t][max(0, (int)P.tg[k][t]) % 3];
        int d = randomDestNear(c, 2);
        for (int q = j; q < H; q++) P.sdest[k][t][q] = (uint8_t)d;
    }
}

// shift last turn's plan for side p by one turn onto the current units
Plan prevPlan[2];
bool havePrev = false;
St prevRoot;
void warmStart(int p) {
    Plan& P = plan[p];
    initPlanFromRoot(P, p);
    defaultGenes(P);
    if (!havePrev) return;
    const Plan& Q = prevPlan[p];
    // last turn's units at their predicted turn-1 houses: base units one step toward their turn-0
    // destination, units spawned on turn 0 on their spawn corner. Each keeps its later destinations.
    struct Src { int house; const uint8_t* genes; int stride; };
    vector<vector<Src>> at(N);
    for (int i = 0; i < Q.nu; i++) {
        int from = Q.base[i], d = Q.dest[0][i];
        int nx = d == from ? from : HOP[from][d];
        at[nx].push_back({nx, &Q.dest[0][i], MAXU});
    }
    for (int t = 0; t < T; t++) {
        int g = Q.tg[0][t];
        if (g < 0 || g > 2 || prevRoot.own[t] != p) continue;
        at[TC[t][g]].push_back({TC[t][g], &Q.sdest[0][t][0], 1});
    }
    vector<int> taken(N, 0);
    for (int i = 0; i < P.nu; i++) {
        int v = P.base[i];
        if (taken[v] < (int)at[v].size()) {
            const Src& q = at[v][taken[v]++];
            for (int k = 0; k + 1 < MAXH; k++) P.dest[k][i] = q.genes[(k + 1) * q.stride];
            P.dest[MAXH - 1][i] = P.dest[MAXH - 2][i];
        }
    }
    for (int k = 0; k + 1 < MAXH; k++)
        for (int t = 0; t < T; t++) {
            P.tg[k][t] = Q.tg[k + 1][t];
            for (int j = 0; j + 1 < MAXH; j++) P.sdest[k][t][j] = Q.sdest[k + 1][t][j + 1];
        }
}

// ---------------------------------------------------------------- input
int myScore, oppScore, turnNo = 0;
vector<array<int, 2>> curEdges;

#ifndef NO_MAIN
int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        string k = argv[i];
        double v = atof(argv[i + 1]);
        if (k == "time") TIME_BUDGET = v;
        else if (k == "first") FIRST_BUDGET = v;
        else if (k == "h") H = max(2, min(MAXH, (int)v));
        else if (k == "wtri") W_TRI = v;
        else if (k == "wunit") W_UNIT = v;
        else if (k == "opp") OPP_SHARE = v;
        else if (k == "wnull") W_NULL = v;
        else if (k == "rounds") ROUNDS = (int)v;
        else if (k == "radius") RADIUS = (int)v;
        else if (k == "pot") W_POT = v;
        else if (k == "seed") rngState = (uint64_t)v * 2654435761ULL + 1;
    }
    cin >> N;
    for (int i = 0; i < N; i++) { int id, x, y; cin >> id >> x >> y; }
    vector<array<int, 2>> lastEdges;
    while (true) {
        if (!(cin >> myScore >> oppScore)) return 0;
        auto t0 = Clock::now();
        turnNo++;
        for (int i = 0; i < N; i++) { int id, a, b; cin >> id >> a >> b; root.u[0][id] = (int16_t)a; root.u[1][id] = (int16_t)b; }
        int pc; cin >> pc;
        vector<array<int, 2>> edges(pc);
        for (auto& e : edges) cin >> e[0] >> e[1];
        int tc; cin >> tc;
        T = min(tc, MAXT);
        root.cc[0] = root.cc[1] = 0;
        for (int i = 0; i < tc; i++) {
            int a, b, c, o, m, q; cin >> a >> b >> c >> o >> m >> q;
            if (i >= MAXT) continue;
            TC[i][0] = a; TC[i][1] = b; TC[i][2] = c;
            root.own[i] = root.iown[i] = (int8_t)o;
            if (m) root.cc[0] |= 1ULL << i;
            if (q) root.cc[1] |= 1ULL << i;
        }
        int lc; cin >> lc;
        for (int i = 0; i < lc; i++) { int a, b; cin >> a >> b; }
        if (edges != lastEdges) {
            for (int v = 0; v < N; v++) NB[v] = 0;
            for (auto& e : edges) { NB[e[0]] |= 1ULL << e[1]; NB[e[1]] |= 1ULL << e[0]; }
            buildGraph();
            buildNear();
            lastEdges = edges;
        }
        buildTriangles();
        buildVacate();
        root.score[0] = myScore; root.score[1] = oppScore;
        root.turn = turnNo;

        double budget = turnNo == 1 ? FIRST_BUDGET : TIME_BUDGET;
        auto elapsed = [&] { return chrono::duration<double, milli>(Clock::now() - t0).count(); };

        for (int p = 0; p < 2; p++) {
            warmStart(p);
            nullPlan[p] = plan[p];
            initPlanFromRoot(nullPlan[p], p);
            passiveGenes(nullPlan[p]);
        }
        auto evalMe = [&](const Plan& me) {
            double e = simulate(me, plan[1]);
            if (W_NULL > 0) e = (1 - W_NULL) * e + W_NULL * simulate(me, nullPlan[1]);
            return e;
        };
        double best = evalMe(plan[0]);
        long iters = 0, oppIters = 0;
        Plan cand;
        for (int round = 0; round < ROUNDS; round++) {
            double roundEnd = budget * (round + 1) / ROUNDS;
            double oppEnd = budget * (round + OPP_SHARE) / ROUNDS;
            // opponent: best response to my plan (minimizes my evaluation)
            double ob = simulate(plan[0], plan[1]);
            while (elapsed() < oppEnd) {
                cand = plan[1];
                int m = 1 + rndInt(3);
                for (int j = 0; j < m; j++) mutate(cand, 1);
                double e = simulate(plan[0], cand);
                oppIters++;
                if (e <= ob) { ob = e; plan[1] = cand; }
            }
            best = evalMe(plan[0]);
            while (elapsed() < roundEnd) {
                cand = plan[0];
                int m = 1 + rndInt(3);
                for (int j = 0; j < m; j++) mutate(cand, 0);
                double e = evalMe(cand);
                iters++;
                if (e >= best) { best = e; plan[0] = cand; }
            }
        }

        Record rec;
        simulate(plan[0], plan[1], &rec);
        string out;
        for (auto& c : rec.cmds) out += c + ";";
        if (out.empty()) out = "WAIT";
        else out.pop_back();
        cout << out << endl;
        cerr << "turn " << turnNo << " iters " << iters << "/" << oppIters << " t " << elapsed() << " e " << best << endl;
        prevPlan[0] = plan[0];
        prevPlan[1] = plan[1];
        prevRoot = root;
        havePrev = true;
    }
}
#endif
