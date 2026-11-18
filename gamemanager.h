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
   uint64_t m_engine_clock_left_ms[2];        // sum of clock time remaining at game end
   uint64_t m_engine_clock_left_games[2];     // number of games counted for clock statistics
   double m_engine_clock_left_pct_sum[2];     // sum of remaining clock as % of available time
   double m_engine_clock_left_per_move_sum[2]; // sum of remaining clock per move (ms)
   std::atomic<uint64_t> m_engine_max_time_ms[2];
   uint64_t m_total_plies;
   uint64_t m_total_game_time_ms;
   atomic<bool> m_thread_running;
   bool m_swap_sides;
   atomic<bool> m_error;
   atomic<bool> m_engine_disconnected;
   string m_fen;
   string m_pgn;
   atomic<bool> m_pgn_valid;
   game_result m_final_result;
   uint m_pair_id;
   uint m_game_number;
   static constexpr const char* color_names[2] = {"white", "black"};
   static constexpr const char* color_names_4pc[4] = {"red", "blue", "yellow", "green"};

private:
   string m_move_list;
   vector<string> m_move_vector;
   vector<string> m_move_clocks;   // remaining clock (formatted for PGN %clk comments), parallel to m_move_vector
   player_color m_turn;
   player_color_4pc m_turn_4pc;
   uint m_num_moves;
   uint m_drawish_count;
   bool m_loss_on_time;
   bool m_repetition_draw;
   chrono::time_point<std::chrono::steady_clock> m_timestamp; // This timestamp is updated whenever either engine's clock should start running.
                                                              // It's also updated when game_runner starts running.
   atomic<bool> m_move_in_progress;  // true while an engine is searching for its move
   atomic<bool> m_move_stop_sent;    // true once a stop was sent for the current move
   chrono::milliseconds m_player_clocks_ms[PLAYER4 + 1];

   // Watchdog state published by the game thread for the monitor thread (MatchManager), which polls
   // is_engine_unresponsive() and enforce_fixed_time_limit(). Deadline values are steady-clock
   // milliseconds; INT64_MAX means the corresponding check is disabled for the current move.
   atomic<int64_t> m_ready_deadline_ms;        // engines must be ready by this point (5s after the move was armed)
   atomic<int64_t> m_unresponsive_deadline_ms; // mover's clock has been negative for 10s at this point
   atomic<int64_t> m_fixed_time_deadline_ms;   // current search exceeds the fixed time per move at this point
   atomic<int> m_searching_engine;             // engine_number (FIRST/SECOND) of the engine currently searching

public:
   GameManager(void);
   ~GameManager(void);
   void game_runner(void);
   bool is_engine_unresponsive(void);
   bool enforce_fixed_time_limit(void);
   MatchManager* m_match_mgr;

private:
   game_result run_engine_game(chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms, chrono::milliseconds static_clocks_ms);
   game_result determine_game_result(Engine *white_engine, Engine *black_engine);
   void store_pgn(game_result result, const string &white_name, const string &black_name,
                  chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms, chrono::milliseconds static_clocks_ms);
   void store_pgn4(game_result result, const string &white_name, const string &black_name,
                   chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms, chrono::milliseconds static_clocks_ms);
   void move_played(const string &move);
   bool check_for_repetition_draw(void);
   game_result check_for_adjudication(Engine *white_engine, Engine *black_engine);
   void log_error_message(const std::string& msg);
   void arm_move_watchdog(void);
   void record_move_time(int engine, uint64_t time_ms);
   void record_clock_left(Engine *engine, chrono::milliseconds clock_ms, chrono::milliseconds start_ms,
                          chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms, chrono::milliseconds static_clocks_ms, uint own_moves);
   chrono::milliseconds scale_ms_value(chrono::milliseconds ms, double scale) { return chrono::milliseconds(static_cast<int64_t>(ms.count() * scale)); }
};
