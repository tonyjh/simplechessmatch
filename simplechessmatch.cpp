#include "simplechessmatch.h"

namespace po = boost::program_options;

static string filename_from_path(const string &path)
{
   size_t pos = path.find_last_of("/\\");
   return (pos == string::npos) ? path : path.substr(pos + 1);
}

struct comma_numpunct : std::numpunct<char>
{
   char do_thousands_sep() const override { return ','; }
   string do_grouping() const override { return "\3"; }
};

struct options_info options;
MatchManager match_mgr;

int main(int argc, char* argv[])
{
   cout << "simplechessmatch\n";

#ifdef WIN32
   SetConsoleCtrlHandler(ctrl_c_handler, TRUE);
#else
   struct sigaction sig_handler;
   sig_handler.sa_handler = ctrl_c_handler;
   sigemptyset(&sig_handler.sa_mask);
   sig_handler.sa_flags = 0;
   sigaction(SIGINT, &sig_handler, NULL);
#endif

   if (parse_cmd_line_options(argc, argv) == 0)
      return 0;

   if (match_mgr.initialize() == 0)
      return 0;

   cout << "loading engines...\n";

   if (match_mgr.load_all_engines() == 0)
   {
      match_mgr.shut_down_all_engines();
      match_mgr.cleanup();
      return 0;
   }

   cout << "engines loaded.\n";

   for (uint i = 0; i < options.num_threads; i++)
   {
      match_mgr.set_engine_options(&(match_mgr.m_game_mgr[i].m_engine1));
      match_mgr.send_engine_custom_commands(&(match_mgr.m_game_mgr[i].m_engine1));
      match_mgr.set_engine_options(&(match_mgr.m_game_mgr[i].m_engine2));
      match_mgr.send_engine_custom_commands(&(match_mgr.m_game_mgr[i].m_engine2));
   }

   match_mgr.main_loop();

   match_mgr.shut_down_all_engines();
   match_mgr.print_thread_results();
   match_mgr.print_final_results();
   match_mgr.save_pgn();

   match_mgr.cleanup();

   cout << "Exiting.\n";

   return 0;
}

MatchManager::MatchManager(void)
{
   m_total_games_started = 0;
   m_engines_shut_down = false;
   m_game_mgr = nullptr;
   m_thread = nullptr;

   for (int i = 0; i < 5; i++) m_penta[i] = 0;

   m_sprt_enabled = false;
   m_sprt_test_finished = false;
   m_sprt_llr = 0.0;
   m_sprt_lower_bound = 0.0;
   m_sprt_upper_bound = 0.0;
   m_sprt_elo0 = 0.0;
   m_sprt_elo1 = 0.0;
   m_sprt_alpha = 0.0;
   m_sprt_beta = 0.0;
   m_sprt_decision = SPRT_NONE;
}

MatchManager::~MatchManager(void)
{
}

void MatchManager::cleanup(void)
{
   if (m_FENs_file.is_open())
      m_FENs_file.close();
   if (m_pgn_file.is_open())
      m_pgn_file.close();

   for (uint i = 0; i < options.num_threads; i++)
      if (m_thread[i].joinable())
         m_thread[i].join();

   delete[] m_game_mgr;
   delete[] m_thread;
}

void MatchManager::main_loop(void)
{
   string fen;
   bool swap_sides = false;
   uint current_pair_id = 0;

#if defined(WIN32) || defined(__linux__)
   // _kbhit is used to detect keypress
   cout << "\n***** Press any key to exit and terminate match *****\n\n";
#else
   cout << "\n***** Press Ctrl-C to exit and terminate match *****\n\n";
#endif

   m_match_start_time = chrono::steady_clock::now();

   while (!match_completed())
   {
      // 1. Join finished threads and record results
      join_finished_threads();

      // 2. Start new games
      for (uint i = 0; i < options.num_threads; i++)
      {
         if (!new_game_can_start())
            break;
         if (!m_game_mgr[i].m_thread_running && !m_thread[i].joinable())
         {
            if (!swap_sides) {
               if (get_next_fen(fen) == 0) {
                  // Gracefully stop starting new games by pretending we hit our target game count.
                  options.num_games_to_play = m_total_games_started;
                  break;
               }
            }

            m_game_mgr[i].m_fen = fen;
            m_game_mgr[i].m_swap_sides = swap_sides;
            m_game_mgr[i].m_pair_id = current_pair_id;
            if (swap_sides) current_pair_id++;
            swap_sides = !swap_sides;

            // cout << "Starting thread " << i << ", swap: " << m_game_mgr[i].m_swap_sides << ", pair ID: " << m_game_mgr[i].m_pair_id << " , FEN: [" << m_game_mgr[i].m_fen << "]\n";
            m_game_mgr[i].m_thread_running = true;
            m_thread[i] = thread(&GameManager::game_runner, &m_game_mgr[i]);
            m_total_games_started++;
         }
      }

      // 3. Wait until a thread finishes
      while (!new_game_can_start() && !match_completed())
      {
         this_thread::sleep_for(200ms);
         join_finished_threads();
         print_results();
         save_pgn();
         if (_kbhit())
            return;
         for (uint i = 0; i < options.num_threads; i++)
            if (m_game_mgr[i].m_engine_disconnected || m_game_mgr[i].is_engine_unresponsive() || (!options.continue_on_error && m_game_mgr[i].m_error))
               return;
      }
   }

   // Join any remaining finished threads
   join_finished_threads();
}

void MatchManager::join_finished_threads(void)
{
   for (uint i = 0; i < options.num_threads; i++)
   {
      if (!m_game_mgr[i].m_thread_running && m_thread[i].joinable())
      {
         m_thread[i].join();
         uint pid = m_game_mgr[i].m_pair_id;
         if (!m_game_mgr[i].m_swap_sides) m_pair_records[pid].g1 = m_game_mgr[i].m_final_result;
         else                             m_pair_records[pid].g2 = m_game_mgr[i].m_final_result;
      }
   }
   update_penta_stats();
}

bool MatchManager::match_completed(void)
{
   if (m_sprt_enabled && m_sprt_test_finished)
      return (num_games_in_progress() == 0);
   else
      return ((m_total_games_started >= options.num_games_to_play) && (num_games_in_progress() == 0));
}

bool MatchManager::new_game_can_start(void)
{
   if (m_sprt_enabled && m_sprt_test_finished)
      return false;
   else
      return ((m_total_games_started < options.num_games_to_play) && (num_games_in_progress() < options.num_threads));
}

uint MatchManager::num_games_in_progress(void)
{
   uint games = 0;
   for (uint i = 0; i < options.num_threads; i++)
      if (m_game_mgr[i].m_thread_running)
         games++;
   return games;
}

int MatchManager::initialize(void)
{
   if (options.engine_file_name_1.empty() || options.engine_file_name_2.empty())
   {
      cout << "Error: must specify two engines\n";
      return 0;
   }

   m_sprt_enabled = options.sprt_enabled;
   if (m_sprt_enabled) {
      m_sprt_elo0 = options.sprt_elo0;
      m_sprt_elo1 = options.sprt_elo1;
      m_sprt_alpha = options.sprt_alpha;
      m_sprt_beta = options.sprt_beta;
      m_sprt_lower_bound = log(m_sprt_beta / (1.0 - m_sprt_alpha));
      m_sprt_upper_bound = log((1.0 - m_sprt_beta) / m_sprt_alpha);
      cout << "SPRT test enabled with elo0=" << m_sprt_elo0 << ", elo1=" << m_sprt_elo1
           << ", alpha=" << m_sprt_alpha << ", beta=" << m_sprt_beta << " (" << options.sprt_elo_model << ")\n";
      cout << "SPRT bounds:[" << m_sprt_lower_bound << ", " << m_sprt_upper_bound << "]\n";
   }

   if (!options.fens_filename.empty())
   {
      m_FENs_file.open(options.fens_filename, ios::in);
      if (!m_FENs_file.is_open())
      {
         cout << "Error: could not open FEN file " << options.fens_filename << "\n";
         return 0;
      }
   }

   if (!options.pgn_filename.empty() && !options.pgn4_filename.empty())
   {
      cout << "Error: must not choose both PGN and PGN4\n";
      return 0;
   }

   if (!options.pgn_filename.empty() || !options.pgn4_filename.empty())
   {
      options.pgn4_format = options.pgn_filename.empty();
      string filename = (options.pgn4_format) ? options.pgn4_filename : options.pgn_filename;
      m_pgn_file.open(filename, ios::out);
      if (!m_pgn_file.is_open())
      {
         cout << "Error: could not open PGN file " << filename << "\n";
         return 0;
      }
   }
   else
      options.pgn4_format = options.fourplayerchess;

   if (options.num_games_to_play % 2 != 0)
      options.num_games_to_play++; // ensure complete pairs

   int num_pairs = options.num_games_to_play / 2;
   m_pair_records.resize(num_pairs);

   m_game_mgr = new GameManager[options.num_threads];
   m_thread = new thread[options.num_threads];

   if (options.tc_fixed_time_move_ms > 0)
      m_tc_str = ((options.tc_fixed_time_move_ms % 1000) == 0) ? to_string(options.tc_fixed_time_move_ms / 1000) + "s fixed" :
                                                               to_string(options.tc_fixed_time_move_ms) + "ms fixed";
   else
   {
      string tc_base_str = ((options.tc_ms % 1000) == 0) ? to_string(options.tc_ms / 1000) + "s + " : to_string(options.tc_ms) + "ms + ";
      string tc_inc_str  = ((options.tc_inc_ms % 1000) == 0) ? to_string(options.tc_inc_ms / 1000) + "s" : to_string(options.tc_inc_ms) + "ms";
      m_tc_str = tc_base_str + tc_inc_str;
   }

   return 1;
}

int MatchManager::load_all_engines(void)
{
   for (uint i = 0; i < options.num_threads; i++)
   {
      if (m_game_mgr[i].m_engine1.load_engine(options.engine_file_name_1, i * 2 + 1, FIRST, options.uci_1) == 0)
      {
         cout << "failed to load engine " << options.engine_file_name_1 << "\n";
         return 0;
      }
      if (m_game_mgr[i].m_engine2.load_engine(options.engine_file_name_2, i * 2 + 2, SECOND, options.uci_2) == 0)
      {
         cout << "failed to load engine " << options.engine_file_name_2 << "\n";
         return 0;
      }
   }
   return 1;
}

void MatchManager::shut_down_all_engines(void)
{
   if (m_engines_shut_down)
      return;

   m_engines_shut_down = true;

   for (uint i = 0; i < options.num_threads; i++)
   {
      m_game_mgr[i].m_engine1.send_quit_cmd();
      m_game_mgr[i].m_engine2.send_quit_cmd();
   }

   cout << "shutting down engines...\n";

   // poll for all engines shut down, for up to 500 ms.
   int num_engines_running;
   for (int x = 0; x < 10; x++)
   {
      this_thread::sleep_for(50ms);
      num_engines_running = 0;
      for (uint i = 0; i < options.num_threads; i++)
         num_engines_running += (m_game_mgr[i].m_engine1.is_running() + m_game_mgr[i].m_engine2.is_running());
      if (num_engines_running == 0)
         return;
   }

   for (uint i = 0; i < options.num_threads; i++)
   {
      m_game_mgr[i].m_engine1.force_exit();
      m_game_mgr[i].m_engine2.force_exit();
   }
}

void MatchManager::set_engine_options(Engine *engine)
{
   if ((engine->m_number == FIRST) && (options.mem_size_1 != 0))
   {
      if (engine->m_uci)
         engine->send_engine_cmd("setoption name Hash value " + to_string(options.mem_size_1));
      else
         engine->send_engine_cmd("memory " + to_string(options.mem_size_1));
   }
   if ((engine->m_number == SECOND) && (options.mem_size_2 != 0))
   {
      if (engine->m_uci)
         engine->send_engine_cmd("setoption name Hash value " + to_string(options.mem_size_2));
      else
         engine->send_engine_cmd("memory " + to_string(options.mem_size_2));
   }

   if ((engine->m_number == FIRST) && (options.num_cores_1 != 0))
   {
      if (engine->m_uci)
         engine->send_engine_cmd("setoption name Threads value " + to_string(options.num_cores_1));
      else
         engine->send_engine_cmd("cores " + to_string(options.num_cores_1));
   }
   if ((engine->m_number == SECOND) && (options.num_cores_2 != 0))
   {
      if (engine->m_uci)
         engine->send_engine_cmd("setoption name Threads value " + to_string(options.num_cores_2));
      else
         engine->send_engine_cmd("cores " + to_string(options.num_cores_2));
   }
   if (!engine->m_uci && options.fourplayerchess && !options.legacy_clocks)
      engine->send_engine_cmd("option Separate Clocks=1"); // for xboard 4pc engines
}

void MatchManager::send_engine_custom_commands(Engine *engine)
{
   if (engine->m_number == FIRST)
   {
      for (int i = 0; i < options.custom_commands_1.size(); i++)
         engine->send_engine_cmd(options.custom_commands_1[i]);
   }
   else
   {
      for (int i = 0; i < options.custom_commands_2.size(); i++)
         engine->send_engine_cmd(options.custom_commands_2[i]);
   }
}

void MatchManager::print_results(void)
{
   uint engine1_wins, engine2_wins, draws, illegal_move_games, engine1_losses_on_time, engine2_losses_on_time;
   engine1_wins = engine2_wins = draws = illegal_move_games = engine1_losses_on_time = engine2_losses_on_time = 0;

   // don't print results again unless the total number of games completed has changed.
   static int last_total_games_completed = 0;
   int total_games_completed = m_total_games_started - num_games_in_progress();
   if (total_games_completed == last_total_games_completed)
      return;
   last_total_games_completed = total_games_completed;

   for (uint i = 0; i < options.num_threads; i++)
   {
      engine1_wins += m_game_mgr[i].m_engine1_wins;
      engine2_wins += m_game_mgr[i].m_engine2_wins;
      draws += m_game_mgr[i].m_draws;
      illegal_move_games += m_game_mgr[i].m_illegal_move_games;
      engine1_losses_on_time += m_game_mgr[i].m_engine1_losses_on_time;
      engine2_losses_on_time += m_game_mgr[i].m_engine2_losses_on_time;
   }

   int total = engine1_wins + engine2_wins + draws;
   double engine1_score = (total > 0) ? ((double)engine1_wins + (double)draws / 2.0) / (double)total : 0.5;
   double engine2_score = (total > 0) ? ((double)engine2_wins + (double)draws / 2.0) / (double)total : 0.5;
   double elo_diff = (total > 0) ? log10(1.0 / engine2_score - 1.0) * 400.0 : 0.0;
   string name1 = filename_from_path(options.engine_file_name_1);
   string name2 = filename_from_path(options.engine_file_name_2);

   cout << "[games " << total_games_completed << "/" << options.num_games_to_play << "]  "
        << "[" << name1 << " vs " << name2 << "]  "
        << "[" << m_tc_str << "]  "
        << "W1:" << engine1_wins << "  W2:" << engine2_wins << "  D:" << draws
        << "  " << fixed << setprecision(1) << 100.0 * engine1_score << "%"
        << "  elo " << showpos << setprecision(2) << elo_diff << noshowpos;

   if (illegal_move_games != 0)
      cout << "  [illegal:" << illegal_move_games << "]";
   if ((engine1_losses_on_time != 0) || (engine2_losses_on_time != 0))
      cout << "  [time losses:" << engine1_losses_on_time << "/" << engine2_losses_on_time << "]";

   cout << "\n";

   print_extended_results();
}

void MatchManager::print_final_results(void)
{
   uint engine1_wins, engine2_wins, draws, illegal_move_games, engine1_losses_on_time, engine2_losses_on_time;
   engine1_wins = engine2_wins = draws = illegal_move_games = engine1_losses_on_time = engine2_losses_on_time = 0;

   uint64_t total_depth1 = 0, total_depth2 = 0;
   uint64_t total_sel_depth1 = 0, total_sel_depth2 = 0;
   uint64_t total_time1 = 0, total_time2 = 0;
   uint64_t total_nodes1 = 0, total_nodes2 = 0;
   uint64_t total_moves1 = 0, total_moves2 = 0;

   for (uint i = 0; i < options.num_threads; i++)
   {
      engine1_wins += m_game_mgr[i].m_engine1_wins;
      engine2_wins += m_game_mgr[i].m_engine2_wins;
      draws += m_game_mgr[i].m_draws;
      illegal_move_games += m_game_mgr[i].m_illegal_move_games;
      engine1_losses_on_time += m_game_mgr[i].m_engine1_losses_on_time;
      engine2_losses_on_time += m_game_mgr[i].m_engine2_losses_on_time;

      total_depth1 += m_game_mgr[i].m_engine_total_depth[FIRST];
      total_depth2 += m_game_mgr[i].m_engine_total_depth[SECOND];
      total_sel_depth1 += m_game_mgr[i].m_engine_total_sel_depth[FIRST];
      total_sel_depth2 += m_game_mgr[i].m_engine_total_sel_depth[SECOND];
      total_time1 += m_game_mgr[i].m_engine_total_time_ms[FIRST];
      total_time2 += m_game_mgr[i].m_engine_total_time_ms[SECOND];
      total_nodes1 += m_game_mgr[i].m_engine_total_nodes[FIRST];
      total_nodes2 += m_game_mgr[i].m_engine_total_nodes[SECOND];
      total_moves1 += m_game_mgr[i].m_engine_num_moves[FIRST];
      total_moves2 += m_game_mgr[i].m_engine_num_moves[SECOND];
   }

   int total = engine1_wins + engine2_wins + draws;
   double engine1_score = (total > 0) ? ((double)engine1_wins + (double)draws / 2.0) / (double)total : 0.5;
   double engine2_score = 1.0 - engine1_score;
   double elo_diff = (total > 0) ? log10(1.0 / engine2_score - 1.0) * 400.0 : 0.0;

   string name1 = filename_from_path(options.engine_file_name_1);
   string name2 = filename_from_path(options.engine_file_name_2);

   int lw = 20; // label width
   int vw = 25; // value column width

   cout.imbue(locale(cout.getloc(), new comma_numpunct()));

   string fens_str = options.fens_filename.empty() ? "none" : options.fens_filename;

   auto elapsed = chrono::steady_clock::now() - m_match_start_time;
   int total_secs = (int)chrono::duration_cast<chrono::seconds>(elapsed).count();
   int hours = total_secs / 3600;
   int mins = (total_secs % 3600) / 60;
   int secs = total_secs % 60;

   string dur_str;
   if (hours > 0)
      dur_str = to_string(hours) + "h " + to_string(mins) + "m " + to_string(secs) + "s";
   else if (mins > 0)
      dur_str = to_string(mins) + "m " + to_string(secs) + "s";
   else
      dur_str = to_string(secs) + "s";

   cout << "\n";
   cout << "MATCH INFO\n\n";
   cout << "E1:              " << name1 << "\n";
   cout << "E2:              " << name2 << "\n";
   cout << "Time Control:    " << m_tc_str << "\n";
   cout << "Completed Games: " << total << " / " << options.num_games_to_play << "\n";
   cout << "FENs File:       " << fens_str << "\n";
   cout << "Match Duration:  " << dur_str << "\n";
   cout << "\n";

   cout << "MATCH STATISTICS\n";
   cout << "  " << left << setw(lw) << "" << right << setw(vw) << "E1" << setw(vw) << "E2" << "\n";
   cout << "  " << left << setw(lw) << "" << right << setw(vw) << "----------" << setw(vw) << "----------" << "\n";
   cout << "  " << left << setw(lw) << "Score %" << right << setw(vw) << fixed << setprecision(1) << 100.0 * engine1_score << "%" << setw(vw - 1) << fixed << setprecision(1) << 100.0 * engine2_score << "%\n";
   cout << "  " << left << setw(lw) << "W / D / L" << right << setw(vw) << (to_string(engine1_wins) + " / " + to_string(draws) + " / " + to_string(engine2_wins)) << setw(vw) << (to_string(engine2_wins) + " / " + to_string(draws) + " / " + to_string(engine1_wins)) << "\n";
   cout << "  " << left << setw(lw) << "Elo Difference" << right << setw(vw) << showpos << setprecision(2) << elo_diff << noshowpos << "\n";
   cout << "  " << left << setw(lw) << "Time Forfeits" << right << setw(vw) << engine1_losses_on_time << setw(vw) << engine2_losses_on_time << "\n";
   cout << "\n";

   cout << "ENGINE STATISTICS\n";
   cout << "  " << left << setw(lw) << "" << right << setw(vw) << "E1" << setw(vw) << "E2" << "\n";
   cout << "  " << left << setw(lw) << "" << right << setw(vw) << "----------" << setw(vw) << "----------" << "\n";
   cout << "  " << left << setw(lw) << "Avg Depth" << right
        << setw(vw) << fixed << setprecision(2) << (total_moves1 > 0 ? (double)total_depth1 / (double)total_moves1 : 0.0)
        << setw(vw) << fixed << setprecision(2) << (total_moves2 > 0 ? (double)total_depth2 / (double)total_moves2 : 0.0) << "\n";
   cout << "  " << left << setw(lw) << "Avg Selective Depth" << right
        << setw(vw) << fixed << setprecision(2) << (total_moves1 > 0 ? (double)total_sel_depth1 / (double)total_moves1 : 0.0)
        << setw(vw) << fixed << setprecision(2) << (total_moves2 > 0 ? (double)total_sel_depth2 / (double)total_moves2 : 0.0) << "\n";
   cout << "  " << left << setw(lw) << "Avg Time / Move (ms)" << right
        << setw(vw) << fixed << setprecision(0) << (total_moves1 > 0 ? (double)total_time1 / (double)total_moves1 : 0.0)
        << setw(vw) << fixed << setprecision(0) << (total_moves2 > 0 ? (double)total_time2 / (double)total_moves2 : 0.0) << "\n";
   cout << "  " << left << setw(lw) << "NPS" << right
        << setw(vw) << fixed << setprecision(0) << (total_time1 > 0 ? (double)total_nodes1 * 1000.0 / (double)total_time1 : 0.0)
        << setw(vw) << fixed << setprecision(0) << (total_time2 > 0 ? (double)total_nodes2 * 1000.0 / (double)total_time2 : 0.0) << "\n";

   if (illegal_move_games != 0)
      cout << "\n  [games ending in illegal move: " << illegal_move_games << "]";
   cout << "\n";
}

void MatchManager::print_thread_results(void)
{
   int lw = 10;  // thread number width
   int ww = 8;   // wins/draws width
   int nw = 18;  // nps width

   locale comma_locale(cout.getloc(), new comma_numpunct());
   cout.imbue(comma_locale);

   cout << "\n";
   cout << "THREAD RESULTS\n";
   cout << "\n";
   cout << "  " << left << setw(lw) << "Thread" << right
        << setw(ww) << "W(E1)" << setw(ww) << "W(E2)" << setw(ww) << "D"
        << setw(nw) << "NPS(E1)" << setw(nw) << "NPS(E2)" << "\n";

   for (uint i = 0; i < options.num_threads; i++)
   {
      double nps1 = m_game_mgr[i].m_engine_total_time_ms[FIRST] > 0 ? (double)m_game_mgr[i].m_engine_total_nodes[FIRST] * 1000.0 / (double)m_game_mgr[i].m_engine_total_time_ms[FIRST] : 0.0;
      double nps2 = m_game_mgr[i].m_engine_total_time_ms[SECOND] > 0 ? (double)m_game_mgr[i].m_engine_total_nodes[SECOND] * 1000.0 / (double)m_game_mgr[i].m_engine_total_time_ms[SECOND] : 0.0;
      cout << "  " << left << setw(lw) << i
           << right << setw(ww) << m_game_mgr[i].m_engine1_wins
           << setw(ww) << m_game_mgr[i].m_engine2_wins
           << setw(ww) << m_game_mgr[i].m_draws
           << setw(nw) << fixed << setprecision(0) << nps1
           << setw(nw) << fixed << setprecision(0) << nps2 << "\n";
   }
}

void MatchManager::print_extended_results(void)
{
   uint engine1_wins = 0, engine2_wins = 0, draws = 0;
   uint illegal_move_games = 0, engine1_losses_on_time = 0, engine2_losses_on_time = 0;

   for (uint i = 0; i < options.num_threads; i++)
   {
      engine1_wins += m_game_mgr[i].m_engine1_wins;
      engine2_wins += m_game_mgr[i].m_engine2_wins;
      draws += m_game_mgr[i].m_draws;
      illegal_move_games += m_game_mgr[i].m_illegal_move_games;
      engine1_losses_on_time += m_game_mgr[i].m_engine1_losses_on_time;
      engine2_losses_on_time += m_game_mgr[i].m_engine2_losses_on_time;
   }

   int N_games = engine1_wins + engine2_wins + draws;
   int N_pairs = m_penta[0] + m_penta[1] + m_penta[2] + m_penta[3] + m_penta[4];

   stringstream ss_output;

   if (N_pairs == 0) {
      ss_output << "No game pairs completed yet." << endl;
   } else {
      double p[5] = {0.0};
      for (int k = 0; k < 5; ++k) p[k] = (double)m_penta[k] / N_pairs;

      double score = 0.0;
      for (int k = 0; k < 5; ++k) score += p[k] * (k * 0.25);

      double var_pair_avg = 0.0;
      for (int k = 0; k < 5; ++k) {
         double diff = (k * 0.25) - score;
         var_pair_avg += p[k] * diff * diff;
      }

      ss_output << fixed << setprecision(2);

      if (score <= 1e-9 || score >= 1.0 - 1e-9) {
         ss_output << "Elo   | " << (score > 0.5 ? "+inf" : "-inf") << endl;
         ss_output << "nElo  | " << (score > 0.5 ? "+inf" : "-inf") << endl;
      } else {
         double std_error_of_mean_score = sqrt(var_pair_avg / N_pairs);

         // 1. Classical / Logistic Elo (Exact bounds calculation)
         auto to_elo = [](double s) {
             if (s < 1e-5) s = 1e-5;
             if (s > 1.0 - 1e-5) s = 1.0 - 1e-5;
             return -400.0 * log10(1.0 / s - 1.0);
         };
         double elo_diff = to_elo(score);
         double mu_min = score - 1.96 * std_error_of_mean_score;
         double mu_max = score + 1.96 * std_error_of_mean_score;
         double elo_margin = (to_elo(mu_max) - to_elo(mu_min)) / 2.0;

         // 2. Normalized Elo (nElo)
         double sigma_pg = sqrt(2.0 * var_pair_avg);
         double nElo = 0.0, margin_nElo = 0.0;
         if (sigma_pg > 1e-9) {
            double nt = (score - 0.5) / sigma_pg;
            nElo = nt * (800.0 / log(10.0));
            double std_err_nt = std_error_of_mean_score / sigma_pg;
            margin_nElo = 1.96 * std_err_nt * (800.0 / log(10.0));
         }

         ss_output << "Elo   | " << elo_diff << " +- " << elo_margin << " (95%)" << endl;
         ss_output << "nElo  | " << nElo << " +- " << margin_nElo << " (95%)" << endl;
      }

      if (m_sprt_enabled) {
         stringstream tc_ss;
         if (options.tc_fixed_time_move_ms > 0)
            tc_ss << fixed << setprecision(2) << (float)options.tc_fixed_time_move_ms / 1000.0 << "s";
         else
            tc_ss << options.tc_ms / 1000 << "+" << fixed << setprecision(2) << (float)options.tc_inc_ms / 1000.0;

         string thread_str = "Th=" + to_string(options.num_cores_1) + (options.num_cores_1 == options.num_cores_2 ? "" : "/" + to_string(options.num_cores_2));
         string hash_str = "Hash=" + to_string(options.mem_size_1) + "MB" + (options.mem_size_1 == options.mem_size_2 ? "" : "/" + to_string(options.mem_size_2) + "MB");

         ss_output << "SPRT  | " << tc_ss.str() << " " << thread_str << " " << hash_str << " Conc=" << options.num_threads << endl;
         ss_output << "LLR   | " << m_sprt_llr << " (" << m_sprt_lower_bound << ", " << m_sprt_upper_bound 
                   << ") [" << m_sprt_elo0 << ", " << m_sprt_elo1 << " " << options.sprt_elo_model << "]" << endl;
      }

      ss_output << "Games | N: " << N_games << " W: " << engine1_wins << " L: " << engine2_wins << " D: " << draws << " Completed Pairs: " << N_pairs << endl;
      ss_output << "Penta | " << m_penta[0] << " " << m_penta[1] << " " << m_penta[2] << " " << m_penta[3] << " " << m_penta[4] << endl;

      stringstream ss;
      if (illegal_move_games != 0) ss << " [Illegal Moves: " << illegal_move_games << "]";
      if (engine1_losses_on_time != 0 || engine2_losses_on_time != 0) ss << " [Timeouts: " << engine1_losses_on_time << " / " << engine2_losses_on_time << "]";
      if (ss.str().length() > 0) ss_output << "Info  |" << ss.str() << endl;

      if (m_sprt_enabled && m_sprt_test_finished) {
         ss_output << "\nSPRT test finished: ";
         if (m_sprt_decision == SPRT_H1) ss_output << "H1 accepted (Engine 1 is stronger)." << endl;
         else if (m_sprt_decision == SPRT_H0) ss_output << "H0 accepted (elo is within bounds)." << endl;
      }
   }

   string output_str = ss_output.str();

   cout << output_str;
}

int MatchManager::get_next_fen(string &fen)
{
   if (!m_FENs_file.is_open())
   {
      fen = "";
      return 1;
   }
   getline(m_FENs_file, fen);
   if (fen.empty())
   {
      cout << "Used all FENs.\n";
      return 0;
   }
   return 1;
}

void MatchManager::save_pgn(void)
{
   if (!m_pgn_file.is_open())
      return;

   for (uint i = 0; i < options.num_threads; i++)
   {
      if (m_game_mgr[i].m_pgn_valid.load(memory_order_acquire))
      {
         m_game_mgr[i].m_pgn_valid = false;
         m_pgn_file << m_game_mgr[i].m_pgn;
      }
   }
}

void MatchManager::update_penta_stats(void)
{
   for (int k = 0; k < 5; k++) m_penta[k] = 0;
   int completed_pairs = 0;

   for (uint pid = 0; pid < m_pair_records.size(); pid++) {
      game_result g1 = m_pair_records[pid].g1;
      game_result g2 = m_pair_records[pid].g2;

      bool g1_done = (g1 == WHITE_WIN || g1 == BLACK_WIN || g1 == DRAW);
      bool g2_done = (g2 == WHITE_WIN || g2 == BLACK_WIN || g2 == DRAW);

      if (g1_done && g2_done) {
         double e1_score = 0.0;
         if (g1 == WHITE_WIN) e1_score += 1.0;
         else if (g1 == DRAW) e1_score += 0.5;

         if (g2 == BLACK_WIN) e1_score += 1.0;
         else if (g2 == DRAW) e1_score += 0.5;

         if      (e1_score == 0.0) m_penta[0]++;
         else if (e1_score == 0.5) m_penta[1]++;
         else if (e1_score == 1.0) m_penta[2]++;
         else if (e1_score == 1.5) m_penta[3]++;
         else if (e1_score == 2.0) m_penta[4]++;

         completed_pairs++;
      }
   }

   if (m_sprt_enabled && !m_sprt_test_finished && completed_pairs > 0) {
      double R[5];
      for (int k = 0; k < 5; ++k) {
         R[k] = m_penta[k];
         if (R[k] == 0.0) R[k] = 1e-3; // Fishtest epsilon
      }
      
      double N = 0.0;
      for (int k = 0; k < 5; ++k) N += R[k];
      
      double p_hat[5];
      for (int k = 0; k < 5; ++k) p_hat[k] = R[k] / N;

      if (options.sprt_elo_model == "normalized") {
         m_sprt_llr = N * LLR_normalized(p_hat, m_sprt_elo0, m_sprt_elo1);
      } else {
         double s0 = 1.0 / (1.0 + pow(10.0, -m_sprt_elo0 / 400.0));
         double s1 = 1.0 / (1.0 + pow(10.0, -m_sprt_elo1 / 400.0));
         m_sprt_llr = N * LLR_logistic(p_hat, s0, s1);
      }

      if (m_sprt_llr >= m_sprt_upper_bound) {
         m_sprt_test_finished = true;
         m_sprt_decision = SPRT_H1;
      } else if (m_sprt_llr <= m_sprt_lower_bound) {
         m_sprt_test_finished = true;
         m_sprt_decision = SPRT_H0;
      }
   }
}

// -------------------------------------------------------------------------
// FISHTEST MLE STATISTICAL FUNCTIONS
// -------------------------------------------------------------------------

double MatchManager::secular(const double a[5], const double p[5]) {
   double v = 1e9, w = -1e9;
   for (int k = 0; k < 5; ++k) {
      if (p[k] > 0.0) {
         if (a[k] < v) v = a[k];
         if (a[k] > w) w = a[k];
      }
   }
   if (v * w >= 0.0) return 0.0; 
   
   double L = -1.0 / w + 1e-9;
   double U = -1.0 / v - 1e-9;
   
   double x = 0.0;
   for (int iter = 0; iter < 100; ++iter) {
      x = 0.5 * (L + U);
      if (x == L || x == U) break;
      double f = 0.0;
      for (int k = 0; k < 5; ++k) {
         f += p[k] * a[k] / (1.0 + x * a[k]);
      }
      if (f > 0.0) L = x;
      else U = x;
   }
   return x;
}

void MatchManager::MLE_expected(const double a[5], const double p[5], double s, double p_MLE[5]) {
   double a_shifted[5];
   for (int k = 0; k < 5; ++k) a_shifted[k] = a[k] - s;
   double x = secular(a_shifted, p);
   for (int k = 0; k < 5; ++k) p_MLE[k] = p[k] / (1.0 + x * a_shifted[k]);
}

void MatchManager::MLE_t_value(const double a[5], const double p_hat[5], double ref, double t_target, double p_MLE[5]) {
   for (int k = 0; k < 5; ++k) p_MLE[k] = 0.2; 
   
   for (int iter = 0; iter < 10; ++iter) {
      double p_prev[5];
      for (int k = 0; k < 5; ++k) p_prev[k] = p_MLE[k];
      
      double mu = 0.0, var = 0.0;
      for (int k = 0; k < 5; ++k) mu += p_MLE[k] * a[k];
      for (int k = 0; k < 5; ++k) var += p_MLE[k] * (a[k] - mu) * (a[k] - mu);
      double sigma = sqrt(var);
      
      double a_shifted[5];
      for (int k = 0; k < 5; ++k) {
         double z = (mu - a[k]) / sigma;
         a_shifted[k] = a[k] - ref - t_target * sigma * (1.0 + z * z) / 2.0;
      }
      
      double x = secular(a_shifted, p_hat);
      
      double max_diff = 0.0;
      for (int k = 0; k < 5; ++k) {
         p_MLE[k] = p_hat[k] / (1.0 + x * a_shifted[k]);
         double diff = std::abs(p_prev[k] - p_MLE[k]);
         if (diff > max_diff) max_diff = diff;
      }
      if (max_diff < 1e-9) break;
   }
}

double MatchManager::LLR_logistic(const double p_hat[5], double s0, double s1) {
   double a[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
   double p_MLE0[5], p_MLE1[5];
   MLE_expected(a, p_hat, s0, p_MLE0);
   MLE_expected(a, p_hat, s1, p_MLE1);
   
   double llr = 0.0;
   for (int k = 0; k < 5; ++k) {
      llr += p_hat[k] * log(p_MLE1[k] / p_MLE0[k]);
   }
   return llr;
}

double MatchManager::LLR_normalized(const double p_hat[5], double nelo0, double nelo1) {
   double nelo_divided_by_nt = 800.0 / log(10.0);
   double t0 = (nelo0 / nelo_divided_by_nt) * sqrt(2.0);
   double t1 = (nelo1 / nelo_divided_by_nt) * sqrt(2.0);
   
   double a[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
   double p_MLE0[5], p_MLE1[5];
   MLE_t_value(a, p_hat, 0.5, t0, p_MLE0);
   MLE_t_value(a, p_hat, 0.5, t1, p_MLE1);
   
   double llr = 0.0;
   for (int k = 0; k < 5; ++k) {
      llr += p_hat[k] * log(p_MLE1[k] / p_MLE0[k]);
   }
   return llr;
}

int parse_cmd_line_options(int argc, char* argv[])
{
   try
   {
      po::options_description desc("Command line options");
      desc.add_options()
         ("help",       "print help message")
         ("e1",         po::value<string>(&options.engine_file_name_1), "first engine's file name")
         ("e2",         po::value<string>(&options.engine_file_name_2), "second engine's file name")
         ("x1",         "first engine uses xboard protocol. (UCI is the default protocol.)")
         ("x2",         "second engine uses xboard protocol. (UCI is the default protocol.)")
         ("cores1",     po::value<uint>(&options.num_cores_1)->default_value(1), "first engine number of cores")
         ("cores2",     po::value<uint>(&options.num_cores_2)->default_value(1), "second engine number of cores")
         ("mem1",       po::value<uint>(&options.mem_size_1)->default_value(128), "first engine memory usage (MB)")
         ("mem2",       po::value<uint>(&options.mem_size_2)->default_value(128), "second engine memory usage (MB)")
         ("custom1",    po::value<vector<string>>(&options.custom_commands_1), "first engine custom command. e.g. --custom1 \"setoption name Style value Risky\"")
         ("custom2",    po::value<vector<string>>(&options.custom_commands_2), "second engine custom command. Note: --custom1 and --custom2 can be used more than once in the command line.")
         ("debug1",     "enable debug for first engine")
         ("debug2",     "enable debug for second engine")
         ("tc",         po::value<uint>(&options.tc_ms)->default_value(10000), "time control base time (ms)")
         ("inc",        po::value<uint>(&options.tc_inc_ms)->default_value(100), "time control increment (ms)")
         ("fixed",      po::value<uint>(&options.tc_fixed_time_move_ms)->default_value(0), "time control fixed time per move (ms). This must be set to 0, unless engines should simply use a fixed amount of time per move.")
         ("margin",     po::value<uint>(&options.margin_ms)->default_value(50), "An engine loses on time if its clock goes below zero for this amount of time (ms).")
         ("games",      po::value<uint>(&options.num_games_to_play)->default_value(1000000), "total number of games to play")
         ("threads",    po::value<uint>(&options.num_threads)->default_value(1), "number of concurrent games to run")
         ("maxmoves",   po::value<uint>(&options.max_moves)->default_value(1000), "maximum number of moves per game (total) before adjudicating draw regardless of scores")
         ("earlywin",   "adjudicate win result early if both engines report mate scores")
         ("earlydraw",  "adjudicate draw result early if both engine scores are in range (-drawscore <= score <= drawscore) for a total of drawmoves moves")
         ("drawscore",  po::value<uint>(&options.draw_score)->default_value(25), "drawscore (centipawns) value for \"earlydraw\" setting")
         ("drawmoves",  po::value<uint>(&options.draw_moves)->default_value(20), "drawmoves value for \"earlydraw\" setting")
         ("fens",       po::value<string>(&options.fens_filename), "file containing FENs for opening positions (one FEN per line)")
         ("variant",    po::value<string>(&options.variant), "variant name")
         ("4pc",        "enable 4 player chess (teams) mode")
         ("legacy-clocks", "use legacy 2-clock system instead of independent 4-player clocks")
         ("continue",   "continue match if error occurs (e.g. illegal move)")
         ("pmoves",     "print out all moves")
         ("pgn",        po::value<string>(&options.pgn_filename), "save games in PGN format to specified file name\n(if file exists it will be overwritten)")
         ("pgn4",       po::value<string>(&options.pgn4_filename), "save games in PGN4 format to specified file name\n(if file exists it will be overwritten)")
         ("sprt",       "Enable SPRT test. Test stops when bounds are reached.")
         ("sprt-elo-model", po::value<string>(&options.sprt_elo_model)->default_value("normalized"), "SPRT Elo model ('normalized' or 'logistic')")
         ("sprt-elo0",  po::value<double>(&options.sprt_elo0)->default_value(0.0, "0.0"), "SPRT H0 (null hypothesis) Elo.")
         ("sprt-elo1",  po::value<double>(&options.sprt_elo1)->default_value(5.0, "5.0"), "SPRT H1 (alternative hypothesis) Elo.")
         ("sprt-alpha", po::value<double>(&options.sprt_alpha)->default_value(0.05, "0.05"), "SPRT alpha (type I error).")
         ("sprt-beta",  po::value<double>(&options.sprt_beta)->default_value(0.05, "0.05"), "SPRT beta (type II error).")
         ;

      po::variables_map var_map;
      po::store(po::parse_command_line(argc, argv, desc), var_map);
      po::notify(var_map);

      if (var_map.count("help"))
      {
         cout << desc << "\n";
         return 0;
      }

      options.uci_1 = (var_map.count("x1") == 0);
      options.uci_2 = (var_map.count("x2") == 0);
      options.debug_1 = (var_map.count("debug1") != 0);
      options.debug_2 = (var_map.count("debug2") != 0);
      options.continue_on_error = (var_map.count("continue") != 0);
      options.print_moves = (var_map.count("pmoves") != 0);
      options.fourplayerchess = (var_map.count("4pc") != 0);
      options.legacy_clocks = (var_map.count("legacy-clocks") != 0);
      options.early_win = (var_map.count("earlywin") != 0);
      options.early_draw = (var_map.count("earlydraw") != 0);

      options.sprt_enabled = (var_map.count("sprt") != 0);
      if (options.sprt_elo_model != "normalized" && options.sprt_elo_model != "logistic")
      {
         cerr << "error: --sprt-elo-model must be 'normalized' or 'logistic'\n";
         return 0;
      }
   }
   catch (exception &e)
   {
      cerr << "error: " << e.what() << "\n";
      return 0;
   }
   catch (...)
   {
      cerr << "error processing command line options\n";
      return 0;
   }

   if (options.num_threads > MAX_THREADS)
      options.num_threads = MAX_THREADS;
   if (options.num_threads > options.num_games_to_play)
      options.num_threads = options.num_games_to_play;

   return 1;
}

#ifndef WIN32
#ifdef __linux__
// Linux _kbhit code from https://www.flipcode.com/archives/_kbhit_for_Linux.shtml (by Morgan McGuire)
int _kbhit(void)
{
   static const int STDIN = 0;
   static bool initialized = false;

   if (!initialized)
   {
      // Use termios to turn off line buffering
      termios term;
      tcgetattr(STDIN, &term);
      term.c_lflag &= ~ICANON;
      tcsetattr(STDIN, TCSANOW, &term);
      setbuf(stdin, NULL);
      initialized = true;
   }

   int bytesWaiting;
   ioctl(STDIN, FIONREAD, &bytesWaiting);
   return bytesWaiting;
}
#else
// _kbhit not implemented
int _kbhit(void)
{
   return 0;
}
#endif
#endif

#ifdef WIN32
BOOL WINAPI ctrl_c_handler(DWORD fdwCtrlType)
{
   match_mgr.shut_down_all_engines();
   return true;
}
#else
void ctrl_c_handler(int s)
{
   match_mgr.shut_down_all_engines();
}
#endif
