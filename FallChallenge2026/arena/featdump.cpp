// Replays CodinGame text logs through the referee port and writes position features per turn and side.
//   featdump LOG... > features.csv
// Columns: game, turn, side, result (1 win, 0.5 draw, 0 loss for that side), rankSelf, rankOpp, features...
#include "referee.hpp"
#include "features.hpp"
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

using namespace tac;

int main(int argc, char** argv) {
    printf("game,turn,side,result,rankSelf,rankOpp");
    for (int k = 0; k < feat::NF; k++) printf(",%s", feat::NAMES[k]);
    printf("\n");
    std::regex frameRe(R"(^frame \d+ agent (\d+))"), seedRe(R"(seed=(-?\d+))"), agentRe(R"(^  ([01]) (\S+) \((.*)\))"), rankRe(R"(rank (\d+))");
    for (int a = 1; a < argc; a++) {
        std::ifstream in(argv[a]);
        std::string line, gid = argv[a];
        long long seed = 0; bool haveSeed = false;
        std::vector<std::string> outs[2];
        int agent = -1, rank[2] = {-1, -1};
        std::smatch m;
        while (std::getline(in, line)) {
            if (!haveSeed && line.rfind("options:", 0) == 0 && std::regex_search(line, m, seedRe)) { seed = std::stoll(m[1]); haveSeed = true; }
            if (std::regex_search(line, m, agentRe)) {
                int i = std::stoi(m[1]); std::string info = m[3]; std::smatch r;
                if (std::regex_search(info, r, rankRe)) rank[i] = std::stoi(r[1]);
            }
            if (std::regex_search(line, m, frameRe)) agent = std::stoi(m[1]);
            auto pos = line.find("stdout: ");
            if (pos != std::string::npos && agent >= 0) outs[agent].push_back(line.substr(pos + 8));
        }
        if (!haveSeed || outs[0].empty()) continue;
        Game g(seed, 2);
        size_t next[2] = {0, 0};
        std::vector<std::string> rows[2];
        Game::IO io = [&](int p, const std::string&, bool, std::string& out) {
            if (next[p] >= outs[p].size()) return false;
            if (p == 0) {
                for (int me = 0; me < 2; me++) {
                    feat::State s;
                    s.N = g.board.nodes.size();
                    for (int v = 0; v < s.N; v++) {
                        s.adj[v] = g.board.nodes[v].nb;
                        s.cnt[0][v] = g.board.nodes[v].units[me];
                        s.cnt[1][v] = g.board.nodes[v].units[1 - me];
                    }
                    for (int t : g.board.tris) {
                        const Tri& T = g.board.pool[t];
                        feat::State::T x{{T.n[0], T.n[1], T.n[2]}, T.owner == -1 ? -1 : (T.owner == me ? 0 : 1), {T.cc[me], T.cc[1 - me]}};
                        s.tris.push_back(x);
                    }
                    s.turn = next[0] + 1;
                    s.score[0] = g.score[me]; s.score[1] = g.score[1 - me];
                    double f[feat::NF];
                    feat::compute(s, f);
                    std::ostringstream os;
                    os << s.turn << "," << me;
                    for (int k = 0; k < feat::NF; k++) os << "," << f[k];
                    rows[me].push_back(os.str());
                }
            }
            out = outs[p][next[p]++];
            return true;
        };
        g.run(io);
        double res0 = g.score[0] > g.score[1] ? 1 : g.score[0] < g.score[1] ? 0 : 0.5;
        for (int me = 0; me < 2; me++)
            for (auto& r : rows[me]) {
                // move the result and ranks right after "turn,side"
                size_t c2 = r.find(',', r.find(',') + 1);
                printf("%s,%s,%g,%d,%d%s\n", gid.c_str(), r.substr(0, c2).c_str(), me == 0 ? res0 : 1 - res0, rank[me], rank[1 - me], r.substr(c2).c_str());
            }
    }
}
