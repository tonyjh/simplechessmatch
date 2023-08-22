#include "simplechessmatch.h"

namespace po = boost::program_options;

struct options_info options;
MatchManager match_mgr;

int main(int argc, char* argv[])
{
   cout << "simplechessmatch\n";

   if (parse_cmd_line_options(argc, argv) == 0)
      return 0;

   if (!options.fens_filename.empty())
   {
      match_mgr.m_FENs_file.open(options.fens_filename, ios::in);
      if (!match_mgr.m_FENs_file.is_open())
      {
         cout << "Error: could not open file " << options.fens_filename << "\n";
         return 0;
      }
   }

   if (options.engine_file_name_1.empty() || options.engine_file_name_2.empty())
   {
      cout << "Error: must specify two engines\n";
      return 0;
   }

   cout << "loading engines...\n";

   if (match_mgr.load_all_engines() == 0)
   {
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

   match_mgr.print_results();

   match_mgr.cleanup();

   cout << "Exiting.\n";

   return 0;
}

MatchManager::MatchManager(void)
{
   m_total_games_started = 0;
   m_engines_shut_down = false;
}

MatchManager::~MatchManager(void)
{
}

void MatchManager::cleanup(void)
{
   if (m_FENs_file.is_open())
      m_FENs_file.close();

   shut_down_all_engines();

   for (uint i = 0; i < options.num_threads; i++)
      if (m_game_mgr[i].m_thread.joinable())
         m_game_mgr[i].m_thread.join();

   this_thread::sleep_for(100ms);
}

void MatchManager::main_loop(void)
{
   string fen;
   bool swap_sides = false;

   cout << "\n***** Press any key to exit and terminate match *****\n\n";

   while (!match_completed())
   {
      for (uint i = 0; i < options.num_threads; i++)
      {
         if (!new_game_can_start())
            break;
         if (m_game_mgr[i].m_thread_running == 0)
         {
            if (m_game_mgr[i].m_thread.joinable())
               m_game_mgr[i].m_thread.join();

            if (!swap_sides)
               if (get_next_fen(fen) == 0)
                  return;
            m_game_mgr[i].m_fen = fen;
            m_game_mgr[i].m_swap_sides = swap_sides;
            swap_sides = !swap_sides;

            // cout << "Starting thread " << i << ", swap: " << m_game_mgr[i].m_swap_sides << ", FEN: [" << m_game_mgr[i].m_fen << "]\n";
            m_game_mgr[i].m_thread_running = true;
            m_game_mgr[i].m_thread = thread(&GameManager::game_runner, &m_game_mgr[i]);
            m_total_games_started++;
         }
      }
      while (!new_game_can_start() && !match_completed())
      {
         this_thread::sleep_for(200ms);
         print_results();
         if (_kbhit())
            return;
         for (uint i = 0; i < options.num_threads; i++)
            if (m_game_mgr[i].is_engine_unresponsive() || ((options.continue_on_error == 0) && m_game_mgr[i].m_error))
               return;
      }
   }
}

bool MatchManager::match_completed(void)
{
   return ((m_total_games_started >= options.num_games_to_play) && (num_games_in_progress() == 0));
}

bool MatchManager::new_game_can_start(void)
{
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

   if (illegal_move_games != 0)
      cout << "Engine1 (" << options.engine_file_name_1 << "): " << engine1_wins << " wins. Engine2 (" << options.engine_file_name_2 << "): " << engine2_wins <<  " wins.  " << draws << " draws.  " << illegal_move_games << " games ended with illegal move.\n";
   else
      cout << "Engine1 (" << options.engine_file_name_1 << "): " << engine1_wins << " wins. Engine2 (" << options.engine_file_name_2 << "): " << engine2_wins <<  " wins.  " << draws << " draws.\n";
   if ((engine1_losses_on_time != 0) || (engine2_losses_on_time != 0))
      cout << "Engine1 (" << options.engine_file_name_1 << "): " << engine1_losses_on_time << " losses on time. Engine2 (" << options.engine_file_name_2 << "): " << engine2_losses_on_time <<  " losses on time.\n";

   return;
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

int parse_cmd_line_options(int argc, char* argv[])
{
   try
   {
      po::options_description desc("Command line options");
      desc.add_options()
         ("help",      "print help message")
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
         ("tc",         po::value<uint>(&options.tc_ms)->default_value(10000), "time control base time (ms)")
         ("inc",        po::value<uint>(&options.tc_inc_ms)->default_value(100), "time control increment (ms)")
         ("fixed",      po::value<uint>(&options.tc_fixed_time_move_ms)->default_value(0), "time control fixed time per move (ms). This must be set to 0, unless engines should simply use a fixed amount of time per move.")
         ("margin",     po::value<uint>(&options.margin_ms)->default_value(50), "An engine loses on time if its clock goes below zero for this amount of time (ms).")
         ("games",      po::value<uint>(&options.num_games_to_play)->default_value(1000000), "total number of games to play")
         ("threads",    po::value<uint>(&options.num_threads)->default_value(1), "number of concurrent games to run")
         ("maxmoves",   po::value<uint>(&options.max_moves)->default_value(1000), "maximum number of moves per game (total) before adjudicating draw")
         ("fens",       po::value<string>(&options.fens_filename), "file containing FENs for opening positions (one FEN per line)")
         ("variant",    po::value<string>(&options.variant), "variant name")
         ("4pc",                    "enable 4 player chess (teams) mode")
         ("continue",               "continue match if error occurs (e.g. illegal move)")
         ("pmoves",                 "print out all moves");

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
      options.continue_on_error = (var_map.count("continue") != 0);
      options.print_moves = (var_map.count("pmoves") != 0);
      options.fourplayerchess = (var_map.count("4pc") != 0);
   }
   catch (exception &e)
   {
      cerr << "error: " << e.what() << "\n";
      return 0;
   }
   catch (...)
   {
      cerr << "error processing command line options\n";
   }

   if (options.num_threads > MAX_THREADS)
      options.num_threads = MAX_THREADS;
   if (options.num_threads > options.num_games_to_play)
      options.num_threads = options.num_games_to_play;

   return 1;
}

#ifndef WIN32
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
#endif
