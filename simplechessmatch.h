#include "gamemanager.h"
#include <boost/program_options.hpp>
#include <fstream>
#ifdef WIN32
#include <conio.h>
#else
#include <signal.h>
#endif
#ifdef __linux__
#include <termios.h>
#endif

#define MAX_THREADS 32

int parse_cmd_line_options(int argc, char* argv[]);
#ifdef WIN32
BOOL WINAPI ctrl_c_handler(DWORD fdwCtrlType);
#else
void ctrl_c_handler(int s);
int _kbhit(void);
#endif

class MatchManager
{
public:
   GameManager *m_game_mgr;
   fstream m_FENs_file;

private:
   thread *m_thread;
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
   void allocate_threads(void);
   int load_all_engines(void);
   void shut_down_all_engines(void);
   void set_engine_options(Engine *engine);
   void send_engine_custom_commands(Engine *engine);
   void print_results(void);
   int get_next_fen(string &fen);
};
