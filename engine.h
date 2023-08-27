#include <boost/process.hpp>
#include <string>
#include <iostream>
#include <vector>

namespace bp = boost::process;
using namespace std;

typedef unsigned int uint;

enum game_result
{
   UNFINISHED,                // game is still ongoing
   WHITE_WIN,                 // 1-0
   BLACK_WIN,                 // 0-1
   DRAW,                      // 1/2-1/2
   NO_LEGAL_MOVES,            // engine has no legal moves due to checkmate or stalemate
   UNDETERMINED,              // could not determine result
   ERROR_ILLEGAL_MOVE,        // engine reported an illegal move from its opponent
   ERROR_INVALID_POSITION,    // engine reported an invalid FEN position
   ERROR_ENGINE_DISCONNECTED  // could not read data from engine
};

enum player_color
{
   WHITE,
   BLACK
};

enum engine_number
{
   FIRST,
   SECOND
};

void rstrip(string &s);
void lstrip(string &s);
string get_first_token(const string &s, size_t pos);
vector<string> get_tokens(const string &s);

class Engine
{
public:
   bool m_uci;
   uint m_ID;                 // 1 through N, where N = total number of engine instances running
   engine_number m_number;    // FIRST or SECOND
   string m_file_name;
   string m_name;
   string m_move;
   bool m_is_ready;
   bool m_quit_cmd_sent;
   bool m_resigned;

private:
   bp::child *m_child_proc;
   bp::opstream m_in_stream;
   bp::ipstream m_out_stream;
   game_result m_result;
   string m_line;
   player_color m_color;
   int m_score;               // Currently, only tracking latest eval score for UCI engines.

   const int mate_score = 100000;
   const int mate_score_neg = (0 - mate_score);

public:
   // functions
   Engine(void);
   ~Engine(void);
   int load_engine(const string &eng_file_name, int ID, engine_number engine_num, bool uci);
   void send_engine_cmd(const string &cmd);
   void send_quit_cmd(void);
   int readline(void);
   int get_engine_move(void);
   int wait_for_ready(bool check_output);
   int engine_new_game_setup(player_color color, int64_t start_time_ms, int64_t inc_time_ms, int64_t fixed_time_ms, const string &fen, const string &variant);
   void engine_new_game_start(int64_t start_time_ms, int64_t inc_time_ms, int64_t fixed_time_ms);
   void send_move_and_clocks_to_engine(const string &move, const string &startfen, const string &movelist, int64_t engine_clock_ms, int64_t opp_clock_ms, int64_t inc_ms, int64_t fixed_time_ms);
   void send_result_to_engine(game_result result);
   void check_engine_output(void);
   bool is_running(void);
   void force_exit(void);
   bool has_checkmate(void);
   bool is_checkmated(void);
   bool is_checkmating(void);
   bool is_getting_checkmated(void);
   bool got_decisive_result(void);
   game_result get_game_result(void);
   void update_game_result(void);
};
