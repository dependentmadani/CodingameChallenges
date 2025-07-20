#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <set>
#include <climits>

using namespace std;

struct COORDS {
    int x, y;
    bool operator==(const COORDS& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const COORDS& other) const {
        return x != other.x || y != other.y;
    }
    bool operator<(const COORDS& other) const {
        return x < other.x || (x == other.x && y < other.y);
    }
};

struct GAME {
    int my_id;
    int agent_count;
    int my_agent_count;
    int width;
    int height;
    int myPoints = 0;
    int oppPoints = 0;
    int turn = 0;
};

struct TILE {
    int type;
};

struct AGENT {
    COORDS c;
    int id;
    int player;
    int cooldown;
    int range;
    int power;
    int bombs;
    int wetness;
    bool mine;
    COORDS target = {-1, -1};
    COORDS nextMove = {-1, -1};
    bool dead = false;
    string move = "";
    COORDS startC = {-1, -1};
};

struct COVER {
    COORDS c;
    int type;
};

int distance(const COORDS& c1, const COORDS& c2) {
    return abs(c1.x - c2.x) + abs(c1.y - c2.y);
}

vector<COVER> findCovers(const GAME& game, const vector<vector<TILE>>& map) {
    vector<COVER> covers;
    for (int x = 0; x < game.width; x++) {
        for (int y = 0; y < game.height; y++) {
            if (map[x][y].type != 0) {
                covers.push_back({{x, y}, map[x][y].type});
            }
        }
    }
    return covers;
}

COORDS findTarget(GAME& game, vector<vector<TILE>>& map, vector<COVER>& covers, 
                 unordered_map<int, AGENT>& agent, int id, set<COORDS>& reservedTargets) {
    COORDS best_target = {-1, -1};
    int best_score = INT_MIN;
    COORDS center = {game.width/2, game.height/2};

    for (const auto& cover : covers) {
        COORDS pos = cover.c;
        COORDS candidate;
        
        // Choose side based on starting position
        if (agent[id].startC.x < game.width/2) {
            candidate = {max(0, pos.x - 1), pos.y};
        } else {
            candidate = {min(game.width-1, pos.x + 1), pos.y};
        }

        // Validate candidate position
        if (candidate.x < 0 || candidate.x >= game.width || 
            candidate.y < 0 || candidate.y >= game.height) {
            continue;
        }
        
        if (map[candidate.x][candidate.y].type != 0) {
            continue;
        }
        
        // Skip reserved targets
        if (reservedTargets.find(candidate) != reservedTargets.end()) {
            continue;
        }

        // Calculate score (prioritize center and cover quality)
        int dist_to_center = distance(candidate, center);
        int cover_quality = cover.type; // 1 for low, 2 for high
        int score = -dist_to_center * 10 + cover_quality * 100;

        if (score > best_score) {
            best_score = score;
            best_target = candidate;
        }
    }

    // Fallback to center position
    if (best_target.x == -1) {
        best_target = {game.width/2, game.height/2};
    }
    
    // Reserve target
    if (best_target.x != -1) {
        reservedTargets.insert(best_target);
    }
    
    return best_target;
}

COORDS selectRoute(GAME& game, vector<vector<TILE>>& map, 
                  const vector<vector<int>>& reservedMap,
                  COORDS start, COORDS end) {
    // If already at target, don't move
    if (start == end) {
        return start;
    }

    // BFS setup
    vector<vector<int>> dist(game.width, vector<int>(game.height, INT_MAX));
    vector<vector<COORDS>> parent(game.width, vector<COORDS>(game.height, {-1, -1}));
    queue<COORDS> q;
    
    // Directions: up, right, down, left
    vector<COORDS> dirs = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    
    dist[start.x][start.y] = 0;
    q.push(start);
    
    while (!q.empty()) {
        COORDS cur = q.front();
        q.pop();
        
        // Stop if reached target
        if (cur == end) {
            break;
        }
        
        // Explore neighbors
        for (const auto& dir : dirs) {
            COORDS next = {cur.x + dir.x, cur.y + dir.y};
            
            // Check boundaries
            if (next.x < 0 || next.x >= game.width || 
                next.y < 0 || next.y >= game.height) {
                continue;
            }
            
            // Check for obstacles and reservations
            if (map[next.x][next.y].type != 0 || reservedMap[next.x][next.y] != 0) {
                continue;
            }
            
            // Update distance if shorter path found
            int new_dist = dist[cur.x][cur.y] + 1;
            if (new_dist < dist[next.x][next.y]) {
                dist[next.x][next.y] = new_dist;
                parent[next.x][next.y] = cur;
                q.push(next);
            }
        }
    }
    
    // No valid path found
    if (dist[end.x][end.y] == INT_MAX) {
        return start;
    }
    
    // Reconstruct path to find first step
    COORDS step = end;
    while (parent[step.x][step.y] != start) {
        step = parent[step.x][step.y];
    }
    return step;
}

vector<vector<int>> buildReservedMap(GAME& game, unordered_map<int, AGENT>& agent) {
    vector<vector<int>> reservedMap(game.width, vector<int>(game.height, 0));
    
    for (auto& kv : agent) {
        AGENT& a = kv.second;
        if (!a.dead) {
            // Reserve current position for enemies
            if (!a.mine) {
                reservedMap[a.c.x][a.c.y] = 2;
            }
            // Reserve next move for allies
            else if (a.nextMove.x != -1) {
                reservedMap[a.nextMove.x][a.nextMove.y] = 1;
            }
        }
    }
    
    return reservedMap;
}

int calculateShotDamage(AGENT& shooter, AGENT& target, const vector<vector<TILE>>& map, GAME& game) {
    int dist = distance(shooter.c, target.c);
    int damage = shooter.power;
    
    // Adjust for range
    if (dist > shooter.range) {
        if (dist > shooter.range * 2) {
            return 0; // Out of range
        }
        damage = shooter.power / 2;
    }
    
    // Cover detection
    bool has_cover = false;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            int nx = target.c.x + dx;
            int ny = target.c.y + dy;
            if (nx >= 0 && nx < game.width && 
                ny >= 0 && ny < game.height) {
                if (map[nx][ny].type != 0) {
                    has_cover = true;
                    break;
                }
            }
        }
    }
    
    // Apply cover reduction
    if (has_cover) {
        damage = max(1, damage * 3 / 4);
    }
    
    return damage;
}

void findNextAction(GAME& game, unordered_map<int, AGENT>& agent, int id, 
                   const vector<vector<TILE>>& map, 
                   const vector<vector<int>>& reservedMap) {
    AGENT& a = agent[id];
    
    // Bomb throwing logic
    if (a.bombs > 0) {
        vector<COORDS> bombCandidates;
        for (int dx = -4; dx <= 4; dx++) {
            for (int dy = -4; dy <= 4; dy++) {
                if (abs(dx) + abs(dy) > 4) continue;
                COORDS pos = {a.c.x + dx, a.c.y + dy};
                
                // Validate position
                if (pos.x < 0 || pos.x >= game.width || 
                    pos.y < 0 || pos.y >= game.height) {
                    continue;
                }
                
                bombCandidates.push_back(pos);
            }
        }
        
        // Evaluate bomb candidates
        COORDS bestBomb = {-1, -1};
        int bestScore = -1;
        
        for (const COORDS& pos : bombCandidates) {
            int enemyCount = 0;
            int allyCount = 0;
            
            // Check 3x3 area around bomb
            for (int bx = max(0, pos.x-1); bx <= min(game.width-1, pos.x+1); bx++) {
                for (int by = max(0, pos.y-1); by <= min(game.height-1, pos.y+1); by++) {
                    if (reservedMap[bx][by] == 2) {
                        enemyCount++;
                    } else if (reservedMap[bx][by] == 1) {
                        allyCount++;
                    }
                }
            }
            
            // Score bombs that hit enemies without hitting allies
            if (enemyCount > 0 && allyCount == 0) {
                int score = enemyCount * 10 - distance(a.c, pos);
                if (score > bestScore) {
                    bestScore = score;
                    bestBomb = pos;
                }
            }
        }
        
        if (bestBomb.x != -1) {
            a.move += ";THROW " + to_string(bestBomb.x) + " " + to_string(bestBomb.y);
            return;
        }
    }
    
    // Shooting logic
    if (a.cooldown == 0) {
        int bestTarget = -1;
        int bestDamage = 0;
        
        for (auto& kv : agent) {
            AGENT& target = kv.second;
            if (!target.dead && !target.mine) {
                int damage = calculateShotDamage(a, target, map, game);
                int potential = target.wetness + damage;
                
                // Prioritize kills
                if (potential >= 100) {
                    a.move += ";SHOOT " + to_string(target.id);
                    return;
                }
                
                // Otherwise track best damage
                if (damage > bestDamage) {
                    bestDamage = damage;
                    bestTarget = target.id;
                }
            }
        }
        
        if (bestTarget != -1) {
            a.move += ";SHOOT " + to_string(bestTarget);
            return;
        }
    }
    
    // Default action
    a.move += ";HUNKER_DOWN";
}

int main() {
    GAME game;
    unordered_map<int, AGENT> agent;
    vector<vector<TILE>> map;
    set<COORDS> reservedTargets;
    
    // Initialization
    cin >> game.my_id; cin.ignore();
    cin >> game.agent_count; cin.ignore();
    
    for (size_t i = 0; i < game.agent_count; i++) {
        AGENT temp;
        cin >> temp.id >> temp.player >> temp.cooldown >> temp.range >> temp.power >> temp.bombs; 
        cin.ignore();
        agent[temp.id] = temp;
        agent[temp.id].mine = (temp.player == game.my_id);
    }
    
    cin >> game.width >> game.height; cin.ignore();
    map.resize(game.width, vector<TILE>(game.height));
    
    for (size_t i = 0; i < game.height; i++) {
        for (size_t j = 0; j < game.width; j++) {
            int x, y, type;
            cin >> x >> y >> type; cin.ignore();
            map[x][y].type = type;
        }
    }

    vector<COVER> covers = findCovers(game, map);
    
    // Game loop
    while (1) {
        game.turn++;
        cin >> game.agent_count; cin.ignore();
        reservedTargets.clear();
        
        // Update agent states
        for (auto& kv : agent) {
            kv.second.dead = true;
        }
        
        for (size_t i = 0; i < game.agent_count; i++) {
            int id;
            cin >> id >> agent[id].c.x >> agent[id].c.y >> agent[id].cooldown >> agent[id].bombs >> agent[id].wetness; 
            cin.ignore();
            agent[id].dead = false;
            
            if (agent[id].startC.x == -1) {
                agent[id].startC = agent[id].c;
            }
        }

        cin >> game.my_agent_count; cin.ignore();
        
        // Build reservation map
        vector<vector<int>> reservedMap = buildReservedMap(game, agent);
        
        // Process our agents
        for (auto& kv : agent) {
            AGENT& a = kv.second;
            if (a.mine && !a.dead) {
                // Update target
                if (a.target.x == -1 || 
                    distance(a.c, a.target) == 0 || 
                    reservedTargets.find(a.target) != reservedTargets.end()) {
                    a.target = findTarget(game, map, covers, agent, a.id, reservedTargets);
                }
                
                // Find next move
                if (a.target.x != -1) {
                    a.nextMove = selectRoute(game, map, reservedMap, a.c, a.target);
                } else {
                    a.nextMove = a.c;
                }
                
                // Update reservation map
                reservedMap[a.nextMove.x][a.nextMove.y] = 1;
                
                // Build move command
                a.move = to_string(a.id) + ";MOVE " + to_string(a.nextMove.x) + " " + to_string(a.nextMove.y);
            }
        }
        
        // Determine combat actions
        for (auto& kv : agent) {
            AGENT& a = kv.second;
            if (a.mine && !a.dead) {
                findNextAction(game, agent, a.id, map, reservedMap);
                cout << a.move << endl;
            }
        }
    }
}
