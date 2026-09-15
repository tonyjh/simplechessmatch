#include "gamemanager.h"
#include <boost/program_options.hpp>
#include <fstream>
#include <math.h>
#include <mutex>
#ifdef WIN32
#include <conio.h>
#include <windows.h>
#else
#include <signal.h>
#endif
#ifdef __linux__
#include <termios.h>
#endif

#define MAX_THREADS 32

struct AggregatedResults {
   uint wins[2] = {};
   uint draws = 0;
   uint total_games = 0;
   uint illegal_move_games = 0;
   uint losses_on_time[2] = {};
   uint64_t total_depth[2] = {};
   uint64_t total_sel_depth[2] = {};
   uint64_t total_time_ms[2] = {};
   uint64_t total_nodes[2] = {};
   uint64_t total_moves[2] = {};
   uint64_t total_plies = 0;
   uint64_t total_game_time_ms = 0;
   double engine_score[2] = {};
   double avg_depth[2] = {};
   double avg_sel_depth[2] = {};
   double avg_time_per_move[2] = {};
   double nps[2] = {};
   double avg_plies_per_game = 0.0;
   double avg_game_duration = 0.0;
};

struct EloInfo {
   double elo_diff = 0.0;
   double elo_margin = 0.0;
   double nElo_diff = 0.0;
   double nElo_margin = 0.0;
   string elo_str;
   string nElo_str;
};

struct PairRecord {
   game_result g1 = UNFINISHED;
   game_result g2 = UNFINISHED;
};

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

private:
   thread *m_thread;
   uint m_total_games_started;
   bool m_engines_shut_down;
   fstream m_FENs_file;
   fstream m_pgn_file;
   string m_tc_str;
   chrono::time_point<chrono::steady_clock> m_match_start_time;

   vector<PairRecord> m_pair_records;
   int m_penta[5];
   void update_penta_stats(void);

   // Error messages
   std::mutex m_output_mutex;
   std::vector<std::string> m_error_messages;

   // SPRT related members
   bool m_sprt_enabled;
   double m_sprt_elo0;
   double m_sprt_elo1;
   double m_sprt_alpha;
   double m_sprt_beta;
   double m_sprt_lower_bound;
   double m_sprt_upper_bound;
   double m_sprt_llr;
   bool m_sprt_test_finished;

   enum SPRT_Decision {
      SPRT_NONE = -1,
      SPRT_H0 = 0,
      SPRT_H1 = 1
   };
   SPRT_Decision m_sprt_decision;

   int m_lines_printed;

public:
   MatchManager(void);
   ~MatchManager(void);
   void cleanup(void);
   void main_loop(void);
   int initialize(void);
   int load_all_engines(void);
   void set_engine_options(Engine *engine);
   void send_engine_custom_commands(Engine *engine);
   void log_error_message(const std::string& msg);
   void reset_cursor(void);
   void print_results(void);
   void print_error_messages(bool all);
   void print_final_results(void);
   void print_thread_results(void);
   void print_extended_results(AggregatedResults &r);
   void save_pgn(void);
   void shut_down_all_engines(void);

private:
   bool match_completed(void);
   bool new_game_can_start(void);
   uint num_games_in_progress(void);
   int get_next_fen(string &fen);
   void join_finished_threads(void);
   bool simple_output_mode(void);

   EloInfo calculate_elo(void);
   AggregatedResults aggregate_results(void);

   // Fishtest Statistical LLR Functions
   double secular(const double a[5], const double p[5]);
   void MLE_expected(const double a[5], const double p[5], double s, double p_MLE[5]);
   void MLE_t_value(const double a[5], const double p_hat[5], double ref, double t_target, double p_MLE[5]);
   double LLR_logistic(const double p_hat[5], double s0, double s1);
   double LLR_normalized(const double p_hat[5], double nelo0, double nelo1);
};
