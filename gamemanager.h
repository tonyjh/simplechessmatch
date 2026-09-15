#pragma once

#include "engine.h"
#include <thread>
#include <atomic>

class MatchManager;

void convert_move_to_PGN4_format(string &move);
void convert_move_to_standard_engine_format(string &move);

class GameManager
{
public:
   Engine m_engine1;
   Engine m_engine2;
   uint m_wins[2];
   uint m_draws;
   uint m_losses_on_time[2];
   uint m_illegal_move_games;
   uint64_t m_engine_total_depth[2];
   uint64_t m_engine_total_sel_depth[2];
   uint64_t m_engine_total_time_ms[2];
   uint64_t m_engine_total_nodes[2];
   uint64_t m_engine_num_moves[2];
   uint64_t m_total_plies;
   uint64_t m_total_game_time_ms;
   atomic<bool> m_thread_running;
   bool m_swap_sides;
   bool m_error;
   bool m_engine_disconnected;
   string m_fen;
   string m_pgn;
   atomic<bool> m_pgn_valid;
   game_result m_final_result;
   uint m_pair_id;
   static constexpr const char* color_names[2] = {"white", "black"};
   static constexpr const char* color_names_4pc[4] = {"red", "blue", "yellow", "green"};

private:
   string m_move_list;
   vector<string> m_move_vector;
   player_color m_turn;
   player_color_4pc m_turn_4pc;
   uint m_num_moves;
   uint m_drawish_count;
   bool m_loss_on_time;
   bool m_repetition_draw;
   chrono::time_point<std::chrono::steady_clock> m_timestamp; // This timestamp is updated whenever either engine's clock should start running.
                                                              // It's also updated when game_runner starts running.
   chrono::milliseconds m_player_clocks_ms[PLAYER4 + 1];

public:
   GameManager(void);
   ~GameManager(void);
   void game_runner(void);
   bool is_engine_unresponsive(void);
   MatchManager* m_match_mgr;

private:
   game_result run_engine_game(chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   game_result determine_game_result(Engine *white_engine, Engine *black_engine);
   void store_pgn(game_result result, const string &white_name, const string &black_name,
                  chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   void store_pgn4(game_result result, const string &white_name, const string &black_name,
                   chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   void move_played(const string &move);
   bool check_for_repetition_draw(void);
   game_result check_for_adjudication(Engine *white_engine, Engine *black_engine);
   chrono::milliseconds scale_ms_value(chrono::milliseconds ms, double scale) { return chrono::milliseconds(static_cast<int64_t>(ms.count() * scale)); }
};
