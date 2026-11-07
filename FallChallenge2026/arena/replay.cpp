// Replays a CodinGame text game log through the referee port and compares the outcome.
//
//   replay game.log
//
// Takes the seed from the "options: seed=..." line and each agent's stdout lines in order, feeds
// them to the port, then compares the final scores and the summary events (captures, spawns,
// attack kills, surround kills) per player with the totals in the log.
#include "referee.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>

using namespace tac;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: replay LOG [league]\n"); return 2; }
    int league = argc > 2 ? atoi(argv[2]) : 2;
    std::ifstream in(argv[1]);
    std::string line;
    long long seed = 0;
    bool haveSeed = false;
    std::vector<std::string> outs[2];
    int agent = -1;
    int cg[4][2] = {};  // captured, spawned, attacked, surrounded per player, from summary lines
    int cgScore[2] = {0, 0};
    std::regex frameRe(R"(^frame \d+ agent (\d+))"), seedRe(R"(seed=(-?\d+))");
    std::regex sumRe(R"(\$(\d) (captured|spawned|attacked|surrounded) (\d+) )"), scoreRe(R"(scores: 0=(-?\d+) 1=(-?\d+))");
    std::smatch m;
    while (std::getline(in, line)) {
        if (!haveSeed && line.rfind("options:", 0) == 0 && std::regex_search(line, m, seedRe)) { seed = std::stoll(m[1]); haveSeed = true; }
        if (std::regex_search(line, m, frameRe)) agent = std::stoi(m[1]);
        auto pos = line.find("stdout: ");
        if (pos != std::string::npos && agent >= 0) outs[agent].push_back(line.substr(pos + 8));
        if (std::regex_search(line, m, sumRe)) {
            const char* kinds[] = {"captured", "spawned", "attacked", "surrounded"};
            for (int k = 0; k < 4; k++) if (m[2] == kinds[k]) cg[k][std::stoi(m[1])] += std::stoi(m[3]);
        }
        if (std::regex_search(line, m, scoreRe)) { cgScore[0] = std::stoi(m[1]); cgScore[1] = std::stoi(m[2]); }
    }
    if (!haveSeed) { fprintf(stderr, "no seed in log\n"); return 2; }

    Game g(seed, league);
    size_t next[2] = {0, 0};
    bool ranOut = false;
    bool verbose = getenv("VERBOSE") != nullptr;
    std::vector<std::string> slog;
    size_t slogShown = 0;
    std::string prevState;  // units before the previous turn: "house:mine/theirs" for occupied houses
    if (getenv("SURR")) g.board.surroundLog = &slog;
    FILE* dumpIn[2] = {nullptr, nullptr};  // DUMPIN=prefix writes each player's input to prefix.0 / prefix.1
    if (getenv("DUMPIN")) for (int p = 0; p < 2; p++) dumpIn[p] = fopen((std::string(getenv("DUMPIN")) + "." + std::to_string(p)).c_str(), "w");
    Game::IO io = [&](int p, const std::string& input, bool, std::string& out) {
        if (dumpIn[p]) { fputs(input.c_str(), dumpIn[p]); fflush(dumpIn[p]); }
        if (next[p] >= outs[p].size()) { ranOut = true; return false; }
        out = outs[p][next[p]++];
        if (p == 0 && getenv("SURR")) {
            for (; slogShown < slog.size(); slogShown++)
                printf("T%3zu surround %s   start of turn: %s\n", next[0] - 1, slog[slogShown].c_str(), prevState.c_str());
            prevState.clear();
            for (auto& nd : g.board.nodes)
                if (nd.units[0] || nd.units[1]) prevState += std::to_string(nd.id) + ":" + std::to_string(nd.units[0]) + "/" + std::to_string(nd.units[1]) + " ";
        }
        if (verbose) {
            if (p == 0)
                printf("T%3zu score %3d-%-3d units %2d-%-2d tris %2d-%-2d (of %zu)  ", next[0], g.score[0], g.score[1], g.board.unitsOf(0),
                       g.board.unitsOf(1), g.board.trianglesOf(0), g.board.trianglesOf(1), g.board.tris.size());
            int c[3] = {0, 0, 0};
            for (const char* k : {"MOVE", "SPAWN", "ATTACK"}) {
                size_t pos = 0; int idx = std::string(k) == "MOVE" ? 0 : std::string(k) == "SPAWN" ? 1 : 2;
                while ((pos = out.find(k, pos)) != std::string::npos) { c[idx]++; pos++; }
            }
            printf("p%d M%d S%d A%d", p, c[0], c[1], c[2]);
            if (p == 0) printf(" | ");
            else  // running totals before this turn: spawned, attack kills, surround kills (kills made BY each player)
                printf("  cum sp %d-%d ak %d-%d sk %d-%d\n", g.statSpawned[0], g.statSpawned[1], g.statAttacked[0], g.statAttacked[1],
                       g.board.statSurrounded[0], g.board.statSurrounded[1]);
        }
        return true;
    };
    g.run(io);

    int port[4][2] = {{g.board.statCaptured[0], g.board.statCaptured[1]},
                      {g.statSpawned[0], g.statSpawned[1]},
                      {g.statAttacked[0], g.statAttacked[1]},
                      {g.board.statSurrounded[0], g.board.statSurrounded[1]}};
    const char* kinds[] = {"captured triangles", "spawned units", "attack kills", "surround kills"};
    bool same = true;
    printf("seed %lld, %zu/%zu outputs in log, port used %zu/%zu%s\n", seed, outs[0].size(), outs[1].size(), next[0], next[1],
           ranOut ? " (RAN OUT: port asked for more turns than the log has)" : "");
    printf("%-20s %12s %12s\n", "", "CodinGame", "port");
    for (int k = 0; k < 4; k++)
        for (int p = 0; p < 2; p++) {
            bool eq = cg[k][p] == port[k][p];
            same &= eq;
            printf("%-18s p%d %12d %12d %s\n", kinds[k], p, cg[k][p], port[k][p], eq ? "" : "  <-- DIFFERENT");
        }
    for (int p = 0; p < 2; p++) {
        bool eq = cgScore[p] == g.score[p];
        same &= eq;
        printf("%-18s p%d %12d %12d %s\n", "final score", p, cgScore[p], g.score[p], eq ? "" : "  <-- DIFFERENT");
    }
    for (int p = 0; p < 2; p++) if (!g.endReason[p].empty()) printf("port: p%d %s\n", p, g.endReason[p].c_str());
    // after a kill, CodinGame still runs the other player's turn of that frame; the port stops before it
    for (int p = 0; p < 2; p++) same &= !ranOut && (next[p] == outs[p].size() || (g.ended && next[p] + 1 == outs[p].size()));
    printf(same ? "MATCH\n" : "MISMATCH\n");
    return same ? 0 : 1;
}
