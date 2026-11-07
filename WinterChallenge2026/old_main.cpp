#undef _GLIBCXX_DEBUG
#pragma GCC optimize("Ofast,unroll-loops,omit-frame-pointer,inline")
#pragma GCC option("arch=native", "tune=native", "no-zero-upper")
#pragma GCC target("movbe,aes,pclmul,avx,avx2,f16c,fma,sse3,ssse3,sse4.1,sse4.2,rdrnd,popcnt,bmi,bmi2,lzcnt")

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <climits>
#include <algorithm>
#include <sstream>
#include <map>
#include <set>
#include <unordered_set>
#include <chrono>

#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

using namespace std;

using Coord = int;

/* --- GRID CONFIG --- */

constexpr int GRID_MAX_W = 45;
constexpr int GRID_MAX_H = 30;

constexpr int BORDER_PAD = 2;
constexpr int TOTAL_W = GRID_MAX_W + 2 * BORDER_PAD;
constexpr int TOTAL_H = GRID_MAX_H + 2 * BORDER_PAD;
constexpr int TOTAL_TILES = TOTAL_W * TOTAL_H;

constexpr int FRUIT_LIMIT = 100;

struct GridConfig
{
    int cols;
    int rows;
    int self_pid;
    int rival_pid;
};

static GridConfig g_cfg;

/* --- COORD HELPERS --- */

constexpr int DIR_UP = -TOTAL_W;
constexpr int DIR_LEFT = -1;
constexpr int DIR_RIGHT = 1;
constexpr int DIR_DOWN = TOTAL_W;

Coord to_coord(int cx, int cy) { return cy * TOTAL_W + cx; }
Coord grid_to_coord(int gx, int gy) { return (gy + BORDER_PAD) * TOTAL_W + (gx + BORDER_PAD); }

int coord_cx(Coord c) { return c % TOTAL_W; }
int coord_cy(Coord c) { return c / TOTAL_W; }
int coord_gx(Coord c) { return coord_cx(c) - BORDER_PAD; }
int coord_gy(Coord c) { return coord_cy(c) - BORDER_PAD; }

/* --- WORM --- */

constexpr int WORM_MAX_LEN = 100;
constexpr int WORM_LIMIT = 8;
constexpr int WORMS_PER_SIDE = WORM_LIMIT / 2;

struct Worm
{
    int idx;
    int owner;
    Coord segments[WORM_MAX_LEN];
    int seg_count;
};

int worm_idx(Worm &w) { return w.idx; }
int worm_owner(Worm &w) { return w.owner; }
Coord worm_seg(Worm &w, int i) { return w.segments[i]; }
Coord worm_head(Worm &w) { return worm_seg(w, 0); }
int worm_len(Worm &w) { return w.seg_count; }

void worm_set_seg(Worm &w, int i, Coord c) { w.segments[i] = c; }
void worm_set_len(Worm &w, int n) { w.seg_count = n; }

void worm_append(Worm &w, Coord c) { w.segments[w.seg_count++] = c; }
void worm_pop_front(Worm &w)
{
    memmove(&w.segments[0], &w.segments[1], sizeof(Coord) * (--w.seg_count));
}
void worm_clear(Worm &w) { w.seg_count = 0; }

void worm_init(Worm &w, int id, int pid)
{
    w.idx = id;
    w.owner = pid;
    bzero(w.segments, sizeof(Coord) * WORM_MAX_LEN);
    w.seg_count = 3;
}

void worm_dump(Worm &w)
{
    for (int i = 0; i < w.seg_count; i++)
    {
        Coord p = worm_seg(w, i);
    }
}

/* --- INSTRUCTION / STEP --- */

struct Step
{
    int worm_id;
    Coord target;
};

int step_worm(Step &s) { return s.worm_id; }
Coord step_target(Step &s) { return s.target; }
void step_set_worm(Step &s, int id) { s.worm_id = id; }
void step_set_target(Step &s, Coord c) { s.target = c; }

constexpr int ipow(int b, int e) { return e == 0 ? 1 : b * ipow(b, e - 1); }
constexpr int MAX_COMBOS = ipow(3, WORMS_PER_SIDE);

struct Combo
{
    Step steps[WORM_LIMIT];
    int count;
};

Step &combo_step(Combo &cb, int i) { return cb.steps[i]; }
int combo_size(Combo &cb) { return cb.count; }
void combo_put(Combo &cb, Step &s, int i) { cb.steps[i] = s; }
void combo_set_size(Combo &cb, int n) { cb.count = n; }

/* --- BOARD --- */

constexpr int TILE_FREE = 8;
constexpr int TILE_WALL = 9;
constexpr int TILE_FRUIT = 10;

struct Board
{
    int tick;
    bool finished;
    int casualties[2];
    int tiles[TOTAL_TILES];
    Worm worms[WORM_LIMIT];
    int side_alive_count[2];
    int side_alive_ids[2][WORMS_PER_SIDE];
    int total_alive;
    int alive_ids[WORM_LIMIT];
    Coord fruits[FRUIT_LIMIT];
    int fruit_count;
    float score;
    Combo root_combo;
};

int board_tick(Board &b) { return b.tick; }
bool board_over(Board &b) { return b.finished; }
int board_casualties(Board &b, int pid) { return b.casualties[pid]; }
int board_fruit_count(Board &b) { return b.fruit_count; }
int board_tile(Board &b, Coord c) { return b.tiles[c]; }
Worm &board_worm(Board &b, int id) { return b.worms[id]; }
int board_side_count(Board &b, int pid) { return b.side_alive_count[pid]; }
int board_side_id(Board &b, int pid, int i) { return b.side_alive_ids[pid][i]; }
int board_alive_total(Board &b) { return b.total_alive; }
int board_alive_id(Board &b, int i) { return b.alive_ids[i]; }
Coord board_fruit(Board &b, int i) { return b.fruits[i]; }
float board_score(Board &b) { return b.score; }
Combo &board_root_combo(Board &b) { return b.root_combo; }

void board_set_tick(Board &b, int t) { b.tick = t; }
void board_mark_over(Board &b) { b.finished = true; }
void board_set_casualties(Board &b, int pid, int n) { b.casualties[pid] = n; }
void board_add_casualties(Board &b, int pid, int n) { b.casualties[pid] += n; }
void board_set_fruit_count(Board &b, int n) { b.fruit_count = n; }
void board_set_tile(Board &b, Coord c, int v) { b.tiles[c] = v; }
void board_set_fruit(Board &b, int i, Coord c) { b.fruits[i] = c; }
void board_set_score(Board &b, float s) { b.score = s; }
void board_set_root_combo(Board &b, Combo &cb) { b.root_combo = cb; }

void board_init_tiles(Board &b) { for (int i = 0; i < TOTAL_TILES; i++) b.tiles[i] = TILE_FREE; }
void board_init_worm(Board &b, int wid, int pid)
{
    Worm &w = board_worm(b, wid);
    worm_init(w, wid, pid);
}

void board_reset_alive(Board &b) { b.total_alive = 0; b.side_alive_count[0] = 0; b.side_alive_count[1] = 0; }
void board_register_alive(Board &b, int pid, int wid)
{
    b.alive_ids[b.total_alive++] = wid;
    b.side_alive_ids[pid][b.side_alive_count[pid]++] = wid;
}
void board_unregister(Board &b, int wid, int pid)
{
    for (int i = 0; i < board_alive_total(b); i++)
    {
        if (board_alive_id(b, i) == wid)
        {
            if (i + 1 < board_alive_total(b))
                memmove(&b.alive_ids[i], &b.alive_ids[i + 1], sizeof(int) * (board_alive_total(b) - i - 1));
            b.total_alive--;
            break;
        }
    }
    for (int i = 0; i < board_side_count(b, pid); i++)
    {
        if (board_side_id(b, pid, i) == wid)
        {
            if (i + 1 < board_side_count(b, pid))
                memmove(&b.side_alive_ids[pid][i], &b.side_alive_ids[pid][i + 1], sizeof(int) * (board_side_count(b, pid) - i - 1));
            b.side_alive_count[pid]--;
            return;
        }
    }
}
void board_drop_fruit(Board &b, Coord fc)
{
    for (int i = 0; i < b.fruit_count; i++)
    {
        if (board_fruit(b, i) == fc)
        {
            if (i + 1 < b.fruit_count)
                memmove(&b.fruits[i], &b.fruits[i + 1], sizeof(Coord) * (b.fruit_count - i - 1));
            b.fruit_count--;
            return;
        }
    }
}

void board_snapshot(Board &src, Board &dst)
{
    memcpy(&dst, &src, sizeof(Board));
}

int tally_side_score(Board &b, int pid);

/* --- HELPERS --- */

int rival_of(const int pid) { return 1 - pid; }

bool oob_up(const Coord c) { return coord_cy(c) == 0; }
bool oob_left(const Coord c) { return coord_cx(c) == 0; }
bool oob_right(const Coord c) { return coord_cx(c) == TOTAL_W - 1; }
bool oob_down(const Coord c) { return coord_cy(c) == TOTAL_H - 1; }

Coord go_up(const Coord c) { return c + DIR_UP; }
Coord go_left(const Coord c) { return c + DIR_LEFT; }
Coord go_right(const Coord c) { return c + DIR_RIGHT; }
Coord go_down(const Coord c) { return c + DIR_DOWN; }

int nearest_fruit_bfs(Board &b, Coord origin, Coord &out);

/* --- PRECOMPUTED TABLES --- */

struct DistEntry { int dist; Coord fruit_loc; };

DistEntry fruit_dist_table[TOTAL_TILES][FRUIT_LIMIT];

int quick_lookup_nearest(Board &b, Coord head)
{
    for (int e = 0; e < FRUIT_LIMIT; e++)
    {
        DistEntry de = fruit_dist_table[head][e];
        if (board_tile(b, de.fruit_loc) == TILE_FRUIT)
            return de.dist;
    }
    return -1;
}

void build_fruit_dist_table(Board &b)
{
    for (int ei = 0; ei < b.fruit_count; ei++)
    {
        Coord fp = b.fruits[ei];
        for (int i = 0; i < TOTAL_TILES; i++)
            fruit_dist_table[i][ei] = {TOTAL_W + TOTAL_H, fp};

        bool seen[TOTAL_TILES] = {false};
        queue<pair<Coord, int>> q;
        seen[fp] = true;
        fruit_dist_table[fp][ei] = {0, fp};
        q.push({fp, 0});

        while (!q.empty())
        {
            auto [cur, d] = q.front();
            q.pop();
            Coord nb[4] = { go_up(cur), go_left(cur), go_right(cur), go_down(cur) };
            bool ob[4] = { oob_up(cur), oob_left(cur), oob_right(cur), oob_down(cur) };
            for (int i = 0; i < 4; i++)
            {
                if (ob[i] || seen[nb[i]]) continue;
                if (board_tile(b, nb[i]) == TILE_WALL) continue;
                seen[nb[i]] = true;
                fruit_dist_table[nb[i]][ei] = { d + 1, fp };
                q.push({nb[i], d + 1});
            }
        }
    }
    for (int t = 0; t < TOTAL_TILES; t++)
    {
        sort(fruit_dist_table[t], fruit_dist_table[t] + b.fruit_count,
            [](const DistEntry &a, const DistEntry &b) { return a.dist < b.dist; });
    }
}

void precompute(Board &b)
{
    auto t0 = chrono::high_resolution_clock::now();
    build_fruit_dist_table(b);
    auto t1 = chrono::high_resolution_clock::now();
}

/* --- PHYSICS - MOVE GEN --- */

bool tile_blocks(int v, int wid) { return v != wid && v != TILE_FREE; }

int enumerate_worm_steps(Worm &w, Coord out[3])
{
    Coord hd = worm_head(w);
    Coord nb[4] = { go_left(hd), go_right(hd), go_up(hd), go_down(hd) };
    bool ob[4] = { oob_left(hd), oob_right(hd), oob_up(hd), oob_down(hd) };
    int n = 0;
    for (int i = 0; i < 4; i++)
    {
        if (!ob[i] && nb[i] != worm_seg(w, 1))
            out[n++] = nb[i];
    }
    return n;
}

int enumerate_side_combos(Board &b, int pid, Combo out[MAX_COMBOS])
{
    int wc = board_side_count(b, pid);
    int wids[wc];
    int wcounts[wc];
    Coord wmoves[wc][3];

    for (int i = 0; i < wc; i++)
    {
        int wid = board_side_id(b, pid, i);
        Worm &w = board_worm(b, wid);
        wids[i] = wid;
        wcounts[i] = enumerate_worm_steps(w, wmoves[i]);
    }

    int idx[wc];
    for (int i = 0; i < wc; i++) idx[i] = 0;

    int total = 0;
    while (total < MAX_COMBOS)
    {
        Combo cb;
        cb.count = wc;
        for (int i = 0; i < wc; i++)
        {
            Step &s = combo_step(cb, i);
            step_set_worm(s, wids[i]);
            step_set_target(s, wmoves[i][idx[i]]);
        }
        out[total++] = cb;

        int carry = 1;
        for (int i = wc - 1; i >= 0 && carry; i--)
        {
            idx[i]++;
            if (idx[i] >= wcounts[i]) idx[i] = 0;
            else carry = 0;
        }
        if (carry) break;
    }
    return total;
}

/* --- PHYSICS - APPLY --- */

Combo join_combos(Combo &a, Combo &b)
{
    Combo merged;
    int na = combo_size(a);
    int nb = combo_size(b);
    for (int i = 0; i < na; i++) combo_put(merged, combo_step(a, i), i);
    for (int i = 0; i < nb; i++) combo_put(merged, combo_step(b, i), na + i);
    combo_set_size(merged, na + nb);
    return merged;
}

void exec_step(Board &b, Worm &w, Step &s, Coord consumed[WORM_LIMIT], int *consumed_n)
{
    Coord dest = step_target(s);
    int shift_len = worm_len(w) - 1;

    if (board_tile(b, dest) == TILE_FRUIT)
    {
        consumed[(*consumed_n)++] = dest;
        shift_len = worm_len(w);
        worm_set_len(w, worm_len(w) + 1);
    }
    else
    {
        Coord tail = worm_seg(w, worm_len(w) - 1);
        board_set_tile(b, tail, TILE_FREE);
    }

    memmove(&w.segments[1], &w.segments[0], sizeof(Coord) * shift_len);
    worm_set_seg(w, 0, dest);
}

void exec_all_steps(Board &b, Combo &cb, Coord consumed[WORM_LIMIT], int *consumed_n)
{
    for (int i = 0; i < combo_size(cb); i++)
    {
        Step &s = combo_step(cb, i);
        Worm &w = board_worm(b, step_worm(s));
        exec_step(b, w, s, consumed, consumed_n);
    }
}

void clear_consumed(Board &b, Coord consumed[WORM_LIMIT], int consumed_n)
{
    for (int i = 0; i < consumed_n; i++)
    {
        board_set_tile(b, consumed[i], TILE_FREE);
        board_drop_fruit(b, consumed[i]);
    }
}

int detect_crashes(Board &b, Worm *crashed[WORM_LIMIT])
{
    int crash_n = 0;
    for (int wi = 0; wi < board_alive_total(b); wi++)
    {
        bool hit = false;
        int wid = board_alive_id(b, wi);
        Worm &w = board_worm(b, wid);
        Coord hd = worm_head(w);

        int hv = board_tile(b, hd);
        if (hv == TILE_WALL)
        {
            crashed[crash_n++] = &w;
            continue;
        }

        for (int oi = 0; oi < board_alive_total(b) && !hit; oi++)
        {
            int oid = board_alive_id(b, oi);
            Worm &other = board_worm(b, oid);
            for (int si = 0; si < worm_len(other) && !hit; si++)
            {
                if (wi == oi && si == 0) continue;
                Coord sp = worm_seg(other, si);
                if (hd == sp)
                {
                    crashed[crash_n++] = &w;
                    hit = true;
                }
            }
        }

        if (!hit)
            board_set_tile(b, hd, wid);
    }
    return crash_n;
}

void resolve_crashes(Board &b, Worm *crashed[WORM_LIMIT], int crash_n)
{
    for (int i = 0; i < crash_n; i++)
    {
        Worm &w = *crashed[i];
        if (worm_len(w) - 1 < 3)
        {
            board_unregister(b, worm_idx(w), worm_owner(w));
            board_add_casualties(b, worm_owner(w), 3);
        }
        else
        {
            worm_pop_front(w);
            board_add_casualties(b, worm_owner(w), 1);
        }
    }
}

void destroy_worm(Board &b, Worm &w)
{
    for (int i = 0; i < worm_len(w); i++)
        board_set_tile(b, worm_seg(w, i), TILE_FREE);
    board_unregister(b, worm_idx(w), worm_owner(w));
}

bool apply_fall(Board &b, Worm &w)
{
    int wid = worm_idx(w);
    int wlen = worm_len(w);
    Coord shifted[wlen];

    bool fell = false;
    int iter = 0;
    while (iter++ < TOTAL_H)
    {
        int lo_y = TOTAL_H;
        for (int i = 0; i < wlen; i++)
        {
            Coord p = worm_seg(w, i);
            if (oob_down(p)) { destroy_worm(b, w); return true; }
        }
        for (int i = 0; i < wlen; i++)
        {
            Coord p = worm_seg(w, i);
            Coord below = go_down(p);
            int bv = board_tile(b, below);
            if (tile_blocks(bv, wid)) return false;
            lo_y = min(lo_y, coord_cy(below));
            shifted[i] = below;
        }
        for (int i = 0; i < wlen; i++)
            board_set_tile(b, worm_seg(w, i), TILE_FREE);

        if (lo_y >= g_cfg.rows + BORDER_PAD) { destroy_worm(b, w); return true; }

        for (int i = 0; i < wlen; i++)
        {
            board_set_tile(b, shifted[i], wid);
            worm_set_seg(w, i, shifted[i]);
        }
        fell = true;
    }
    return fell;
}

void settle_gravity(Board &b)
{
    int done = 0;
    int wi = 0;
    while (done < board_alive_total(b))
    {
        int wid = board_alive_id(b, wi);
        Worm &w = board_worm(b, wid);
        bool moved = apply_fall(b, w);
        if (moved) done = 0;
        else done++;
        if (++wi >= board_alive_total(b)) wi = 0;
    }
}

void execute_combo(Board &b, Combo &cb)
{
    board_set_tick(b, board_tick(b) + 1);

    Coord consumed[WORM_LIMIT];
    int consumed_n = 0;
    exec_all_steps(b, cb, consumed, &consumed_n);
    clear_consumed(b, consumed, consumed_n);

    Worm *crashed[WORM_LIMIT];
    int crash_n = detect_crashes(b, crashed);
    resolve_crashes(b, crashed, crash_n);

    settle_gravity(b);

    if (board_tick(b) == 200 ||
        board_side_count(b, g_cfg.self_pid) == 0 ||
        board_side_count(b, g_cfg.rival_pid) == 0 ||
        board_fruit_count(b) == 0)
        board_mark_over(b);
}

/* --- BFS FRUIT DISTANCE --- */

int bfs_nearest_fruit_iter(Board &b, Coord origin, Coord &out)
{
    bool seen[TOTAL_TILES] = {false};
    seen[origin] = true;
    queue<pair<Coord, int>> q;
    q.push({origin, 0});

    while (!q.empty())
    {
        auto [cur, d] = q.front();
        q.pop();
        Coord nb[4] = { go_up(cur), go_left(cur), go_right(cur), go_down(cur) };
        bool ob[4] = { oob_up(cur), oob_left(cur), oob_right(cur), oob_down(cur) };
        for (int i = 0; i < 4; i++)
        {
            if (ob[i] || seen[nb[i]]) continue;
            if (board_tile(b, nb[i]) == TILE_FRUIT) { out = nb[i]; return d + 1; }
            seen[nb[i]] = true;
            if (board_tile(b, nb[i]) == TILE_FREE) q.push({nb[i], d + 1});
        }
    }
    return -1;
}

int bfs_nearest_fruit_rec(Board &b, queue<pair<Coord, int>> &q, bool seen[], Coord &out)
{
    if (q.empty()) return -1;
    pair<Coord, int> front = q.front();
    Coord cur = front.first;
    int d = front.second;
    q.pop();
    Coord nb[4] = { go_up(cur), go_left(cur), go_right(cur), go_down(cur) };
    bool ob[4] = { oob_up(cur), oob_left(cur), oob_right(cur), oob_down(cur) };
    for (int i = 0; i < 4; i++)
    {
        if (!ob[i] && !seen[nb[i]])
        {
            int v = board_tile(b, nb[i]);
            if (v == TILE_FRUIT) { out = nb[i]; return d + 1; }
            seen[nb[i]] = true;
            if (v == TILE_FREE) q.push(make_pair(nb[i], d + 1));
        }
    }
    return bfs_nearest_fruit_rec(b, q, seen, out);
}

int nearest_fruit_bfs(Board &b, Coord origin, Coord &out)
{
    if (board_tile(b, origin) == TILE_FRUIT) { out = origin; return 0; }
    bool seen[TOTAL_TILES] = {false};
    queue<pair<Coord, int>> q;
    seen[origin] = true;
    q.push(make_pair(origin, 0));
    return bfs_nearest_fruit_rec(b, q, seen, out);
}

/* --- EVALUATION --- */

int tally_side_score(Board &b, int pid)
{
    int pts = 0;
    for (int i = 0; i < board_side_count(b, pid); i++)
    {
        int wid = board_side_id(b, pid, i);
        Worm &w = board_worm(b, wid);
        pts += worm_len(w);
    }
    return pts;
}

int pack_priority(int a, int b_val, int b_max) { return a * b_max + b_val; }

int judge_terminal(Board &b, int pid, int my_pts, int opp_pts)
{
    bool i_win = false;
    bool i_lose = false;

    if (board_tick(b) == 200 || board_fruit_count(b) == 0)
    {
        if (my_pts > opp_pts) i_win = true;
        else if (my_pts < opp_pts) i_lose = true;
        else if (board_casualties(b, pid) > board_casualties(b, rival_of(pid))) i_lose = true;
        else if (board_casualties(b, pid) < board_casualties(b, rival_of(pid))) i_win = true;
    }
    else if (opp_pts == 0) i_win = true;
    else if (my_pts == 0) i_lose = true;
    else if (board_casualties(b, pid) > board_casualties(b, rival_of(pid))) i_lose = true;
    else if (board_casualties(b, pid) < board_casualties(b, rival_of(pid))) i_win = true;

    if (i_win) return 1000 + pack_priority(my_pts, (201 - board_tick(b)), 200);
    if (i_lose) return -21000 + pack_priority(my_pts, board_tick(b), 200);
    return 0;
}

float assess(Board &b, int pid)
{
    int my_pts, opp_pts;
    if (pid == g_cfg.self_pid)
    {
        my_pts = tally_side_score(b, g_cfg.self_pid);
        opp_pts = tally_side_score(b, g_cfg.rival_pid);
    }
    else
    {
        my_pts = tally_side_score(b, g_cfg.rival_pid);
        opp_pts = tally_side_score(b, g_cfg.self_pid);
    }

    if (board_over(b))
        return judge_terminal(b, pid, my_pts, opp_pts);

    int gap_sum = 0;
    int footing_bonus = 0;
    for (int i = 0; i < board_side_count(b, pid); i++)
    {
        int wid = board_side_id(b, pid, i);
        Worm &w = board_worm(b, wid);
        Coord hd = worm_head(w);

        int fd = quick_lookup_nearest(b, hd);
        if (fd == -1) gap_sum += GRID_MAX_W + GRID_MAX_H;
        else gap_sum += fd;

        for (int si = 0; si < worm_len(w); si++)
        {
            Coord sp = worm_seg(w, si);
            if (coord_cx(sp) < BORDER_PAD || coord_cx(sp) >= TOTAL_W - BORDER_PAD ||
                coord_cy(sp) < BORDER_PAD || coord_cy(sp) >= TOTAL_H - BORDER_PAD)
                gap_sum += 1;
        }

        for (int si = 0; si < worm_len(w); si++)
        {
            Coord sp = worm_seg(w, si);
            Coord under = go_down(sp);
            if (board_tile(b, under) == TILE_WALL)
            {
                footing_bonus += 1.5 * (worm_len(w) - si) / worm_len(w);
                break;
            }
        }
    }

    gap_sum -= footing_bonus;
    float proximity = gap_sum != 0 ? 1.0 / (2 * gap_sum) : 0.0;

    return my_pts - opp_pts + proximity;
}

Combo pick_best_side_combo(Board &b, int pid, Combo &prior)
{
    Board tmp;
    Combo options[MAX_COMBOS];
    int n_opts = enumerate_side_combos(b, pid, options);

    Combo best;
    float best_val = -100000;
    for (int i = 0; i < n_opts; i++)
    {
        board_snapshot(b, tmp);
        Combo full = join_combos(prior, options[i]);
        execute_combo(tmp, full);
        float val = assess(tmp, pid);
        if (best_val < val) { best_val = val; best = options[i]; }
    }
    return best;
}

Combo pick_greedy_combo(Board &b, int pid)
{
    Combo result;
    Board tmp;
    int wc = board_side_count(b, pid);

    for (int i = 0; i < wc; i++)
    {
        int wid = board_side_id(b, pid, i);
        Worm &w = board_worm(b, wid);
        Coord choices[3];
        int nc = enumerate_worm_steps(w, choices);

        int best_v = -100000;
        Step best_s = {};
        for (int j = 0; j < nc; j++)
        {
            Combo single;
            combo_set_size(single, 1);
            Step &s = combo_step(single, 0);
            step_set_worm(s, wid);
            step_set_target(s, choices[j]);
            Combo trial = join_combos(result, single);
            memcpy(&tmp, &b, sizeof(Board));
            execute_combo(tmp, trial);
            float val = assess(tmp, pid);
            if (val > best_v) { best_v = val; best_s = s; }
        }
        combo_put(result, best_s, i);
        combo_set_size(result, i + 1);
    }
    return result;
}

void emit_markers(Board &b, Combo best)
{
    for (int i = 0; i < combo_size(best); i++)
    {
        Step &s = combo_step(best, i);
        int wid = step_worm(s);
        Worm &w = board_worm(b, wid);
        Coord tgt;
        int d = bfs_nearest_fruit_iter(b, worm_head(w), tgt);
        if (d != -1) cout << "MARK " << coord_gx(tgt) << " " << coord_gy(tgt) << ";";
    }
}

/* --- BEAM SEARCH --- */

struct ScoreCmp { bool operator()(Board &a, Board &b) const { return board_score(a) > board_score(b); } };

int g_beam_depth = 0;
int g_beam_nodes = 0;

bool over_budget(auto &t0, int budget_us)
{
    auto now = chrono::high_resolution_clock::now();
    return chrono::duration_cast<chrono::microseconds>(now - t0).count() >= budget_us;
}

void flush_to_vec(std::vector<Board> &vec, std::priority_queue<Board, std::vector<Board>, ScoreCmp> &pq)
{
    vec.clear();
    while (!pq.empty()) { vec.push_back(pq.top()); pq.pop(); }
    std::sort(vec.begin(), vec.end(), [](Board &a, Board &b) { return board_score(a) > board_score(b); });
}

void try_insert(Board &b, Combo &origin_cb, priority_queue<Board, std::vector<Board>, ScoreCmp> &pq, int width)
{
    g_beam_nodes++;
    if (pq.size() < (size_t)width)
    {
        if (g_beam_depth == 1) board_set_root_combo(b, origin_cb);
        pq.push(b);
    }
    else if (board_score(b) > board_score((Board &)pq.top()))
    {
        pq.pop();
        pq.push(b);
    }
}

void expand_children(Board &b, int pid, std::priority_queue<Board, std::vector<Board>, ScoreCmp> &pq, int width)
{
    Combo empty_cb = {};
    combo_set_size(empty_cb, 0);
    Combo rival_cb = pick_best_side_combo(b, rival_of(pid), empty_cb);

    Combo ally_options[MAX_COMBOS];
    int n_ally = enumerate_side_combos(b, pid, ally_options);

    Board child;
    for (int i = 0; i < n_ally; i++)
    {
        board_snapshot(b, child);
        Combo turn_cb = join_combos(rival_cb, ally_options[i]);
        execute_combo(child, turn_cb);
        float h = assess(child, pid);
        board_set_score(child, h);
        try_insert(child, ally_options[i], pq, width);
    }
}

const Board &extract_best(std::priority_queue<Board, std::vector<Board>, ScoreCmp> &pq)
{
    while (pq.size() > 1) pq.pop();
    return pq.top();
}

Combo run_beam(Board &root, int pid, int max_depth, int width, int budget_us, auto t0)
{
    g_beam_nodes = 0;
    g_beam_depth = 1;

    std::vector<Board> storage;
    storage.reserve(width);
    std::priority_queue<Board, std::vector<Board>, ScoreCmp> pq(ScoreCmp(), std::move(storage));

    expand_children(root, pid, pq, width);

    std::vector<Board> frontier;
    frontier.reserve(width);

    while (!over_budget(t0, budget_us) && g_beam_depth < max_depth)
    {
        g_beam_depth++;
        flush_to_vec(frontier, pq);

        for (Board &node : frontier)
        {
            if (board_over(node)) { try_insert(node, board_root_combo(node), pq, width); continue; }

            expand_children(node, pid, pq, width);

            if (over_budget(t0, budget_us))
            {
                Board &top = (Board &)extract_best(pq);
                if (board_score(frontier[0]) > board_score(top))
                    return board_root_combo(frontier[0]);
                else
                    return board_root_combo(top);
            }
        }
    }

    Board &top = (Board &)extract_best(pq);
    return board_root_combo(top);
}

/* --- PARSING --- */

void read_init(Board &b)
{
    cin >> g_cfg.self_pid;
    g_cfg.rival_pid = rival_of(g_cfg.self_pid);
    cin >> g_cfg.cols >> g_cfg.rows;
    board_init_tiles(b);
    cin.ignore();
    for (int y = 0; y < g_cfg.rows; y++)
    {
        string line; getline(cin, line);
        for (int x = 0; x < g_cfg.cols; x++)
        {
            Coord c = grid_to_coord(x, y);
            if (line[x] == '.') board_set_tile(b, c, TILE_FREE);
            else if (line[x] == '#') board_set_tile(b, c, TILE_WALL);
        }
    }
    int worms_per_side; cin >> worms_per_side;
    board_reset_alive(b);
    int wid;
    for (int i = 0; i < worms_per_side; i++) { cin >> wid; board_init_worm(b, wid, g_cfg.self_pid); board_register_alive(b, g_cfg.self_pid, wid); }
    for (int i = 0; i < worms_per_side; i++) { cin >> wid; board_init_worm(b, wid, g_cfg.rival_pid); board_register_alive(b, g_cfg.rival_pid, wid); }
}

bool decode_segment(string seg, Coord &c)
{
    size_t comma = seg.find(',');
    int x = stoi(seg.substr(0, comma));
    int y = stoi(seg.substr(comma + 1));
    if (x < -BORDER_PAD || x >= g_cfg.cols + BORDER_PAD || y < -BORDER_PAD || y >= g_cfg.rows + BORDER_PAD) return false;
    c = grid_to_coord(x, y);
    return true;
}

void decode_worm(Board &b, Worm &w, int wid, string raw)
{
    worm_clear(w);
    size_t start = 0;
    size_t end = raw.find(':');
    while (end != string::npos)
    {
        string seg = raw.substr(start, end - start);
        Coord c;
        if (decode_segment(seg, c)) { worm_append(w, c); board_set_tile(b, c, wid); }
        start = end + 1;
        end = raw.find(':', start);
    }
    string tail = raw.substr(start);
    Coord c;
    if (decode_segment(tail, c)) { worm_append(w, c); board_set_tile(b, c, wid); }
}

void read_turn(Board &b)
{
    int fc; cin >> fc;
    for (int i = 0; i < fc; i++)
    {
        int x, y; cin >> x >> y;
        Coord c = grid_to_coord(x, y);
        board_set_fruit(b, i, c);
        board_set_tile(b, b.fruits[i], TILE_FRUIT);
    }
    board_set_fruit_count(b, fc);
    board_reset_alive(b);
    int wc; cin >> wc;
    for (int i = 0; i < wc; i++)
    {
        int wid; string raw; cin >> wid >> raw;
        Worm &w = board_worm(b, wid);
        int pid = worm_owner(w);
        board_register_alive(b, pid, wid);
        decode_worm(b, w, wid, raw);
    }
}

/* --- MAIN --- */

void crash_handler(int sig)
{
    const char *name = "Unknown";
    if (sig == SIGSEGV) name = "SIGSEGV";
    else if (sig == SIGABRT) name = "SIGABRT";
    else if (sig == SIGFPE) name = "SIGFPE";
    std::cerr << "\n[ERROR] Caught signal: " << name << " (" << sig << ")\n";
    exit(EXIT_FAILURE);
}

int main()
{
    // signal(SIGSEGV, crash_handler);
    // signal(SIGABRT, crash_handler);
    // signal(SIGFPE, crash_handler);

    Board base;
    bzero(&base, sizeof(base));
    read_init(base);

    Board b;
    while (true)
    {
        int tick = b.tick;
        int my_cas = board_casualties(b, g_cfg.self_pid);
        int opp_cas = board_casualties(b, g_cfg.rival_pid);

        memcpy(&b, &base, sizeof(b));
        board_set_tick(b, tick);
        board_set_casualties(b, g_cfg.self_pid, my_cas);
        board_set_casualties(b, g_cfg.rival_pid, opp_cas);

        read_turn(b);
        auto t0 = chrono::high_resolution_clock::now();

        if (tick == 0) precompute(b);

        Combo best = run_beam(b, g_cfg.self_pid, 50, 150, 30000, t0);

        emit_markers(b, best);

        for (int i = 0; i < combo_size(best); i++)
        {
            Step &s = combo_step(best, i);
            Coord dst = step_target(s);
            int wid = step_worm(s);
            Worm &w = board_worm(b, wid);
            Coord hd = worm_head(w);
            int offset = dst - hd;

            if (offset == DIR_UP) cout << wid << " UP";
            else if (offset == DIR_DOWN) cout << wid << " DOWN";
            else if (offset == DIR_LEFT) cout << wid << " LEFT";
            else cout << wid << " RIGHT";

            if (i != board_side_count(b, g_cfg.self_pid) - 1) cout << ";";
        }
        cout << endl;

        tick++;
    }
    return 0;
}
