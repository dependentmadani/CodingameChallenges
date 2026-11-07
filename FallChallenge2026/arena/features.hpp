// Position features for a learned evaluation, from one side's point of view (side 0 = "me").
// Plain arrays only, so the same code can be copied into the bot.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace feat {
static const int MAXN = 64;
struct State {
    int N = 0;
    std::vector<int> adj[MAXN];
    int cnt[2][MAXN] = {};                  // units: [0] me, [1] opponent
    struct T { int n[3]; int owner; bool cc[2]; };  // owner: -1, 0 me, 1 opp; cc[side] can capture
    std::vector<T> tris;
    int turn = 0, score[2] = {0, 0};
};
static const char* NAMES[] = {"tri", "units", "score", "houses", "major", "stacks", "singles", "maxstack", "exposedCrude",
                              "exposedExact", "contact", "near2", "threat", "pot", "infl", "totUnits", "triShare", "dist"};
static const int NF = sizeof(NAMES) / sizeof(NAMES[0]);

// can side a (counts att) outnumber side b (counts def) on every neighbor of v after one step (greedy assignment)?
inline bool canSurround(const State& s, int v, const int* att, const int* def) {
    int left[MAXN];
    int k = 0, nb[MAXN], need[MAXN];
    for (int n : s.adj[v]) { nb[k] = n; need[k] = def[n] + 1; k++; }
    for (int i = 0; i < k; i++) { int n = nb[i]; left[n] = att[n]; for (int u : s.adj[n]) left[u] = att[u]; }
    for (int i = 0; i < k; i++) { int n = nb[i]; int use = std::min(need[i], left[n]); need[i] -= use; left[n] -= use; }
    for (int i = 0; i < k; i++) {
        int n = nb[i];
        for (int u : s.adj[n]) {
            if (need[i] <= 0) break;
            int use = std::min(need[i], left[u]); need[i] -= use; left[u] -= use;
        }
        if (need[i] > 0) return false;
    }
    return true;
}

inline void compute(const State& s, double* f) {
    const int N = s.N;
    const int* c[2] = {s.cnt[0], s.cnt[1]};
    int own[MAXN];
    for (int v = 0; v < N; v++) {
        bool a0 = !s.adj[v].empty(), a1 = a0;
        for (int n : s.adj[v]) { if (!(c[0][n] > c[1][n])) a0 = false; if (!(c[1][n] > c[0][n])) a1 = false; }
        own[v] = a0 ? 0 : a1 ? 1 : (c[0][v] > c[1][v] ? 0 : c[1][v] > c[0][v] ? 1 : -1);
    }
    // distances (BFS) for potential and spread
    static int D[MAXN][MAXN];
    for (int a = 0; a < N; a++) {
        for (int v = 0; v < N; v++) D[a][v] = 1000;
        int q[MAXN], h = 0, t = 0; q[t++] = a; D[a][a] = 0;
        while (h < t) { int x = q[h++]; for (int n : s.adj[x]) if (D[a][n] == 1000) { D[a][n] = D[a][x] + 1; q[t++] = n; } }
    }
    double side[2][NF] = {};
    int reach[2][MAXN];
    for (int p = 0; p < 2; p++)
        for (int v = 0; v < N; v++) { int r = c[p][v]; for (int n : s.adj[v]) r += c[p][n]; reach[p][v] = r; }
    for (int p = 0; p < 2; p++) {
        int o = 1 - p;
        double* g = side[p];
        for (auto& t : s.tris) g[0] += t.owner == p;
        int units = 0, stacks = 0, singles = 0, maxs = 0;
        for (int v = 0; v < N; v++) { units += c[p][v]; if (c[p][v]) { stacks++; maxs = std::max(maxs, c[p][v]); } singles += c[p][v] == 1; }
        g[1] = units; g[2] = s.score[p] / 10.0;
        for (int v = 0; v < N; v++) { g[3] += own[v] == p; g[4] += c[p][v] > c[o][v]; }
        g[5] = stacks; g[6] = singles; g[7] = maxs;
        for (int v = 0; v < N; v++) {
            if (!c[p][v] || s.adj[v].empty()) continue;
            bool all = true;
            for (int n : s.adj[v]) if (reach[o][n] <= c[p][n]) { all = false; break; }
            if (all) { g[8] += c[p][v]; if (canSurround(s, v, c[o], c[p])) g[9] += c[p][v]; }
            bool touch = c[o][v] > 0;
            for (int n : s.adj[v]) touch |= c[o][n] > 0;
            if (touch) g[10] += c[p][v];
        }
        double pot = 0;
        for (auto& t : s.tris) {
            if (t.owner == p) {  // threatened: the other side can outnumber me on every corner next turn
                bool all = true;
                for (int k = 0; k < 3; k++) if (reach[o][t.n[k]] <= c[p][t.n[k]]) all = false;
                if (all) g[12] += 1;
                continue;
            }
            if (!t.cc[p]) continue;
            int ownedCorners = 0, cost = 0;
            for (int k = 0; k < 3; k++) {
                int v = t.n[k];
                if (own[v] == p) { ownedCorners++; continue; }
                int best = 1000;
                for (int u = 0; u < N; u++) if (c[p][u]) best = std::min(best, D[u][v]);
                cost += best + c[o][v];
            }
            if (ownedCorners >= 2) g[11] += 1;
            if (cost < 1000) pot += std::pow(0.7, cost) * (t.owner == o ? 2 : 1);
        }
        g[13] = pot;
        int ctl = 0;
        for (auto& t : s.tris) {
            bool all = true;
            for (int k = 0; k < 3; k++) { int v = t.n[k]; int m = reach[p][v] - reach[o][v]; if (m <= 0) all = false; }
            if (all && t.owner != o) ctl++;
        }
        g[14] = ctl;
        int dsum = 0;  // average distance of my units to the nearest house I don't own
        for (int u = 0; u < N; u++) if (c[p][u]) {
            int best = 1000;
            for (int v = 0; v < N; v++) if (own[v] != p) best = std::min(best, D[u][v]);
            if (best < 1000) dsum += best * c[p][u];
        }
        g[17] = units ? (double)dsum / units : 0;
    }
    for (int k = 0; k < NF; k++) f[k] = side[0][k] - side[1][k];
    f[15] = side[0][1] + side[1][1];
    f[16] = s.tris.empty() ? 0 : (side[0][0] - side[1][0]) / s.tris.size();
}
}  // namespace feat
