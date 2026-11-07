// Differential test: the bot's fast simulator (bot_v2.cpp, namespace sim) against the referee port.
// Random games are played in the referee; at many positions both engines get the same random
// commands for one turn, and the resulting units, triangle owners, capture flags and scores must match.
#include "referee.hpp"
#define NO_MAIN
#include "../bot_v2.cpp"
#include <random>

static std::string tri3(int t) { return std::to_string(sim::TC[t][0]) + " " + std::to_string(sim::TC[t][1]) + " " + std::to_string(sim::TC[t][2]); }

struct Cmds {
    std::string line;
    std::vector<std::array<int, 2>> atk;      // tri, target
    std::vector<std::array<int, 3>> mv;       // from, to, amount
    std::vector<std::array<int, 2>> sp;       // tri, corner house
};

// random valid-syntax commands for player p from the referee state (biased toward meaningful ones)
static Cmds randomCmds(const tac::Game& g, int p, std::mt19937& rng) {
    Cmds c;
    const auto& b = g.board;
    int Nn = (int)b.nodes.size(), Tn = (int)b.tris.size();
    std::vector<std::string> parts;
    std::vector<int> order;
    // attacks: mostly from own triangles, target adjacent to a corner
    int na = rng() % 5;
    for (int i = 0; i < na; i++) {
        int t = rng() % Tn;
        if (rng() % 4) for (int tries = 0; tries < 20; tries++) { int t2 = rng() % Tn; if (b.pool[b.tris[t2]].owner == p) { t = t2; break; } }
        std::vector<int> adjv, hot;
        for (int v = 0; v < Nn; v++) if (sim::TADJ[t] >> v & 1) { adjv.push_back(v); if (b.nodes[v].units[1 - p]) hot.push_back(v); }
        int tg = !hot.empty() && rng() % 4 ? hot[rng() % hot.size()] : adjv[rng() % adjv.size()];
        c.atk.push_back({t, tg});
        parts.push_back("ATTACK " + tri3(t) + " " + std::to_string(tg));
    }
    // moves: from houses with units (sometimes empty ones), anywhere, random amounts
    for (int v = 0; v < Nn; v++) {
        if (b.nodes[v].units[p] == 0 && rng() % 20) continue;
        int k = 1 + rng() % 3;
        for (int i = 0; i < k; i++) {
            int to = rng() % 3 == 0 ? (int)(rng() % Nn) : (b.nodes[v].nb.empty() ? v : b.nodes[v].nb[rng() % b.nodes[v].nb.size()]);
            int amt = 1 + rng() % 4;
            c.mv.push_back({v, to, amt});
            parts.push_back("MOVE " + std::to_string(v) + " " + std::to_string(to) + " " + std::to_string(amt));
        }
    }
    // spawns: any triangle, any corner (invalid ones must be ignored by both)
    int ns = rng() % 6;
    for (int i = 0; i < ns; i++) {
        int t = rng() % Tn;
        if (rng() % 2) for (int tries = 0; tries < 10; tries++) { int t2 = rng() % Tn; if (b.pool[b.tris[t2]].owner == p) { t = t2; break; } }
        int k = rng() % 3;
        c.sp.push_back({t, sim::TC[t][k]});
        parts.push_back("SPAWN " + std::to_string(sim::TC[t][k]) + " " + std::to_string(sim::TC[t][(k + 1) % 3]) + " " + std::to_string(sim::TC[t][(k + 2) % 3]));
    }
    // the referee keeps the order within each action type, so shuffle across types only
    std::shuffle(parts.begin(), parts.end(), rng);
    // rebuild per-type order from the shuffled line so both engines see the same order
    c.atk.clear(); c.mv.clear(); c.sp.clear();
    for (auto& s : parts) {
        std::istringstream is(s);
        std::string kw; is >> kw;
        if (kw == "ATTACK") {
            int a, bb, cc, tg; is >> a >> bb >> cc >> tg;
            int t = -1;
            for (int q = 0; q < sim::T; q++) if (sim::TC[q][0] == a && sim::TC[q][1] == bb && sim::TC[q][2] == cc) t = q;
            c.atk.push_back({t, tg});
        } else if (kw == "MOVE") {
            int f, to, am; is >> f >> to >> am; c.mv.push_back({f, to, am});
        } else {
            int x, y, z; is >> x >> y >> z;
            int t = -1;
            for (int q = 0; q < sim::T; q++) {
                uint64_t m = sim::TMASK[q];
                if ((m >> x & 1) && (m >> y & 1) && (m >> z & 1)) { t = q; break; }
            }
            c.sp.push_back({t, x});
        }
        c.line += s + ";";
    }
    return c;
}

// copy the referee position into the simulator (player 0's point of view = absolute)
static void loadSim(const tac::Game& g, sim::St& s) {
    const auto& b = g.board;
    sim::N = (int)b.nodes.size();
    for (int v = 0; v < sim::N; v++) {
        sim::NB[v] = 0;
        for (int n : b.nodes[v].nb) sim::NB[v] |= 1ULL << n;
        s.u[0][v] = (int16_t)b.nodes[v].units[0];
        s.u[1][v] = (int16_t)b.nodes[v].units[1];
    }
    sim::buildGraph();
    sim::T = (int)b.tris.size();
    s.cc[0] = s.cc[1] = 0;
    for (int i = 0; i < sim::T; i++) {
        const auto& tr = b.pool[b.tris[i]];
        for (int k = 0; k < 3; k++) sim::TC[i][k] = tr.n[k];
        s.own[i] = (int8_t)tr.owner;
        s.iown[i] = (int8_t)tr.initialOwner;
        if (tr.cc[0]) s.cc[0] |= 1ULL << i;
        if (tr.cc[1]) s.cc[1] |= 1ULL << i;
    }
    sim::buildTriangles();
    s.score[0] = g.score[0];
    s.score[1] = g.score[1];
}

static int fails = 0;
static bool compare(const tac::Game& g, const sim::St& s, long long seed, int turn) {
    const auto& b = g.board;
    std::string why;
    for (int v = 0; v < sim::N; v++)
        for (int p = 0; p < 2; p++)
            if (s.u[p][v] != b.nodes[v].units[p]) why += " units[" + std::to_string(p) + "][" + std::to_string(v) + "] " + std::to_string(s.u[p][v]) + " vs " + std::to_string(b.nodes[v].units[p]);
    for (int i = 0; i < sim::T; i++) {
        const auto& tr = b.pool[b.tris[i]];
        if (s.own[i] != tr.owner) why += " owner[" + std::to_string(i) + "]";
        if (s.iown[i] != tr.initialOwner) why += " initialOwner[" + std::to_string(i) + "]";
        for (int p = 0; p < 2; p++) if (((s.cc[p] >> i) & 1) != (tr.cc[p] ? 1u : 0u)) why += " cc[" + std::to_string(p) + "][" + std::to_string(i) + "]";
    }
    if (s.score[0] != g.score[0] || s.score[1] != g.score[1]) why += " score";
    if (!why.empty() && fails++ < 10) printf("MISMATCH seed %lld turn %d:%s\n", seed, turn, why.substr(0, 300).c_str());
    return why.empty();
}

int main(int argc, char** argv) {
    int games = argc > 1 ? atoi(argv[1]) : 200;
    long long checks = 0, attacksApplied = 0, surrounds = 0;
    for (long long seed = 1; seed <= games; seed++) {
        std::mt19937 rng((unsigned)seed * 2654435761u);
        tac::Game g(seed * 977 - 50000, 2);
        // play the game with random commands, checking every turn
        for (int turn = 1; turn <= 120 && !g.ended; turn++) {
            if (g.board.unitsOf(0) + g.board.unitsOf(1) == 0) break;
            sim::St s;
            loadSim(g, s);
            Cmds c0 = randomCmds(g, 0, rng), c1 = randomCmds(g, 1, rng);
            // referee: parse both, run task frames, then finalize frames
            tac::Game r = g;
            r.parseTasks(0, c0.line);
            r.parseTasks(1, c1.line);
            r.applyActions();
            while (r.hasTasks()) r.applyActions();
            int before = r.board.statSurrounded[0] + r.board.statSurrounded[1];
            while (r.board.finalizeTurn(r.score)) {}
            surrounds += r.board.statSurrounded[0] + r.board.statSurrounded[1] > before;
            attacksApplied += r.statAttacked[0] + r.statAttacked[1] > g.statAttacked[0] + g.statAttacked[1];
            // simulator
            sim::startTurn(s);
            int at0[64], av0[64], at1[64], av1[64], n0 = 0, n1 = 0;
            for (auto& a : c0.atk) if (a[0] >= 0) { at0[n0] = a[0]; av0[n0++] = a[1]; }
            for (auto& a : c1.atk) if (a[0] >= 0) { at1[n1] = a[0]; av1[n1++] = a[1]; }
            sim::attackPhase(s, at0, av0, n0, at1, av1, n1);
            for (int p = 0; p < 2; p++) for (auto& m : (p ? c1 : c0).mv) sim::moveCmd(s, p, m[0], m[1], m[2]);
            for (int p = 0; p < 2; p++) for (auto& sp : (p ? c1 : c0).sp) if (sp[0] >= 0) sim::spawnCmd(s, p, sp[0], sp[1]);
            sim::finalize(s);
            checks++;
            if (!compare(r, s, seed, turn)) break;
            g = r;
            g.board.killedSurrounded = true;  // as after a real finalize
        }
    }
    printf("%lld turns compared, %lld with attacks, %lld with surround kills, %d mismatches\n", checks, attacksApplied, surrounds, fails);
    return fails ? 1 : 0;
}
