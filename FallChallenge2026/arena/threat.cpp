// Measures surround threats in a CodinGame game log.
//
//   threat game.log [league]
//
// For each turn and side s: applies s's MOVE commands to the start-of-turn units (spawns and attacks
// ignored), then marks each house v where s has units as threatened when the other side's units, each
// staying or moving one step, can reach a strict majority on every neighbor of v (max flow). Compares
// with the surround kills that really happened that turn.
// Prints per side: threatened houses, of them killed, kills that were not flagged, and the same in units.
#include "referee.hpp"
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

using namespace tac;

// max flow on a small dense graph
struct Flow {
    int n; std::vector<std::vector<int>> c;
    explicit Flow(int n) : n(n), c(n, std::vector<int>(n, 0)) {}
    int dfs(int u, int t, int f, std::vector<char>& seen) {
        if (u == t) return f;
        seen[u] = 1;
        for (int v = 0; v < n; v++)
            if (!seen[v] && c[u][v] > 0) {
                int d = dfs(v, t, std::min(f, c[u][v]), seen);
                if (d > 0) { c[u][v] -= d; c[v][u] += d; return d; }
            }
        return 0;
    }
    int run(int s, int t) { int tot = 0; for (;;) { std::vector<char> seen(n, 0); int d = dfs(s, t, 1 << 29, seen); if (!d) return tot; tot += d; } }
};

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: threat LOG [league]\n"); return 2; }
    int league = argc > 2 ? atoi(argv[2]) : 2;
    std::ifstream in(argv[1]);
    std::string line;
    long long seed = 0; bool haveSeed = false;
    std::vector<std::string> outs[2];
    int agent = -1;
    std::regex frameRe(R"(^frame \d+ agent (\d+))"), seedRe(R"(seed=(-?\d+))");
    std::smatch m;
    while (std::getline(in, line)) {
        if (!haveSeed && line.rfind("options:", 0) == 0 && std::regex_search(line, m, seedRe)) { seed = std::stoll(m[1]); haveSeed = true; }
        if (std::regex_search(line, m, frameRe)) agent = std::stoi(m[1]);
        auto pos = line.find("stdout: ");
        if (pos != std::string::npos && agent >= 0) outs[agent].push_back(line.substr(pos + 8));
    }
    if (!haveSeed) { fprintf(stderr, "no seed\n"); return 2; }
    Game g(seed, league);
    size_t next[2] = {0, 0};
    std::vector<std::string> slog;
    g.board.surroundLog = &slog;
    size_t slogSeen = 0;
    // per side: threatened houses, threatened and killed, killed not threatened; units for each
    long long thr[2] = {}, thrK[2] = {}, missK[2] = {}, thrU[2] = {}, thrKU[2] = {}, missKU[2] = {};
    long long st[2] = {}, stK[2] = {}, stU[2] = {}, stKU[2] = {};  // static threats: no opponent move needed
    std::vector<std::vector<int>> flaggedStatic(2);
    std::vector<std::vector<int>> flagged(2);  // houses flagged this turn, per side
    std::vector<int> afterU[2];
    auto collectKills = [&]() {
        for (; slogSeen < slog.size(); slogSeen++) {
            int s, k, v;
            if (sscanf(slog[slogSeen].c_str(), "p%d lost %d at %d", &s, &k, &v) != 3) continue;
            bool f = false;
            for (int x : flagged[s]) f |= x == v;
            if (f) { thrK[s]++; thrKU[s] += k; } else { missK[s]++; missKU[s] += k; }
            for (int x : flaggedStatic[s]) if (x == v) { stK[s]++; stKU[s] += k; }
        }
        flagged[0].clear(); flagged[1].clear(); flaggedStatic[0].clear(); flaggedStatic[1].clear();
    };
    Game::IO io = [&](int p, const std::string&, bool, std::string& out) {
        if (next[p] >= outs[p].size()) return false;
        if (p == 0) collectKills();
        if (p == 1 && next[0] > 0) {
            const std::string* mv[2] = {&outs[0][next[0] - 1], &outs[1][next[1]]};
            int N = (int)g.board.nodes.size();
            for (int s = 0; s < 2; s++) {
                int o = 1 - s;
                afterU[s].assign(N, 0);
                for (int v = 0; v < N; v++) afterU[s][v] = g.board.nodes[v].units[s];
                std::stringstream ss(*mv[s]); std::string cmd;
                while (std::getline(ss, cmd, ';')) {
                    int a, b, c;
                    if (sscanf(cmd.c_str(), " MOVE %d %d %d", &a, &b, &c) == 3 && a >= 0 && a < N && b >= 0 && b < N) {
                        c = std::min(c, afterU[s][a]); afterU[s][a] -= c; afterU[s][b] += c;
                    }
                }
                for (int v = 0; v < N; v++) {
                    if (afterU[s][v] <= 0 || g.board.nodes[v].nb.empty()) continue;
                    // nodes: 0 source, 1 sink, 2+u supply house u, 2+N+i demand neighbor i
                    const auto& nb = g.board.nodes[v].nb;
                    Flow F(2 + N + (int)nb.size());
                    int need = 0;
                    for (int u = 0; u < N; u++) {
                        int c = g.board.nodes[u].units[o];
                        if (!c) continue;
                        F.c[0][2 + u] = c;
                        for (size_t i = 0; i < nb.size(); i++) {
                            int n = nb[i];
                            bool reach = n == u;
                            for (int w : g.board.nodes[u].nb) reach |= w == n;
                            if (reach) F.c[2 + u][2 + N + i] = 1 << 20;
                        }
                    }
                    for (size_t i = 0; i < nb.size(); i++) { int d = afterU[s][nb[i]] + 1; F.c[2 + N + i][1] = d; need += d; }
                    if (F.run(0, 1) >= need) {
                        thr[s]++; thrU[s] += afterU[s][v]; flagged[s].push_back(v);
                        bool stat = true;
                        for (int n : nb) stat &= g.board.nodes[n].units[o] > afterU[s][n];
                        if (stat) { st[s]++; stU[s] += afterU[s][v]; flaggedStatic[s].push_back(v); }
                    }
                }
            }
        }
        out = outs[p][next[p]++];
        return true;
    };
    g.run(io);
    collectKills();
    for (int s = 0; s < 2; s++)
        printf("p%d threatened %lld killed %lld missed %lld | units threatened %lld killed %lld missed %lld | static %lld killed %lld units %lld killed %lld\n", s, thr[s], thrK[s], missK[s], thrU[s], thrKU[s], missKU[s], st[s], stK[s], stU[s], stKU[s]);
    return 0;
}
