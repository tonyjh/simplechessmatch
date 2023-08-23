#include "gamemanager.h"
#include <boost/program_options.hpp>
#include <fstream>
#ifdef WIN32
#include <conio.h>
#else
#include <termios.h>
#endif

#define MAX_THREADS 32

int parse_cmd_line_options(int argc, char* argv[]);
#ifndef WIN32
int _kbhit(void);
#endif

class MatchManager
{
public:
   GameManager m_game_mgr[MAX_THREADS];
   fstream m_FENs_file;

private:
   uint m_total_games_started;
   bool m_engines_shut_down;

public:
   MatchManager(void);
   ~MatchManager(void);
   void cleanup(void);
   void main_loop(void);
   bool match_completed(void);
   bool new_game_can_start(void);
   uint num_games_in_progress(void);
   int load_all_engines(void);
   void shut_down_all_engines(void);
   void set_engine_options(Engine *engine);
   void send_engine_custom_commands(Engine *engine);
   void print_results(void);
   int get_next_fen(string &fen);
};
