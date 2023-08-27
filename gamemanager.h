#include "engine.h"
#include <thread>
#include <sstream>
#include <atomic>
#include <cctype>

player_color get_color_to_move_from_fen(string fen);
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
   player_color m_turn;
   uint m_num_moves;
   bool m_loss_on_time;
   chrono::time_point<std::chrono::steady_clock> m_timestamp; // This timestamp is updated whenever either engine's clock should start running.
                                                              // It's also updated when game_runner starts running.
   chrono::milliseconds m_white_clock_ms;
   chrono::milliseconds m_black_clock_ms;

public:
   GameManager(void);
   ~GameManager(void);
   void game_runner(void);
   game_result run_engine_game(chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   game_result determine_game_result(Engine *white_engine, Engine *black_engine);
   bool is_engine_unresponsive(void);
   void store_pgn(const string &movelist, game_result result, const string &white_name, const string &black_name,
                  chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
   void store_pgn4(const string &movelist, game_result result, const string &white_name, const string &black_name,
                   chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms);
};

struct options_info
{
   string engine_file_name_1;
   string engine_file_name_2;
   bool uci_1;
   bool uci_2;
   uint num_cores_1;
   uint num_cores_2;
   uint mem_size_1;
   uint mem_size_2;
   vector<string> custom_commands_1;
   vector<string> custom_commands_2;

   bool print_moves;
   bool continue_on_error;
   bool fourplayerchess;
   bool pgn4_format;
   // bool debug;
   uint tc_ms;
   uint tc_inc_ms;
   uint tc_fixed_time_move_ms;
   uint margin_ms;
   uint num_games_to_play;
   uint num_threads;
   uint max_moves;
   string fens_filename;
   string variant;
   string pgn_filename;
   string pgn4_filename;
};
