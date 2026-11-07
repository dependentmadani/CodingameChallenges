// Local arena for TryAngle-Catch: plays two bot programs against each other on seeded maps.
//
//   arena --a "./main" --b "./main_old" --games 100 --jobs 6
//
// Each seed is played twice with the sides swapped. Bots are shell commands, so arguments
// work: --a "./main time 30". Results are from bot A's point of view.
//
// Options:
//   --a CMD, --b CMD     bot commands (required)
//   --games N            number of seeds (default 50); 2N games with --swap (default on)
//   --seed S             first seed (default 1)
//   --no-swap            play each seed once, A as player 0
//   --jobs J             games in parallel (default 4)
//   --league L           rules level: 2 = MOVE/SPAWN/ATTACK (Silver now, default), 3+ adds ADD_PATH/REMOVE_PATH
//   --turn-ms MS         turn time limit (default 50), --first-ms MS first turn limit (default 1000)
//   --time-scale F       multiply the limits before killing a bot for timeout (default 1.0)
//   --log FILE           one line per game
//   --trace DIR          full input/output transcript per game, DIR/seed_<s>_<a|b>first.txt
//   --stderr DIR         keep each bot's stderr in DIR (default: discarded)
//   --map SEED           print the map for SEED and exit
#include "referee.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

extern char** environ;
using namespace tac;
using Clock = std::chrono::steady_clock;

struct Options {
    std::string cmd[2];
    int games = 50, jobs = 4, league = 2;
    long long seed = 1;
    bool swap = true;
    double turnMs = 50, firstMs = 1000, timeScale = 1.0;
    std::string logFile, traceDir, stderrDir;
};

static std::mutex spawnMutex;  // pipes and spawn under one lock so no child inherits another game's pipes

struct Bot {
    pid_t pid = -1;
    int in = -1, out = -1;  // write to in, read from out
    std::string buf;
    double maxMs = 0, sumMs = 0, firstMs = 0;  // max and sum exclude the first turn
    int turns = 0, overLimit = 0;
    bool dead = false;

    bool start(const std::string& cmd, const std::string& stderrPath) {
        std::lock_guard<std::mutex> lock(spawnMutex);
        int pin[2], pout[2];
        if (pipe(pin) || pipe(pout)) return false;
        for (int fd : {pin[0], pin[1], pout[0], pout[1]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, pin[0], 0);
        posix_spawn_file_actions_adddup2(&fa, pout[1], 1);
        if (stderrPath.empty()) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        else posix_spawn_file_actions_addopen(&fa, 2, stderrPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
        std::string full = "exec " + cmd;
        const char* argv[] = {"sh", "-c", full.c_str(), nullptr};
        int rc = posix_spawn(&pid, "/bin/sh", &fa, &attr, (char* const*)argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        posix_spawnattr_destroy(&attr);
        close(pin[0]);
        close(pout[1]);
        in = pin[1];
        out = pout[0];
        if (rc != 0) { pid = -1; return false; }
        return true;
    }

    // send input, wait for one output line; false on timeout or crash
    bool turn(const std::string& input, bool first, double limitMs, double killMs, std::string& line) {
        if (dead) return false;
        const char* p = input.data();
        size_t left = input.size();
        while (left > 0) {
            ssize_t n = write(in, p, left);
            if (n <= 0) { dead = true; return false; }
            p += n; left -= (size_t)n;
        }
        auto t0 = Clock::now();
        while (true) {
            size_t nl = buf.find('\n');
            if (nl != std::string::npos) {
                line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                if (first) firstMs = ms;
                else { maxMs = std::max(maxMs, ms); sumMs += ms; turns++; }
                if (ms > limitMs) overLimit++;
                return true;
            }
            double waited = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            int wait = (int)std::ceil(killMs - waited);
            if (wait <= 0) { dead = true; return false; }
            pollfd pfd{out, POLLIN, 0};
            int rv = poll(&pfd, 1, wait);
            if (rv <= 0) { dead = true; return false; }
            char tmp[65536];
            ssize_t n = read(out, tmp, sizeof tmp);
            if (n <= 0) { dead = true; return false; }
            buf.append(tmp, (size_t)n);
        }
    }

    void stop() {
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        in = out = -1;
        if (pid > 0) {
            kill(pid, SIGKILL);
            int st;
            waitpid(pid, &st, 0);
            pid = -1;
        }
    }
};

struct Result {
    long long seed;
    bool aFirst;           // bot A was player 0
    int scoreA, scoreB;
    std::string reasonA, reasonB;
    int turns;
    double maxMsA, maxMsB, avgMsA, avgMsB, firstMsA, firstMsB;
    int overA, overB;
    int errA[ERROR_CODES], errB[ERROR_CODES];
    std::string firstErrA, firstErrB;
};

static const char* errName(int c) {
    switch (c) {
        case UNKNOWN_COMMAND: return "unknown";
        case NOT_AVAILABLE: return "league";
        case INVALID_NODE: return "badnode";
        case INVALID_TRIANGLE: return "badtri";
        case CANT_BE_APPLIED: return "cantapply";
        case CANT_ADD_PATH: return "cantpath";
        case POSITIVE_AMOUNT: return "amount";
        case NOT_NEAR_TRIANGLE: return "notnear";
    }
    return "?";
}

static Result playGame(const Options& o, long long seed, bool aFirst) {
    Game g(seed, o.league);
    Bot bots[2];  // by player index
    int botOf[2] = {aFirst ? 0 : 1, aFirst ? 1 : 0};  // player -> 0 for A, 1 for B
    for (int p = 0; p < 2; p++) {
        std::string err;
        if (!o.stderrDir.empty())
            err = o.stderrDir + "/seed_" + std::to_string(seed) + (aFirst ? "_afirst_" : "_bfirst_") + (botOf[p] == 0 ? "A" : "B") + ".txt";
        if (!bots[p].start(o.cmd[botOf[p]], err)) bots[p].dead = true;
    }
    std::ofstream trace;
    if (!o.traceDir.empty())
        trace.open(o.traceDir + "/seed_" + std::to_string(seed) + (aFirst ? "_afirst" : "_bfirst") + ".txt");
    Game::IO io = [&](int p, const std::string& input, bool first, std::string& out) {
        double limit = first ? o.firstMs : o.turnMs;
        bool ok = bots[p].turn(input, first, limit, limit * o.timeScale, out);
        if (trace.is_open() && p == 0) {
            char st[160];
            snprintf(st, sizeof st, "### T%d score %d-%d units %d-%d tris %d-%d\n", g.inputTurns, g.score[0], g.score[1], g.board.unitsOf(0),
                     g.board.unitsOf(1), g.board.trianglesOf(0), g.board.trianglesOf(1));
            trace << st;
        }
        if (trace.is_open()) {
            trace << "### frame " << g.frame << " player " << p << " (" << (botOf[p] == 0 ? "A" : "B") << ")\n" << input;
            trace << ">>> " << (ok ? out : std::string("<timeout or crash>")) << "\n";
        }
        return ok;
    };
    g.run(io);
    for (auto& b : bots) b.stop();

    Result r{};
    r.seed = seed;
    r.aFirst = aFirst;
    int pa = aFirst ? 0 : 1, pb = 1 - pa;
    r.scoreA = g.score[pa];
    r.scoreB = g.score[pb];
    r.reasonA = g.endReason[pa];
    r.reasonB = g.endReason[pb];
    r.turns = g.inputTurns;
    r.maxMsA = bots[pa].maxMs; r.maxMsB = bots[pb].maxMs;
    r.firstMsA = bots[pa].firstMs; r.firstMsB = bots[pb].firstMs;
    r.avgMsA = bots[pa].turns ? bots[pa].sumMs / bots[pa].turns : 0;
    r.avgMsB = bots[pb].turns ? bots[pb].sumMs / bots[pb].turns : 0;
    r.overA = bots[pa].overLimit; r.overB = bots[pb].overLimit;
    for (int c = 0; c < ERROR_CODES; c++) { r.errA[c] = g.errors[pa].count[c]; r.errB[c] = g.errors[pb].count[c]; }
    for (int c = 1; c < ERROR_CODES; c++) {
        if (r.firstErrA.empty() && r.errA[c]) r.firstErrA = g.errors[pa].firstMessage[c];
        if (r.firstErrB.empty() && r.errB[c]) r.firstErrB = g.errors[pb].firstMessage[c];
    }
    if (trace.is_open())
        trace << "### end: A " << r.scoreA << " " << r.reasonA << " | B " << r.scoreB << " " << r.reasonB << "\n";
    return r;
}

static std::string describe(const Result& r) {
    std::ostringstream s;
    s << "seed " << r.seed << (r.aFirst ? " A=p0" : " A=p1") << "  A " << r.scoreA << " - " << r.scoreB << " B  "
      << (r.scoreA > r.scoreB ? "WIN " : r.scoreA < r.scoreB ? "LOSS" : "DRAW") << "  turns " << r.turns;
    char buf[128];
    snprintf(buf, sizeof buf, "  ms(avg/max/first) A %.1f/%.1f/%.0f B %.1f/%.1f/%.0f", r.avgMsA, r.maxMsA, r.firstMsA, r.avgMsB, r.maxMsB, r.firstMsB);
    s << buf;
    if (r.overA || r.overB) s << "  over-limit A " << r.overA << " B " << r.overB;
    if (!r.reasonA.empty()) s << "  [A: " << r.reasonA << "]";
    if (!r.reasonB.empty()) s << "  [B: " << r.reasonB << "]";
    for (int c = 1; c < ERROR_CODES; c++) {
        if (r.errA[c]) s << "  A." << errName(c) << "=" << r.errA[c];
        if (r.errB[c]) s << "  B." << errName(c) << "=" << r.errB[c];
    }
    if (!r.firstErrA.empty()) s << "  A first error: " << r.firstErrA.substr(0, 80);
    if (!r.firstErrB.empty()) s << "  B first error: " << r.firstErrB.substr(0, 80);
    return s.str();
}

static void printMap(long long seed, int league) {
    Game g(seed, league);
    std::cout << g.getInput(true, 0, g.linkable());
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "missing value for " << k << "\n"; exit(2); }
            return argv[++i];
        };
        if (k == "--a") o.cmd[0] = val();
        else if (k == "--b") o.cmd[1] = val();
        else if (k == "--games") o.games = std::stoi(val());
        else if (k == "--seed") o.seed = std::stoll(val());
        else if (k == "--no-swap") o.swap = false;
        else if (k == "--jobs") o.jobs = std::stoi(val());
        else if (k == "--league") o.league = std::stoi(val());
        else if (k == "--turn-ms") o.turnMs = std::stod(val());
        else if (k == "--first-ms") o.firstMs = std::stod(val());
        else if (k == "--time-scale") o.timeScale = std::stod(val());
        else if (k == "--log") o.logFile = val();
        else if (k == "--trace") o.traceDir = val();
        else if (k == "--stderr") o.stderrDir = val();
        else if (k == "--map") { printMap(std::stoll(val()), o.league); return 0; }
        else { std::cerr << "unknown option " << k << "\n"; return 2; }
    }
    if (o.cmd[0].empty() || o.cmd[1].empty()) { std::cerr << "need --a CMD and --b CMD\n"; return 2; }

    std::vector<std::pair<long long, bool>> todo;
    for (int i = 0; i < o.games; i++) {
        todo.push_back({o.seed + i, true});
        if (o.swap) todo.push_back({o.seed + i, false});
    }
    std::vector<Result> results(todo.size());
    std::atomic<size_t> next{0};
    std::mutex outMutex;
    std::ofstream log;
    if (!o.logFile.empty()) log.open(o.logFile);
    size_t done = 0;
    auto worker = [&] {
        while (true) {
            size_t i = next++;
            if (i >= todo.size()) return;
            results[i] = playGame(o, todo[i].first, todo[i].second);
            std::lock_guard<std::mutex> lock(outMutex);
            done++;
            std::string line = describe(results[i]);
            if (log.is_open()) { log << line << "\n"; log.flush(); }
            std::cerr << "[" << done << "/" << todo.size() << "] " << line << "\n";
        }
    };
    std::vector<std::thread> threads;
    for (int j = 0; j < std::max(1, o.jobs); j++) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    int w = 0, d = 0, l = 0, crashA = 0, crashB = 0, overA = 0, overB = 0;
    double diff = 0, diff2 = 0, maxA = 0, maxB = 0, firstA = 0, firstB = 0;
    long long errA[ERROR_CODES] = {}, errB[ERROR_CODES] = {};
    for (auto& r : results) {
        if (r.scoreA > r.scoreB) w++; else if (r.scoreA < r.scoreB) l++; else d++;
        double x = r.scoreA - r.scoreB;
        diff += x; diff2 += x * x;
        crashA += !r.reasonA.empty();
        crashB += !r.reasonB.empty();
        overA += r.overA; overB += r.overB;
        maxA = std::max(maxA, r.maxMsA); maxB = std::max(maxB, r.maxMsB);
        firstA = std::max(firstA, r.firstMsA); firstB = std::max(firstB, r.firstMsB);
        for (int c = 0; c < ERROR_CODES; c++) { errA[c] += r.errA[c]; errB[c] += r.errB[c]; }
    }
    int n = (int)results.size();
    // win rate counts a draw as half a win; Wilson 95% interval on that rate
    double p = (w + 0.5 * d) / n, z = 1.96;
    double den = 1 + z * z / n, centre = (p + z * z / (2 * n)) / den;
    double half = z * std::sqrt(p * (1 - p) / n + z * z / (4.0 * n * n)) / den;
    double mean = diff / n, sd = n > 1 ? std::sqrt(std::max(0.0, (diff2 - diff * diff / n) / (n - 1))) : 0;
    printf("A: %s\nB: %s\n", o.cmd[0].c_str(), o.cmd[1].c_str());
    printf("games %d  A wins %d  draws %d  losses %d\n", n, w, d, l);
    printf("A win rate %.1f%%  (95%% CI %.1f%% - %.1f%%)\n", 100 * p, 100 * (centre - half), 100 * (centre + half));
    printf("mean score diff A-B %+.2f  (+/- %.2f)\n", mean, n > 1 ? 1.96 * sd / std::sqrt((double)n) : 0.0);
    printf("max turn ms (after turn 1) A %.1f  B %.1f   max first turn ms A %.0f  B %.0f\n", maxA, maxB, firstA, firstB);
    printf("turns over the limit: A %d  B %d\n", overA, overB);
    printf("games ended by kill (timeout, crash, bad command, eliminated): A %d  B %d\n", crashA, crashB);
    printf("command errors A:");
    for (int c = 1; c < ERROR_CODES; c++) if (errA[c]) printf(" %s=%lld", errName(c), errA[c]);
    printf("\ncommand errors B:");
    for (int c = 1; c < ERROR_CODES; c++) if (errB[c]) printf(" %s=%lld", errName(c), errB[c]);
    printf("\nRESULT games=%d wins=%d draws=%d losses=%d winrate=%.4f ci_lo=%.4f ci_hi=%.4f diff=%.3f\n", n, w, d, l, p,
           centre - half, centre + half, mean);
    return 0;
}
