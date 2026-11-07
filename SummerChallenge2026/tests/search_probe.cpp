// Expose the production simulator to the independent Python rules model.
#define main submission_main
#include "../main.cpp"
#undef main

int main() {
    Game game;
    if (!game.init() || !game.parse()) return 1;
    Game::SearchMove actions[2];
    for (auto &a : actions) {
        int regionId;
        if (!(std::cin >> a.count >> regionId)) return 2;
        if (a.count < 0 || a.count > 3) return 3;
        for (int i = 0; i < a.count; ++i) std::cin >> a.cells[i];
        if (regionId != -1)
            for (int r = 0; r < (int)game.regions.size(); ++r)
                if (game.regions[r].id == regionId) a.attack = r;
    }
    int decisionTurn;
    if (std::cin >> decisionTurn) game.turn = decisionTurn;
    auto s = game.searchState();
    game.searchStep(s, actions[0], actions[1]);
    std::cout << s.score << ' ' << s.rate << ' ' << s.terminal << '\n';
    for (int net : s.vulnerableIncome) std::cout << net << ' ';
    std::cout << s.safeIncome << '\n';
    for (int c = 0; c < (int)game.tiles.size(); ++c)
        std::cout << (int)s.track[c] << ' ' << game.simBlocked(s, c) << '\n';
}
