// Diagnostic entry point; production main.cpp stays unchanged.
#define main submission_main
#include "../main.cpp"
#undef main

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    Game game;
    if (!game.init()) return 0;
    while (game.parse()) {
        auto started = std::chrono::steady_clock::now();
        game.gameTurn();
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::cerr << "decision_ms=" << ms << '\n';
    }
}
