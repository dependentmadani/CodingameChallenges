#undef _GLIBCXX_DEBUG
#pragma GCC optimize("Ofast,inline")
#pragma GCC target("avx,avx2,f16c,fma,sse2,sse3,ssse3,sse4.1,sse4.2")
#pragma GCC target("movbe")
#pragma GCC target("bmi,bmi2,lzcnt,popcnt")
#pragma GCC target("aes,pclmul,rdrnd")

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <climits>
#include <algorithm>
#include <array>

constexpr int MAX_WIDTH = 20;
constexpr int MAX_HEIGHT = 20;
constexpr int MAX_AGENTS = 10;
constexpr int MAX_PLAYERS = 2;
constexpr int MAX_MOVES_PER_AGENT = 5;
constexpr int MAX_SHOOTS_PER_AGENT = 5;
constexpr int MAX_BOMB_PER_AGENT = 5;
constexpr int MAX_COMMANDS_PER_AGENT = 35;
constexpr int MAX_COMMANDS_PER_PLAYER = 1024;
constexpr int MAX_SIMULATIONS = 1024;


enum class ActionType {
    SHOOT,
    THROW,
    HUNKER
};

struct AgentAction {
    int target_x_or_id;
    int target_y;
    float score;
};

struct AgentCommand {
    int mv_x;
    int mv_y;
    ActionType action_type;
    int target_x_or_id;
    int target_y;
    float score;
};

struct SimulationResult {
    float score;
    int my_cmds_index;
    int op_cmds_index;
};

struct GameOutput {
    std::array<std::array<std::array<int, MAX_WIDTH>, MAX_HEIGHT>, MAX_AGENTS> bfs_enemy_distances;
    std::array<std::array<AgentAction, MAX_MOVES_PER_AGENT>, MAX_AGENTS> moves;
    std::array<int, MAX_AGENTS> move_counts;
    std::array<std::array<AgentAction, MAX_SHOOTS_PER_AGENT>, MAX_AGENTS> shoots;
    std::array<int, MAX_AGENTS> shoot_counts;
    std::array<std::array<AgentAction, MAX_BOMB_PER_AGENT>, MAX_AGENTS> bombs;
    std::array<int, MAX_AGENTS> bomb_counts;
    std::array<std::array<AgentCommand, MAX_COMMANDS_PER_AGENT>, MAX_AGENTS> agent_commands;
    std::array<int, MAX_AGENTS> agent_command_counts;
    std::array<std::array<std::array<AgentCommand, MAX_AGENTS>, MAX_COMMANDS_PER_PLAYER>, MAX_PLAYERS> player_commands;
    std::array<int, MAX_PLAYERS> player_command_count;
    std::array<SimulationResult, MAX_SIMULATIONS> simulation_results;
    int simulation_count;
};

struct AgentInfo {
    int id;
    int player_id;
    int shoot_cooldown;
    int optimal_range;
    int soaking_power;
    int splash_bombs;
};

struct PlayerInfo {
    int agent_count;
    int agent_start_index;
    int agent_stop_index;
};

struct Tile {
    int x;
    int y;
    int type;
};

struct AgentState {
    int id;
    int x;
    int y;
    int cooldown;
    int splash_bombs;
    int wetness;
    bool alive;
};

struct MapInfo {
    int width;
    int height;
    std::array<std::array<Tile, MAX_WIDTH>, MAX_HEIGHT> map;
};

struct GameValues {
    int my_player_id;
    int agent_info_count;
    std::array<AgentInfo, MAX_AGENTS> agent_info;
    std::array<PlayerInfo, MAX_PLAYERS> player_info;
    MapInfo map;
};

struct GameStateInfo {
    std::array<AgentState, MAX_AGENTS> agents;
    int agent_count;
    int my_agent_count;
};

struct Game {
    GameValues consts;
    GameStateInfo state;
    GameOutput output;
};

Game game;

static clock_t cpu_start;
#define CPU_RESET        (cpu_start = clock())
#define CPU_MS_USED      (((double)(clock() - cpu_start)) * 1000.0 / CLOCKS_PER_SEC)
#define CPU_BREAK(val)   if (CPU_MS_USED > (val)) break;

int controlled_score_gain_if_agent_moves_to(int agent_id, int nx, int ny) {
    int my_gain = 0;
    int enemy_gain = 0;
    const int my_player_id = game.consts.my_player_id;
    const int enemy_player_id = !my_player_id;

    for (int y = 0; y < game.consts.map.height; y++) {
        for (int x = 0; x < game.consts.map.width; x++) {
            if (game.consts.map.map[y][x].type > 0) continue;

            int d_my = INT_MAX;
            int d_en = INT_MAX;

            for (int i = game.consts.player_info[my_player_id].agent_start_index;
                 i <= game.consts.player_info[my_player_id].agent_stop_index; i++) {
                if (!game.state.agents[i].alive) continue;
                int ax = (i == agent_id) ? nx : game.state.agents[i].x;
                int ay = (i == agent_id) ? ny : game.state.agents[i].y;
                int d = std::abs(x - ax) + std::abs(y - ay);
                if (game.state.agents[i].wetness >= 50) d *= 2;
                if (d < d_my) d_my = d;
            }

            for (int i = game.consts.player_info[enemy_player_id].agent_start_index;
                 i <= game.consts.player_info[enemy_player_id].agent_stop_index; i++) {
                if (!game.state.agents[i].alive) continue;
                int ax = game.state.agents[i].x;
                int ay = game.state.agents[i].y;
                int d = std::abs(x - ax) + std::abs(y - ay);
                if (game.state.agents[i].wetness >= 50) d *= 2;
                if (d < d_en) d_en = d;
            }

            if (d_my < d_en) my_gain++;
            else if (d_en < d_my) enemy_gain++;
        }
    }

    return my_gain - enemy_gain;
}

void read_game_inputs_init() {
    int my_id, agent_info_count;
    std::cin >> my_id;
    std::cin >> agent_info_count;

    game.consts.my_player_id = my_id;
    game.consts.agent_info_count = agent_info_count;

    for (int i = 0; i < agent_info_count; i++) {
        std::cin >> game.consts.agent_info[i].id
                 >> game.consts.agent_info[i].player_id
                 >> game.consts.agent_info[i].shoot_cooldown
                 >> game.consts.agent_info[i].optimal_range
                 >> game.consts.agent_info[i].soaking_power
                 >> game.consts.agent_info[i].splash_bombs;
    }

    for (int p = 0; p < MAX_PLAYERS; ++p) {
        game.consts.player_info[p].agent_count = 0;
        game.consts.player_info[p].agent_start_index = -1;
        game.consts.player_info[p].agent_stop_index = -1;
    }

    for (int i = 0; i < game.consts.agent_info_count; ++i) {
        int player = game.consts.agent_info[i].player_id;
        if (game.consts.player_info[player].agent_count == 0) {
            game.consts.player_info[player].agent_start_index = i;
        }
        game.consts.player_info[player].agent_count++;
        game.consts.player_info[player].agent_stop_index = i;
    }
    
    std::cin >> game.consts.map.width >> game.consts.map.height;
    for (int i = 0; i < game.consts.map.height * game.consts.map.width; i++) {
        int x, y, tile_type;
        std::cin >> x >> y >> tile_type;
        game.consts.map.map[y][x] = Tile{x, y, tile_type};
    }
}

void read_game_inputs_cycle() {    
    for (auto& agent : game.state.agents) {
        agent.alive = false;
    }
    
    std::cin >> game.state.agent_count;
    int agent_id, agent_x, agent_y, agent_cooldown, agent_splash_bombs, agent_wetness;
    for (int i = 0; i < game.state.agent_count; i++) {
        std::cin >> agent_id
                 >> agent_x
                 >> agent_y
                 >> agent_cooldown
                 >> agent_splash_bombs
                 >> agent_wetness;
        agent_id--;
        game.state.agents[agent_id] = AgentState{
            agent_id, agent_x, agent_y, 
            agent_cooldown, agent_splash_bombs, 
            agent_wetness, true
        };
    }
    
    std::cin >> game.state.my_agent_count;
    CPU_RESET;
}

void bfs_distances() {
    static constexpr std::array<std::array<int, 2>, 4> dirs = {{{0,1}, {1,0}, {0,-1}, {-1,0}}};

    for (int k = 0; k < MAX_AGENTS; k++) {
        const AgentState& enemy = game.state.agents[k];
        if (!enemy.alive) continue;

        const int eid = enemy.id;
        std::array<std::array<bool, MAX_WIDTH>, MAX_HEIGHT> visited = {};
        std::array<std::array<int, MAX_WIDTH>, MAX_HEIGHT> dist = {};

        std::array<int, MAX_WIDTH * MAX_HEIGHT> queue_x;
        std::array<int, MAX_WIDTH * MAX_HEIGHT> queue_y;
        int front = 0, back = 0;

        visited[enemy.y][enemy.x] = true;
        dist[enemy.y][enemy.x] = 0;
        queue_x[back] = enemy.x;
        queue_y[back++] = enemy.y;

        while (front < back) {
            int x = queue_x[front];
            int y = queue_y[front++];
            for (const auto& dir : dirs) {
                int nx = x + dir[0];
                int ny = y + dir[1];
                if (nx < 0 || nx >= game.consts.map.width || 
                    ny < 0 || ny >= game.consts.map.height) continue;
                if (game.consts.map.map[ny][nx].type > 0) continue;
                if (visited[ny][nx]) continue;

                visited[ny][nx] = true;
                dist[ny][nx] = dist[y][x] + 1;

                queue_x[back] = nx;
                queue_y[back++] = ny;
            }
        }

        for (int y = 0; y < game.consts.map.height; y++) {
            for (int x = 0; x < game.consts.map.width; x++) {
                game.output.bfs_enemy_distances[eid][y][x] = 
                    visited[y][x] ? dist[y][x] : 9999;
            }
        }
    }
}

void compute_best_agents_moves(int agent_id) {
    static constexpr std::array<std::array<int, 2>, 5> dirs = {{
        {0, 0},   // stay
        {-1, 0},  // left
        {1, 0},   // right
        {0, -1},  // up
        {0, 1}    // down
    }};

    const AgentState& agent_state = game.state.agents[agent_id];
    const AgentInfo& agent_info = game.consts.agent_info[agent_id];

    game.output.move_counts[agent_id] = 0;

    const int my_player_id = agent_info.player_id;
    const int enemy_player_id = !my_player_id;

    const int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    const int enemy_stop = game.consts.player_info[enemy_player_id].agent_stop_index;
    const int ally_start = game.consts.player_info[my_player_id].agent_start_index;
    const int ally_stop = game.consts.player_info[my_player_id].agent_stop_index;

    bool danger = false;
    for (int k = enemy_start; k <= enemy_stop; k++) {
        const AgentState& enemy = game.state.agents[k];
        if (!enemy.alive) continue;
        if (enemy.splash_bombs <= 0) continue;

        int dist = std::abs(enemy.x - agent_state.x) + std::abs(enemy.y - agent_state.y);
        if (dist <= 7) {
            danger = true;
            break;
        }
    }

    for (const auto& dir : dirs) {
        int nx = agent_state.x + dir[0];
        int ny = agent_state.y + dir[1];

        if (nx < 0 || nx >= game.consts.map.width || 
            ny < 0 || ny >= game.consts.map.height) continue;
        if (game.consts.map.map[ny][nx].type > 0) continue;

        int min_dist_to_enemy = 9999;
        for (int k = enemy_start; k <= enemy_stop; k++) {
            const AgentState& op_state = game.state.agents[k];
            if (!op_state.alive) continue;

            int dist = game.output.bfs_enemy_distances[op_state.id][ny][nx];
            if (dist < min_dist_to_enemy) min_dist_to_enemy = dist;
        }

        float penalty = 0.0f;
        if (danger) {
            for (int a = ally_start; a <= ally_stop; a++) {
                if (a == agent_id) continue;
                const AgentState& ally = game.state.agents[a];
                if (!ally.alive) continue;

                int dist_ally = std::abs(ally.x - nx) + std::abs(ally.y - ny);
                if (dist_ally < 3) penalty += 20.0f;
            }
        }

        int gain = controlled_score_gain_if_agent_moves_to(agent_id, nx, ny);

        // Reward being close to own optimal range, but not too close
        int opt_range = agent_info.optimal_range;
        float proximity_reward = -static_cast<float>(std::abs(min_dist_to_enemy - opt_range));

        // Estimate threat from enemies that could shoot this tile
        float risk = 0.0f;
        for (int k2 = enemy_start; k2 <= enemy_stop; k2++) {
            const AgentState& op_state = game.state.agents[k2];
            if (!op_state.alive) continue;
            int dist_e = game.output.bfs_enemy_distances[op_state.id][ny][nx];
            int enemy_range_twice = game.consts.agent_info[k2].optimal_range * 2;
            if (dist_e <= enemy_range_twice) {
                risk += static_cast<float>(enemy_range_twice - dist_e + 1);
            }
        }

        // Final heuristic score (higher is better)
        float score = gain * 15.0f          // strongly favour control gain
                      + proximity_reward * 5.0f  // prefer optimal engagement distance
                      - risk * 2.0f             // avoid being in enemy effective range
                      - penalty;                // avoid clustering when bombs threaten

        if (game.output.move_counts[agent_id] < MAX_MOVES_PER_AGENT) {
            game.output.moves[agent_id][game.output.move_counts[agent_id]++] = 
                AgentAction{nx, ny, score};
        }
    }

    // Sort descending by score
    auto& moves = game.output.moves[agent_id];
    int count = game.output.move_counts[agent_id];
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (moves[j].score > moves[i].score) {
                std::swap(moves[i], moves[j]);
            }
        }
    }
}

void compute_best_agents_shoot(int agent_id, int new_shooter_x, int new_shooter_y) {
    const AgentState& shooter_state = game.state.agents[agent_id];
    const AgentInfo& shooter_info = game.consts.agent_info[agent_id];
    auto& output_list = game.output.shoots[agent_id];

    game.output.shoot_counts[agent_id] = 0;
    if (shooter_state.cooldown > 0) return;

    const int my_player_id = shooter_info.player_id;
    const int enemy_player_id = !my_player_id;
    const int enemy_start = game.consts.player_info[enemy_player_id].agent_start_index;
    const int enemy_stop = game.consts.player_info[enemy_player_id].agent_stop_index;

    int shoots_count = 0;

    for (int k = enemy_start; k <= enemy_stop; k++) {
        const AgentState& enemy = game.state.agents[k];
        if (!enemy.alive) continue;

        int dx = std::abs(enemy.x - new_shooter_x);
        int dy = std::abs(enemy.y - new_shooter_y);
        int dist = dx + dy;

        int max_range = 2 * shooter_info.optimal_range;
        if (dist > max_range) continue;

        // Estimate expected wetness gain if we shoot this enemy
        float range_modifier = (dist <= shooter_info.optimal_range) ? 1.0f : 0.5f;
        int damage = static_cast<int>(shooter_info.soaking_power * range_modifier);
        int newWet = std::min(100, enemy.wetness + damage);
        int wet_gain = newWet - enemy.wetness;
        int fifty_gain = (enemy.wetness < 50 && newWet >= 50) ? 1 : 0;
        int kill_gain  = (enemy.wetness < 100 && newWet >= 100) ? 1 : 0;

        // Heuristic score mirroring evaluate_simulation weights (but simplified)
        float score = kill_gain * 1200.0f + fifty_gain * 250.0f + wet_gain * 5.0f - dist * 1.5f;

        if (score > 0 && shoots_count < MAX_SHOOTS_PER_AGENT) {
            output_list[shoots_count++] = AgentAction{k, 0, score};
        }
    }

    // Sort descending
    auto& shoots = game.output.shoots[agent_id];
    for (int i = 0; i < shoots_count - 1; i++) {
        for (int j = i + 1; j < shoots_count; j++) {
            if (shoots[j].score > shoots[i].score) {
                std::swap(shoots[i], shoots[j]);
            }
        }
    }
    game.output.shoot_counts[agent_id] = shoots_count;
}

void compute_best_agents_bomb(int agent_id, int new_thrower_x, int new_thrower_y) {
    const AgentState& thrower_state = game.state.agents[agent_id];
    const AgentInfo& thrower_info = game.consts.agent_info[agent_id];
    game.output.bomb_counts[agent_id] = 0;

    // Cannot throw if no bombs available or agent dead
    if (!thrower_state.alive || thrower_state.splash_bombs <= 0) return;

    const int my_player_id = thrower_info.player_id;
    const int enemy_player_id = !my_player_id;

    // Iterate over every walkable cell within Manhattan distance <= 3 from the thrower’s new position
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            int tx = new_thrower_x + dx;
            int ty = new_thrower_y + dy;

            if (tx < 0 || tx >= game.consts.map.width ||
                ty < 0 || ty >= game.consts.map.height) continue;

            int dist = std::abs(dx) + std::abs(dy);
            if (dist > 3) continue; // outside throw range

            // Skip solid tiles (cannot land the bomb there)
            if (game.consts.map.map[ty][tx].type > 0) continue;

            // Prevent friendly fire (splash radius is Chebyshev distance 1)
            bool splash_hits_ally = false;
            for (int a = game.consts.player_info[my_player_id].agent_start_index;
                 a <= game.consts.player_info[my_player_id].agent_stop_index; ++a) {
                const AgentState& ally = game.state.agents[a];
                if (!ally.alive) continue;
                int dxa = std::abs(ally.x - tx);
                int dya = std::abs(ally.y - ty);
                if (dxa <= 1 && dya <= 1) { // ally in splash radius
                    splash_hits_ally = true;
                    break;
                }
            }
            if (splash_hits_ally) continue;

            // Evaluate effect on enemies
            int wet_gain = 0;
            int fifty_gain = 0;
            int kill_gain = 0;
            int enemy_hits = 0;
            for (int e = game.consts.player_info[enemy_player_id].agent_start_index;
                 e <= game.consts.player_info[enemy_player_id].agent_stop_index; ++e) {
                const AgentState& enemy = game.state.agents[e];
                if (!enemy.alive) continue;
                int dxe = std::abs(enemy.x - tx);
                int dye = std::abs(enemy.y - ty);
                if (dxe <= 1 && dye <= 1) {
                    enemy_hits++;
                    int newWet = std::min(100, enemy.wetness + 30); // bomb adds 30 wetness
                    wet_gain += newWet - enemy.wetness;
                    if (enemy.wetness < 50 && newWet >= 50) fifty_gain++;
                    if (enemy.wetness < 100 && newWet >= 100) kill_gain++;
                }
            }
            if (enemy_hits == 0) continue; // no enemy affected

            // Heuristic score approximates evaluation weights
            float score = kill_gain * 1200.0f + fifty_gain * 250.0f + wet_gain * 5.0f - dist * 2.0f;

            // Insert into candidate list (keep up to MAX_BOMB_PER_AGENT best bombs)
            if (game.output.bomb_counts[agent_id] < MAX_BOMB_PER_AGENT) {
                game.output.bombs[agent_id][game.output.bomb_counts[agent_id]++] =
                    AgentAction{tx, ty, score};
            } else {
                // If full, replace worst one when new bomb is better
                int worst_idx = 0;
                float worst_score = game.output.bombs[agent_id][0].score;
                for (int b = 1; b < MAX_BOMB_PER_AGENT; ++b) {
                    if (game.output.bombs[agent_id][b].score < worst_score) {
                        worst_score = game.output.bombs[agent_id][b].score;
                        worst_idx = b;
                    }
                }
                if (score > worst_score) {
                    game.output.bombs[agent_id][worst_idx] = AgentAction{tx, ty, score};
                }
            }
        }
    }

    // Sort candidates by descending score
    int count = game.output.bomb_counts[agent_id];
    auto& bombs = game.output.bombs[agent_id];
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (bombs[j].score > bombs[i].score) {
                std::swap(bombs[i], bombs[j]);
            }
        }
    }
}

void compute_best_agents_positions() {
    for (int i = 0; i < MAX_AGENTS; i++) {
        game.output.agent_command_counts[i] = 0;        
        if (!game.state.agents[i].alive) continue;
        
        compute_best_agents_moves(i);
        int move_count = game.output.move_counts[i];
        int cmd_index = 0;

        for (int m = 0; m < move_count && cmd_index < MAX_COMMANDS_PER_AGENT; m++) {
            const AgentAction& mv = game.output.moves[i][m];
            int mv_x = mv.target_x_or_id;
            int mv_y = mv.target_y;

            compute_best_agents_bomb(i, mv_x, mv_y);
            int bombs_limit = std::min(game.output.bomb_counts[i], 3); // take up to top 3 bombs
            for (int b = 0; b < bombs_limit && cmd_index < MAX_COMMANDS_PER_AGENT; ++b) {
                const AgentAction& bomb = game.output.bombs[i][b];
                game.output.agent_commands[i][cmd_index++] = 
                    AgentCommand{mv_x, mv_y, ActionType::THROW, 
                                 bomb.target_x_or_id, bomb.target_y, bomb.score};
            }

            compute_best_agents_shoot(i, mv_x, mv_y);
            int shoot_limit = std::min(game.output.shoot_counts[i], MAX_SHOOTS_PER_AGENT);
            for (int s = 0; s < shoot_limit && cmd_index < MAX_COMMANDS_PER_AGENT; s++) {
                const AgentAction& shoot = game.output.shoots[i][s];
                game.output.agent_commands[i][cmd_index++] = 
                    AgentCommand{mv_x, mv_y, ActionType::SHOOT, 
                                shoot.target_x_or_id, shoot.target_y, shoot.score};
            }

            if (cmd_index < MAX_COMMANDS_PER_AGENT) {
                game.output.agent_commands[i][cmd_index++] = 
                    AgentCommand{mv_x, mv_y, ActionType::HUNKER, -1, -1, mv.score};
            }
        }
        game.output.agent_command_counts[i] = cmd_index;
    }
}

void compute_best_player_positions() {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        game.output.player_command_count[p] = 0;

        int agent_start_id = game.consts.player_info[p].agent_start_index;
        int agent_stop_id = game.consts.player_info[p].agent_stop_index;
        std::array<int, MAX_AGENTS> max_cmds = {};
        int total = 1;

        for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
            if (!game.state.agents[agent_id].alive) continue;
            max_cmds[agent_id] = 1;
        }

        bool updated = true;
        while (updated) {
            updated = false;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;

                int current = max_cmds[agent_id];
                int available = game.output.agent_command_counts[agent_id];

                if (current < available) {
                    int new_total = total / current * (current + 1);
                    if (new_total <= MAX_COMMANDS_PER_PLAYER) {
                        max_cmds[agent_id]++;
                        total = new_total;
                        updated = true;
                    }
                }
            }
        }

        std::array<int, MAX_AGENTS> indices = {};
        while (true) {

            for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;
                int cmd_id = indices[agent_id];
                game.output.player_commands[p][game.output.player_command_count[p]][agent_id] =
                    game.output.agent_commands[agent_id][cmd_id];
            }

            game.output.player_command_count[p]++;

            int carry = 1;
            for (int agent_id = agent_start_id; agent_id <= agent_stop_id && carry; agent_id++) {
                if (!game.state.agents[agent_id].alive) continue;

                indices[agent_id]++;
                if (indices[agent_id] >= max_cmds[agent_id]) {
                    indices[agent_id] = 0;
                    carry = 1;
                } else {
                    carry = 0;
                }
            }

            if (carry) break;
        }
    }
}

struct SimulationCalculation {
    std::array<AgentState, MAX_AGENTS> sim_agents;
    int wetness_gain;
    int nb_50_wet_gain;
    int nb_100_wet_gain;
    int control_score;
};

void simulate_players_commands(int my_cmd_index, int en_cmd_index, SimulationCalculation* ctx) {
    const int my_id = game.consts.my_player_id;
    const int en_id = !my_id;
    const int my_start = game.consts.player_info[my_id].agent_start_index;
    const int my_stop = game.consts.player_info[my_id].agent_stop_index;
    const int en_start = game.consts.player_info[en_id].agent_start_index;
    const int en_stop = game.consts.player_info[en_id].agent_stop_index;

    ctx->sim_agents = game.state.agents;
    ctx->wetness_gain = 0;
    ctx->nb_50_wet_gain = 0;
    ctx->nb_100_wet_gain = 0;

    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;

        AgentCommand cmd;
        if (aid >= my_start && aid <= my_stop) {
            cmd = game.output.player_commands[my_id][my_cmd_index][aid];
        } else if (aid >= en_start && aid <= en_stop) {
            cmd = game.output.player_commands[en_id][en_cmd_index][aid];
        } else continue;

        ctx->sim_agents[aid].x = cmd.mv_x;
        ctx->sim_agents[aid].y = cmd.mv_y;
    }

    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;

        AgentCommand cmd;
        if (aid >= my_start && aid <= my_stop) {
            cmd = game.output.player_commands[my_id][my_cmd_index][aid];
        } else if (aid >= en_start && aid <= en_stop) {
            cmd = game.output.player_commands[en_id][en_cmd_index][aid];
        } else continue;

        if (cmd.action_type == ActionType::THROW) {
            for (int t = 0; t < MAX_AGENTS; t++) {
                if (!ctx->sim_agents[t].alive) continue;
                int dx = std::abs(ctx->sim_agents[t].x - cmd.target_x_or_id);
                int dy = std::abs(ctx->sim_agents[t].y - cmd.target_y);
                if (dx <= 1 && dy <= 1) {
                    ctx->sim_agents[t].wetness += 30;
                }
            }
        } else if (cmd.action_type == ActionType::SHOOT) {
            int target_id = cmd.target_x_or_id;
            if (!ctx->sim_agents[target_id].alive) continue;

            AgentState& shooter = ctx->sim_agents[aid];
            AgentState& target = ctx->sim_agents[target_id];
            const AgentInfo& shooter_info = game.consts.agent_info[aid];

            int dx = std::abs(shooter.x - target.x);
            int dy = std::abs(shooter.y - target.y);
            int dist = dx + dy;
            float range_modifier = 0.0f;
            if (dist <= shooter_info.optimal_range) {
                range_modifier = 1.0f;
            } else if (dist <= 2 * shooter_info.optimal_range) {
                range_modifier = 0.5f;
            }
            if (range_modifier == 0.0f) continue;

            float cover_modifier = 1.0f;
            int adj_x = -((target.x - shooter.x) > 0) + ((target.x - shooter.x) < 0);
            int adj_y = -((target.y - shooter.y) > 0) + ((target.y - shooter.y) < 0);
            int cx = target.x + adj_x;
            int cy = target.y + adj_y;
            if (cx >= 0 && cx < game.consts.map.width && 
                cy >= 0 && cy < game.consts.map.height) {
                int tile = game.consts.map.map[cy][cx].type;
                if (tile == 1) cover_modifier = 0.5f;
                else if (tile == 2) cover_modifier = 0.25f;
            }

            float damage = shooter_info.soaking_power * range_modifier * cover_modifier;
            if (damage > 0) target.wetness += static_cast<int>(damage);
        }
    }

    const int my_id_player = game.consts.my_player_id;
    for (int aid = 0; aid < MAX_AGENTS; aid++) {
        int curr = game.state.agents[aid].wetness;
        int now = ctx->sim_agents[aid].wetness;
        if (now >= 100) {
            ctx->sim_agents[aid].alive = false;
            now = 100;
        }

        int pid = game.consts.agent_info[aid].player_id;
        int delta = now - curr;
        if (delta == 0) continue;

        if (now >= 100 && curr < 100) {
            ctx->nb_100_wet_gain += (pid == my_id_player) ? -1 : 1;
        }
        if (now >= 50 && curr < 50) {
            ctx->nb_50_wet_gain += (pid == my_id_player) ? -1 : 1;
        }

        ctx->wetness_gain += (pid == my_id_player) ? -delta : delta;
    }

    ctx->control_score = 0;
    for (int aid = my_start; aid <= my_stop; aid++) {
        if (!ctx->sim_agents[aid].alive) continue;
        ctx->control_score += controlled_score_gain_if_agent_moves_to(
            aid, ctx->sim_agents[aid].x, ctx->sim_agents[aid].y);
    }
}

float evaluate_simulation(const SimulationCalculation* ctx) {
    return
        ctx->control_score / 100.0f * 10.0f +
        ctx->wetness_gain / 100.0f * 100.0f +
        ctx->nb_50_wet_gain / 10.0f * 1000.0f +
        ctx->nb_100_wet_gain / 10.0f * 10000.0f;
}

void compute_evaluation() {
    const int my_id = game.consts.my_player_id;
    const int en_id = !my_id;
    game.output.simulation_count = 0;

    const int my_count = game.output.player_command_count[my_id];
    const int en_index = 0;

    for (int i = 0; i < my_count; i++) {
        SimulationCalculation ctx;
        simulate_players_commands(i, en_index, &ctx);
        float score = evaluate_simulation(&ctx);

        if (game.output.simulation_count < MAX_SIMULATIONS) {
            game.output.simulation_results[game.output.simulation_count++] = 
                SimulationResult{score, i, en_index};
        }
    }

    for (int i = 0; i < game.output.simulation_count - 1; i++) {
        for (int j = i + 1; j < game.output.simulation_count; j++) {
            if (game.output.simulation_results[j].score > game.output.simulation_results[i].score) {
                std::swap(game.output.simulation_results[i], game.output.simulation_results[j]);
            }
        }
    }
}

void apply_output() {
    float cpu_time = CPU_MS_USED;
    const int my_player_id = game.consts.my_player_id;
    const int agent_start_id = game.consts.player_info[my_player_id].agent_start_index;
    const int agent_stop_id = game.consts.player_info[my_player_id].agent_stop_index;

    int best_index = game.output.simulation_results[0].my_cmds_index;

    for (int agent_id = agent_start_id; agent_id <= agent_stop_id; agent_id++) {
        if (!game.state.agents[agent_id].alive) continue;
        const AgentCommand& cmd = game.output.player_commands[my_player_id][best_index][agent_id];

        std::cout << agent_id + 1;

        if (cmd.mv_x != game.state.agents[agent_id].x || 
            cmd.mv_y != game.state.agents[agent_id].y) {
            std::cout << ";MOVE " << cmd.mv_x << " " << cmd.mv_y;
        }

        if (cmd.action_type == ActionType::SHOOT) {
            std::cout << ";SHOOT " << cmd.target_x_or_id + 1;
        } else if (cmd.action_type == ActionType::THROW) {
            std::cout << ";THROW " << cmd.target_x_or_id << " " << cmd.target_y;
        } else if (cmd.action_type == ActionType::HUNKER) {
            std::cout << ";HUNKER_DOWN";
        }
        std::cout << "; MESSAGE Heelloo! my name is agent_" << agent_id << " :D" << std::endl;
    }
}

int main() {
    read_game_inputs_init();

    while (true) {
        read_game_inputs_cycle();
        bfs_distances();
        compute_best_agents_positions();
        compute_best_player_positions();
        compute_evaluation();
        apply_output();
    }

    return 0;
}