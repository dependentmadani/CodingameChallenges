// Prints the map of a seed in the same format as the "options" lines of a CodinGame game log
// (nodes=x,y_..., edges=a,b_..., units=u0,u1_...), to compare the generator with CodinGame's.
#include "referee.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: mapcheck SEED\n"); return 2; }
    int score[2] = {0, 0};
    tac::Board b;
    b.generate(std::strtoll(argv[1], nullptr, 10), score);
    std::string nodes, edges, units;
    for (auto& n : b.nodes) {
        nodes += (nodes.empty() ? "" : "_") + std::to_string(n.x) + "," + std::to_string(n.y);
        units += (units.empty() ? "" : "_") + std::to_string(n.units[0]) + "," + std::to_string(n.units[1]);
        for (int m : n.nb) if (m > n.id) edges += (edges.empty() ? "" : "_") + std::to_string(n.id) + "," + std::to_string(m);
    }
    printf("nodes=%s\nedges=%s\nunits=%s\n", nodes.c_str(), edges.c_str(), units.c_str());
}
