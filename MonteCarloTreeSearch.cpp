#include <iostream>
#include <cmath>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

/**
 * Auto-generated code below aims at helping you parse
 * the standard input according to the problem statement.
 **/

using namespace std;

struct Node {
    int visits = 0;
    double scoreSum = 0.0;
    map<char, Node*> children;

    ~Node() {
        for (auto& [_, ch] : children) delete ch;
    }

    // Only expand the first unseen move in this playout
    void addPlayout(const string& path, double score) {
        Node* node = this;
        node->visits++;
        node->scoreSum += score;

        for (char move : path) {
            auto it = node->children.find(move);
            if (it == node->children.end()) {
                Node* child = new Node();
                child->visits++;
                child->scoreSum += score;
                node->children[move] = child;
                break;
            }
            node = it->second;
            node->visits++;
            node->scoreSum += score;
        }
    }

    // Descend by UCB1 until you hit a node with no children
    string selectBestPath(double C) {
        string path;
        Node* node = this;

        while (!node->children.empty()) {
            double bestUCT = -numeric_limits<double>::infinity();
            char bestMove = 0;
            Node* bestChild = nullptr;

            for (auto& [move, child] : node->children) {
                double avg   = child->scoreSum / child->visits;
                double bonus = C * sqrt(log(node->visits) / child->visits);
                double uct   = avg + bonus;

                if (uct > bestUCT ||
                    (fabs(uct - bestUCT) < 1e-8 && move < bestMove))
                {
                    bestUCT   = uct;
                    bestMove  = move;
                    bestChild = child;
                }
            }
            path += bestMove;
            node = bestChild;
        }

        return path;
    }
};

int main() {
    int N;
    double C;
    cin >> N >> C;
    cin.ignore();

    Node root;
    for (int i = 0; i < N; ++i) {
        string line, moves;
        double score;
        getline(cin, line);
        stringstream ss(line);
        ss >> moves >> score;
        root.addPlayout(moves, score);
    }

    cout << root.selectBestPath(C) << '\n';
    return 0;
}