#include "engine.h"
#include <thread>
#include <atomic>

void convert_move_to_PGN4_format(string &move);

class GameManager
{
public:
   Engine m_engine1;
   Engine m_engine2;
   uint m_engine1_wins;
   uint m_engine2_wins;
   uint m_draws;
   uint m_engine1_losses_on_time;
   uint m_engine2_losses_on_time;
   uint m_illegal_move_games;
   bool m_thread_running;
   bool m_swap_sides;
   bool m_error;
   bool m_engine_disconnected;
   string m_fen;
   string m_pgn;
   atomic<bool> m_pgn_valid;

private:
   string m_move_list;
   vector<string> m_move_vector;
   player_color m_turn;
   uint m_num_moves;
   bool m_loss_on_time;
   bool m_repetition_draw;
   chrono::time_point<std::chrono::steady_clock> m_timestamp; // This timestamp is updated whenever either engine's clock should start running.
                                                              // It's also updated when game_runner starts running.
   chrono::milliseconds m_white_clock_ms;
   chrono::milliseconds m_black_clock_ms;

public:
   GameManager(void);
   ~GameManager(void);
   void game_runner(void);
   bool is_engine_unresponsive(void);

private:
   game_result run_engine_game(chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   game_result determine_game_result(Engine *white_engine, Engine *black_engine);
   void store_pgn(game_result result, const string &white_name, const string &black_name,
                  chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   void store_pgn4(game_result result, const string &white_name, const string &black_name,
                   chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   void move_played(const string &move);
   bool check_for_repetition_draw(void);
};
