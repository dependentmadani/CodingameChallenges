// Extracts unit move decisions from CodinGame game logs, for fitting an opponent move model.
//
//   oppdata LOG [league] >> rows.csv
//
// For every turn, side and house with that side's units, writes one row per option (stay, or step to
// a neighbor) with features of the destination seen from the mover, and how many units took it.
// Columns: game,turn,side,rank,me,group,n,count,dest,<features>   (rank -1 = unknown; me = 1 for our bot)
#include "referee.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>

using namespace tac;

static const char* FEATS[] = {"stay", "ownD", "oppD", "ownNb", "oppNb", "oppNbHouses", "deg", "ownerS", "ownerO",
                              "triS", "triO", "complete", "completeO", "surrProg", "surrFull", "danger", "distOpp",
                              "dDist", "stackLog", "stayTurn", "ownD0", "maj", "lastIn", "bounce", "lastOutD"};
const int NF = sizeof(FEATS) / sizeof(FEATS[0]);

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oppdata LOG [league] [header]\n"); return 2; }
    int league = argc > 2 ? atoi(argv[2]) : 2;
    if (argc > 3) {
        printf("game,turn,side,rank,me,group,n,count,dest");
        for (int f = 0; f < NF; f++) printf(",%s", FEATS[f]);
        printf("\n");
    }
    std::ifstream in(argv[1]);
    std::string line;
    long long seed = 0, gameId = 0; bool haveSeed = false;
    std::vector<std::string> outs[2];
    int agent = -1, rank[2] = {-1, -1}, me[2] = {0, 0};
    std::regex frameRe(R"(^frame \d+ agent (\d+))"), seedRe(R"(seed=(-?\d+))"), agRe(R"(^  ([01]) \S+ \((.*)\))"), rankRe(R"(rank (\d+))");
    std::smatch m;
    while (std::getline(in, line)) {
        if (line.rfind("game ", 0) == 0) gameId = atoll(line.c_str() + 5);
        if (!haveSeed && line.rfind("options:", 0) == 0 && std::regex_search(line, m, seedRe)) { seed = std::stoll(m[1]); haveSeed = true; }
        if (std::regex_match(line, m, agRe)) {
            int a = std::stoi(m[1]); std::string info = m[2];
            me[a] = info.find("me") == 0 || info.find("me,") != std::string::npos;
            std::smatch r;
            if (std::regex_search(info, r, rankRe)) rank[a] = std::stoi(r[1]);
        }
        if (std::regex_search(line, m, frameRe)) agent = std::stoi(m[1]);
        auto pos = line.find("stdout: ");
        if (pos != std::string::npos && agent >= 0) outs[agent].push_back(line.substr(pos + 8));
    }
    if (!haveSeed) { fprintf(stderr, "no seed\n"); return 2; }
    Game g(seed, league);
    size_t next[2] = {0, 0};
    std::vector<std::array<int, 4>> mlog, prevLog;
    g.moveLog = &mlog;
    // snapshot of the start of the turn
    struct Snap { int turn; std::vector<int> u[2]; std::vector<int> owner; std::vector<int> triOwner; } snap;
    bool haveSnap = false;
    int turn = 0;
    const Board& B = g.board;
    auto flush = [&]() {
        if (!haveSnap) return;
        int N = B.nodes.size();
        for (int s = 0; s < 2; s++) {
            int o = 1 - s;
            auto& U = snap.u[s]; auto& O = snap.u[o];
            // distance from every house to the nearest unit of the other side
            std::vector<int> dO(N, 99), q;
            for (int v = 0; v < N; v++) if (O[v] > 0) { dO[v] = 0; q.push_back(v); }
            for (size_t h = 0; h < q.size(); h++) for (int nx : B.nodes[q[h]].nb) if (dO[nx] > dO[q[h]] + 1) { dO[nx] = dO[q[h]] + 1; q.push_back(nx); }
            for (int a = 0; a < N; a++) {
                if (U[a] <= 0) continue;
                std::vector<int> opts{a};
                for (int n : B.nodes[a].nb) opts.push_back(n);
                std::vector<int> cnt(opts.size(), 0);
                int movedAll = 0;
                for (auto& e : mlog) if (e[0] == s && e[1] == a)
                    for (size_t k = 1; k < opts.size(); k++) if (opts[k] == e[2]) { cnt[k] += e[3]; movedAll += e[3]; }
                cnt[0] = std::max(0, U[a] - movedAll);
                for (size_t k = 0; k < opts.size(); k++) {
                    int d = opts[k];
                    double f[NF] = {};
                    const auto& nb = B.nodes[d].nb;
                    f[0] = k == 0;
                    f[1] = U[d] - (k == 0 ? 0 : 0);
                    f[2] = O[d];
                    int oh = 0, dang = 0;
                    for (int n : nb) { f[3] += U[n]; f[4] += O[n]; oh += O[n] > 0; dang += O[n] > U[n]; }
                    f[5] = oh; f[6] = nb.size();
                    f[7] = snap.owner[d] == s; f[8] = snap.owner[d] == o;
                    for (size_t t = 0; t < B.tris.size(); t++) {
                        const Tri& T = B.pool[B.tris[t]];
                        if (!T.hasNode(d)) continue;
                        int to = snap.triOwner[t];
                        f[9] += to == s; f[10] += to == o;
                        bool cS = true, cO = true;
                        for (int c = 0; c < 3; c++) if (T.n[c] != d) { cS &= snap.owner[T.n[c]] == s; cO &= snap.owner[T.n[c]] == o; }
                        f[11] += cS && to != s; f[12] += cO && to != o;
                    }
                    // surround progress: for enemy-occupied neighbors v of d, share of v's neighbors I already outnumber on (d counted)
                    double best = 0; int full = 0;
                    for (int v : nb) {
                        if (O[v] <= 0) continue;
                        const auto& nv = B.nodes[v].nb;
                        int have = 0;
                        for (int n : nv) if (n != d && U[n] > O[n]) have++;
                        double pr = (have + 1.0) / nv.size();
                        best = std::max(best, pr);
                        if (have + 1 == (int)nv.size() && U[d] + 1 > O[d]) full = 1;
                    }
                    f[13] = best; f[14] = full;
                    f[15] = nb.empty() ? 0 : (double)dang / nb.size();
                    f[16] = std::min(dO[d], 10);
                    f[17] = std::min(dO[d], 10) - std::min(dO[a], 10);
                    f[18] = k == 0 ? std::log(1.0 + U[a]) : 0;
                    f[19] = k == 0 ? snap.turn / 100.0 : 0;
                    f[20] = U[d] == 0;
                    f[21] = U[d] + (k == 0 ? 0 : 1) > O[d];
                    // last turn's moves of the same side
                    for (auto& e : prevLog) if (e[0] == s) {
                        if (e[2] == a) f[22] += e[3];               // arrived at a
                        if (e[1] == d && e[2] == a && k > 0) f[23] += e[3];  // came from d: going back
                        if (e[1] == d && k > 0) f[24] += e[3];       // left d
                    }
                    printf("%lld,%d,%d,%d,%d,%lld,%d,%d,%d", gameId, snap.turn, s, rank[s], me[s], gameId * 100000 + snap.turn * 200 + s * 100 + a, U[a], cnt[k], d);
                    for (int x = 0; x < NF; x++) printf(",%.4g", f[x]);
                    printf("\n");
                }
            }
        }
        haveSnap = false;
    };
    g.beforeActions = [&](Game& G) {
        flush();
        prevLog = mlog;
        mlog.clear();
        turn++;
        snap.turn = turn;
        int N = G.board.nodes.size();
        for (int s = 0; s < 2; s++) { snap.u[s].resize(N); for (int v = 0; v < N; v++) snap.u[s][v] = G.board.nodes[v].units[s]; }
        snap.owner.resize(N);
        for (int v = 0; v < N; v++) snap.owner[v] = G.board.nodeOwner(v);
        snap.triOwner.resize(G.board.tris.size());
        for (size_t t = 0; t < G.board.tris.size(); t++) snap.triOwner[t] = G.board.pool[G.board.tris[t]].owner;
        haveSnap = true;
    };
    Game::IO io = [&](int p, const std::string&, bool, std::string& out) {
        if (next[p] >= outs[p].size()) return false;
        out = outs[p][next[p]++];
        return true;
    };
    g.run(io);
    flush();
    return 0;
}
