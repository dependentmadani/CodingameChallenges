// Self-tests for the TryAngle-Catch referee port.
//   1. map invariants over many seeds (symmetry, no crossing edges, connectivity, triangle count)
//   2. random games with sanity checks after every frame
//   3. mirror games: player 1 copies player 0 on the mirrored houses, so the game must stay symmetric
#include "referee.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>

using namespace tac;

static int failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            if (failures++ < 20) {                                    \
                fprintf(stderr, "FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
                fprintf(stderr, __VA_ARGS__);                         \
                fprintf(stderr, "\n");                                \
            }                                                         \
        }                                                             \
    } while (0)

static void checkBoardShape(const Board& b, long long seed, bool symmetric) {
    int N = (int)b.nodes.size();
    for (int v = 0; v < N; v++) {
        const Node& n = b.nodes[v];
        CHECK(std::is_sorted(n.nb.begin(), n.nb.end()), "seed %lld node %d neighbors unsorted", seed, v);
        for (int u : n.nb) {
            CHECK(u != v && contains(b.nodes[u].nb, v), "seed %lld edge %d-%d not symmetric", seed, v, u);
            CHECK(b.dist(u, v) <= MAX_PATH_LENGTH, "seed %lld edge %d-%d too long", seed, v, u);
            if (symmetric) CHECK(contains(b.nodes[b.mirror(v)].nb, b.mirror(u)), "seed %lld edge %d-%d has no mirror", seed, v, u);
        }
        if (symmetric) {
            const Node& m = b.nodes[b.mirror(v)];
            CHECK(m.x == WIDTH - 1 - (n.x - SIDE_BORDER) + SIDE_BORDER && m.y == HEIGHT - 1 - (n.y - TOP_BORDER) + TOP_BORDER,
                  "seed %lld node %d mirror position", seed, v);
        }
    }
    // no two edges cross
    std::vector<std::pair<int, int>> E;
    for (int v = 0; v < N; v++) for (int u : b.nodes[v].nb) if (u > v) E.push_back({v, u});
    for (size_t i = 0; i < E.size(); i++)
        for (size_t j = i + 1; j < E.size(); j++)
            CHECK(!b.isCrossing(E[i].first, E[i].second, E[j].first, E[j].second), "seed %lld edges cross", seed);
    // triangles: listed in (a,b,c) order, all three edges exist, empty inside, and every such triangle is listed
    std::set<std::array<int, 3>> listed;
    std::array<int, 3> prev{-1, -1, -1};
    for (int t : b.tris) {
        const Tri& T = b.pool[t];
        std::array<int, 3> k{T.n[0], T.n[1], T.n[2]};
        CHECK(k[0] < k[1] && k[1] < k[2], "seed %lld triangle corners unsorted", seed);
        CHECK(prev < k, "seed %lld triangles out of order", seed);
        prev = k;
        CHECK(contains(b.nodes[k[0]].nb, k[1]) && contains(b.nodes[k[1]].nb, k[2]) && contains(b.nodes[k[0]].nb, k[2]),
              "seed %lld triangle without edges", seed);
        for (int v = 0; v < N; v++) CHECK(!b.isInside(v, k[0], k[1], k[2]), "seed %lld house inside triangle", seed);
        listed.insert(k);
    }
    for (int a = 0; a < N; a++)
        for (int c2 : b.nodes[a].nb)
            for (int c3 : b.nodes[c2].nb) {
                if (!(a < c2 && c2 < c3 && contains(b.nodes[a].nb, c3))) continue;
                bool inside = false;
                for (int v = 0; v < N; v++) inside |= b.isInside(v, a, c2, c3);
                if (!inside) CHECK(listed.count({a, c2, c3}), "seed %lld triangle missing", seed);
            }
}

static void testMaps(int count) {
    std::map<int, int> nodeCounts;
    long long triSum = 0, edgeSum = 0;
    for (long long seed = 1; seed <= count; seed++) {
        int score[2] = {0, 0};
        Board b;
        b.generate(seed, score);
        nodeCounts[(int)b.nodes.size()]++;
        CHECK((int)b.tris.size() >= MIN_TRIANGLE_COUNT, "seed %lld only %zu triangles", seed, b.tris.size());
        CHECK(b.isStronglyConnected(), "seed %lld not strongly connected", seed);
        CHECK(b.unitsOf(0) == 3 && b.unitsOf(1) == 3, "seed %lld start units", seed);
        CHECK(score[0] == score[1], "seed %lld start score", seed);
        checkBoardShape(b, seed, true);
        for (auto& n : b.nodes) edgeSum += n.nb.size();
        triSum += b.tris.size();
    }
    // a negative seed (CodinGame seeds can be negative)
    int score[2] = {0, 0};
    Board b; b.generate(-123456789LL, score);
    checkBoardShape(b, -123456789LL, true);
    printf("maps: %d seeds, node counts:", count);
    for (auto& kv : nodeCounts) printf(" %d nodes x%d", kv.first, kv.second);
    printf(", avg %.1f paths, avg %.1f triangles\n", edgeSum / 2.0 / count, (double)triSum / count);
}

// random player: mostly sensible commands from the real state, plus some junk
static std::string randomCommands(const Game& g, int p, std::mt19937& rng, bool junk) {
    const Board& b = g.board;
    std::vector<std::string> cmds;
    int N = (int)b.nodes.size();
    for (int v = 0; v < N; v++) {
        const Node& n = b.nodes[v];
        if (n.units[p] > 0 && rng() % 3) {
            int to = rng() % 4 == 0 ? (int)(rng() % N) : n.nb.empty() ? v : n.nb[rng() % n.nb.size()];
            int amount = 1 + rng() % std::max(1, n.units[p]);
            cmds.push_back("MOVE " + std::to_string(v) + " " + std::to_string(to) + " " + std::to_string(amount));
        }
    }
    for (int t : b.tris) {
        const Tri& T = b.pool[t];
        if (T.owner != p) continue;
        std::string tri = std::to_string(T.n[0]) + " " + std::to_string(T.n[1]) + " " + std::to_string(T.n[2]);
        int r = rng() % 10;
        if (r < 5) cmds.push_back("SPAWN " + std::to_string(T.n[rng() % 3]) + " " + std::to_string(T.n[(rng() % 2) ? 1 : 2]) + " " +
                                  std::to_string(T.n[0]));
        else if (r < 7) {
            int c = T.n[rng() % 3];
            const Node& n = b.nodes[c];
            if (!n.nb.empty()) cmds.push_back("ATTACK " + tri + " " + std::to_string(n.nb[rng() % n.nb.size()]));
        } else if (r < 8) {
            cmds.push_back("ADD_PATH " + std::to_string(T.n[0]) + " " + std::to_string(T.n[1]) + " " + std::to_string(T.n[2]) + " " +
                           std::to_string(rng() % N));
        } else if (r < 9) {
            const Node& n = b.nodes[T.n[0]];
            if (!n.nb.empty())
                cmds.push_back("REMOVE_PATH " + std::to_string(T.n[0]) + " " + std::to_string(T.n[1]) + " " + std::to_string(T.n[2]) + " " +
                               std::to_string(n.nb[rng() % n.nb.size()]));
        }
    }
    if (junk) {
        const char* j[] = {"MOVE 99 1 1", "SPAWN 1 2 3", "MOVE 0 1 -2", "WAIT", "  ", "MSG hello", "LINE 0 0 10 10", "ATTACK 1 2 3 4"};
        cmds.push_back(j[rng() % 8]);
    }
    std::shuffle(cmds.begin(), cmds.end(), rng);
    std::string out;
    for (auto& c : cmds) out += c + ";";
    return out.empty() ? "WAIT" : out;
}

static void checkState(const Game& g, long long seed) {
    for (auto& n : g.board.nodes) CHECK(n.units[0] >= 0 && n.units[1] >= 0, "seed %lld negative units", seed);
    checkBoardShape(g.board, seed, false);
    for (int t : g.board.tris) {
        const Tri& T = g.board.pool[t];
        CHECK(T.owner >= -1 && T.owner <= 1, "seed %lld bad owner", seed);
    }
}

static void testRandomGames(int count) {
    long long frames = 0, inputs = 0;
    int ends[3] = {0, 0, 0};
    for (long long seed = 1; seed <= count; seed++) {
        std::mt19937 rng((unsigned)seed * 7919u);
        Game g(seed);
        int lastScore[2] = {g.score[0], g.score[1]};
        Game::IO io = [&](int p, const std::string& input, bool first, std::string& out) {
            CHECK(!input.empty(), "empty input");
            if (first) CHECK(input.rfind(std::to_string(g.board.nodes.size()) + "\n", 0) == 0, "first input must start with N");
            checkState(g, seed);
            for (int q = 0; q < 2; q++) {
                CHECK(g.score[q] >= lastScore[q], "seed %lld score went down", seed);
                lastScore[q] = g.score[q];
            }
            out = randomCommands(g, p, rng, true);
            return true;
        };
        g.run(io);
        frames += g.frame;
        inputs += g.inputTurns;
        CHECK(g.inputTurns <= 200, "seed %lld %d input turns", seed, g.inputTurns);
        if (!g.ended) CHECK(g.inputTurns == 200, "seed %lld finished after %d input turns", seed, g.inputTurns);
        ends[g.score[0] > g.score[1] ? 0 : g.score[0] < g.score[1] ? 1 : 2]++;
    }
    printf("random games: %d games, %.1f frames and %.1f input turns per game, p0 wins %d, p1 wins %d, draws %d\n", count,
           (double)frames / count, (double)inputs / count, ends[0], ends[1], ends[2]);
}

// Player 1 plays player 0's commands on mirrored houses. Only one-step moves and spawns are used:
// a longer move follows the lowest-id shortest path, which is not mirror symmetric.
static void testMirrorGames(int count) {
    int checked = 0;
    for (long long seed = 1; seed <= count; seed++) {
        std::mt19937 rng((unsigned)seed * 104729u);
        Game g(seed);
        std::string lastP0;
        Game::IO io = [&](int p, const std::string&, bool, std::string& out) {
            const Board& b = g.board;
            for (int v = 0; v < (int)b.nodes.size(); v++) {
                int m = b.mirror(v);
                CHECK(b.nodes[v].units[0] == b.nodes[m].units[1], "seed %lld frame %d house %d units not mirrored", seed, g.frame, v);
            }
            CHECK(g.score[0] == g.score[1], "seed %lld frame %d scores %d vs %d", seed, g.frame, g.score[0], g.score[1]);
            if (p == 0) {
                std::vector<std::string> cmds;
                std::vector<std::string> mirrored;
                for (int v = 0; v < (int)b.nodes.size(); v++) {
                    const Node& n = b.nodes[v];
                    if (n.units[0] > 0 && !n.nb.empty() && rng() % 2) {
                        int to = n.nb[rng() % n.nb.size()];
                        int amount = 1 + rng() % n.units[0];
                        cmds.push_back("MOVE " + std::to_string(v) + " " + std::to_string(to) + " " + std::to_string(amount));
                        mirrored.push_back("MOVE " + std::to_string(b.mirror(v)) + " " + std::to_string(b.mirror(to)) + " " + std::to_string(amount));
                    }
                }
                for (int t : b.tris) {
                    const Tri& T = b.pool[t];
                    if (T.owner != 0 || rng() % 4 == 0) continue;
                    int k = rng() % 3;
                    cmds.push_back("SPAWN " + std::to_string(T.n[k]) + " " + std::to_string(T.n[(k + 1) % 3]) + " " + std::to_string(T.n[(k + 2) % 3]));
                    mirrored.push_back("SPAWN " + std::to_string(b.mirror(T.n[k])) + " " + std::to_string(b.mirror(T.n[(k + 1) % 3])) + " " +
                                       std::to_string(b.mirror(T.n[(k + 2) % 3])));
                }
                out.clear(); lastP0.clear();
                for (auto& c : cmds) out += c + ";";
                for (auto& c : mirrored) lastP0 += c + ";";
            } else {
                out = lastP0;
            }
            checked++;
            return true;
        };
        g.run(io);
        CHECK(g.score[0] == g.score[1], "seed %lld mirror game final scores %d vs %d", seed, g.score[0], g.score[1]);
    }
    printf("mirror games: %d games, %d symmetric positions checked\n", count, checked);
}

static void testParsing() {
    Game g(7);
    // the 3 start houses of player 0
    std::vector<int> mine;
    for (auto& n : g.board.nodes) if (n.units[0]) mine.push_back(n.id);
    int a = mine[0], nb = g.board.nodes[a].nb[0];
    auto run = [&](const std::string& line) {
        Game h(7);
        h.parseTasks(0, line);
        return h;
    };
    { auto h = run("MOVE " + std::to_string(a) + " " + std::to_string(nb) + " 1"); CHECK(h.tasks.size() == 1 && h.critical[0].empty(), "valid move"); }
    { auto h = run("move " + std::to_string(a) + "   " + std::to_string(nb) + " 1  "); CHECK(h.tasks.size() == 1, "case-insensitive move"); }
    { auto h = run("MOVE 77 1 1"); CHECK(h.errors[0].count[INVALID_NODE] == 1 && h.critical[0].empty(), "invalid node is not critical"); }
    { auto h = run("MOVE 1 2"); CHECK(!h.critical[0].empty(), "missing amount is an unknown command"); }
    { auto h = run("MOVE 1 2 99999999999"); CHECK(!h.critical[0].empty(), "int overflow is an unknown command"); }
    { auto h = run("FOO"); CHECK(!h.critical[0].empty(), "unknown command"); }
    { auto h = run("WAIT;MSG hi there;;LINE 1 2 3 4;TEXT 1 2 hello world;CIRCLE 1 1 5"); CHECK(h.tasks.empty() && h.critical[0].empty(), "no-op commands"); }
    { auto h = run(" MSG hi"); CHECK(!h.critical[0].empty(), "MSG must start the command"); }
    { Game h(7); h.league = 1; h.parseTasks(0, "ATTACK 1 2 3 4"); CHECK(h.critical[0].empty() && h.tasks.empty(), "league lock is not critical"); }
    printf("parsing: ok\n");
}

int main(int argc, char** argv) {
    int maps = argc > 1 ? atoi(argv[1]) : 300;
    int games = argc > 2 ? atoi(argv[2]) : 100;
    testParsing();
    testMaps(maps);
    testRandomGames(games);
    testMirrorGames(games);
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all self-tests passed\n");
    return 0;
}
