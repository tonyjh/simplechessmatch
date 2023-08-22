#include "gamemanager.h"

extern struct options_info options;

GameManager::GameManager(void)
{
   m_turn = WHITE;
   m_engine1_wins = 0;
   m_engine2_wins = 0;
   m_draws = 0;
   m_engine1_losses_on_time = 0;
   m_engine2_losses_on_time = 0;
   m_illegal_move_games = 0;
   m_thread_running = false;
   m_swap_sides = false;
   m_loss_on_time = false;
   m_error = false;
   m_num_moves = 0;
   m_white_clock_ms = chrono::milliseconds(0);
   m_black_clock_ms = chrono::milliseconds(0);
}

GameManager::~GameManager(void)
{
}

void GameManager::game_runner(void)
{
   game_result result;

   m_timestamp = chrono::steady_clock::now();
   m_loss_on_time = false;
   m_thread_running = true;
   m_num_moves = 0;

   result = run_engine_game(chrono::milliseconds(options.tc_ms), chrono::milliseconds(options.tc_inc_ms),
                            chrono::milliseconds(options.tc_fixed_time_move_ms));

   if (result == ERROR_ILLEGAL_MOVE)
      m_illegal_move_games++;
   else if (((result == WHITE_WIN) && !m_swap_sides) || ((result == BLACK_WIN) && m_swap_sides))
   {
      m_engine1_wins++;
      if (m_loss_on_time)
         m_engine2_losses_on_time++;
   }
   else if (((result == BLACK_WIN) && !m_swap_sides) || ((result == WHITE_WIN) && m_swap_sides))
   {
      m_engine2_wins++;
      if (m_loss_on_time)
         m_engine1_losses_on_time++;
   }
   else if (result == DRAW)
      m_draws++;

   m_thread_running = false;
}

game_result GameManager::run_engine_game(chrono::milliseconds start_time_ms, chrono::milliseconds increment_ms, chrono::milliseconds fixed_time_ms)
{
   chrono::milliseconds elapsed_time_ms;
   string movelist = "";
   game_result result = UNFINISHED;
   Engine *white_engine;
   Engine *black_engine;

   if (m_swap_sides)
   {
      white_engine = &m_engine2;
      black_engine = &m_engine1;
   }
   else
   {
      white_engine = &m_engine1;
      black_engine = &m_engine2;
   }

   movelist.reserve(1000);

   if (fixed_time_ms.count())
   {
      m_white_clock_ms = fixed_time_ms;
      m_black_clock_ms = fixed_time_ms;
   }
   else
   {
      m_white_clock_ms = start_time_ms;
      m_black_clock_ms = start_time_ms;
   }

   this_thread::sleep_for(100ms);

   m_turn = get_color_to_move_from_fen(m_fen);

   if (white_engine->engine_new_game_setup(WHITE, start_time_ms.count(), increment_ms.count(), fixed_time_ms.count(), m_fen, options.variant) == 0)
   {
      if (!white_engine->m_quit_cmd_sent)
         cout << "Error: " << white_engine->m_name << " could not start a new game.\n";
      return ERROR_ENGINE_DISCONNECTED;
   }
   if (black_engine->engine_new_game_setup(BLACK, start_time_ms.count(), increment_ms.count(), fixed_time_ms.count(), m_fen, options.variant) == 0)
   {
      if (!black_engine->m_quit_cmd_sent)
         cout << "Error: " << black_engine->m_name << " could not start a new game.\n";
      return ERROR_ENGINE_DISCONNECTED;
   }
   if (m_turn == WHITE)
      white_engine->engine_new_game_start(start_time_ms.count(), increment_ms.count(), fixed_time_ms.count());
   else
      black_engine->engine_new_game_start(start_time_ms.count(), increment_ms.count(), fixed_time_ms.count());

   m_timestamp = chrono::steady_clock::now();

   while ((white_engine->get_game_result() == UNFINISHED) && (black_engine->get_game_result() == UNFINISHED) && (m_num_moves < options.max_moves))
   {
      if (m_turn == WHITE)
      {
         if (!white_engine->get_engine_move())
         {
            if (!white_engine->m_quit_cmd_sent)
               cout << "Error: " << white_engine->m_name << " disconnected.\n";
            return ERROR_ENGINE_DISCONNECTED;
         }
         if (white_engine->m_move.empty())
            break; // no legal moves
         elapsed_time_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - m_timestamp);
         m_white_clock_ms = m_white_clock_ms - elapsed_time_ms;
         if (m_white_clock_ms.count() < (0 - (int)options.margin_ms))
         {
            cout << white_engine->m_name << " (white) ran out of time. " << m_white_clock_ms.count() << " ms\n";
            m_loss_on_time = true;
            result = BLACK_WIN;
            break;
         }
         m_white_clock_ms = (fixed_time_ms.count() ? (fixed_time_ms) : (m_white_clock_ms + increment_ms));

         movelist.append(white_engine->m_move + " ");
         black_engine->send_move_and_clocks_to_engine(white_engine->m_move, m_fen, movelist, m_black_clock_ms.count(), m_white_clock_ms.count(), increment_ms.count(), fixed_time_ms.count());
         m_timestamp = chrono::steady_clock::now();
         m_num_moves++;
         if (options.print_moves)
            cout << "white moved: " << white_engine->m_move << ",   elapsed: " << elapsed_time_ms.count() << " ms,   white clock: " << m_white_clock_ms.count() << " ms\n";
      }
      else
      {
         if (!black_engine->get_engine_move())
         {
            if (!black_engine->m_quit_cmd_sent)
               cout << "Error: " << black_engine->m_name << " disconnected.\n";
            return ERROR_ENGINE_DISCONNECTED;
         }
         if (black_engine->m_move.empty())
            break; // no legal moves
         elapsed_time_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - m_timestamp);
         m_black_clock_ms = m_black_clock_ms - elapsed_time_ms;
         if (m_black_clock_ms.count() < (0 - (int)options.margin_ms))
         {
            cout << black_engine->m_name << " (black) ran out of time. " << m_black_clock_ms.count() << " ms\n";
            m_loss_on_time = true;
            result = WHITE_WIN;
            break;
         }
         m_black_clock_ms = (fixed_time_ms.count() ? (fixed_time_ms) : (m_black_clock_ms + increment_ms));

         movelist.append(black_engine->m_move + " ");
         white_engine->send_move_and_clocks_to_engine(black_engine->m_move, m_fen, movelist, m_white_clock_ms.count(), m_black_clock_ms.count(), increment_ms.count(), fixed_time_ms.count());
         m_timestamp = chrono::steady_clock::now();
         m_num_moves++;
         if (options.print_moves)
            cout << "black moved: " << black_engine->m_move << ",   elapsed: " << elapsed_time_ms.count() << " ms,   black clock: " << m_black_clock_ms.count() << " ms\n";
      }

      m_turn = (m_turn == WHITE) ? BLACK : WHITE;
   }

   if (result == UNFINISHED)
   {
      // In case there is unread data (which may contain game result) from engine where result isn't known yet:
      if (white_engine->get_game_result() == UNFINISHED)
         white_engine->wait_for_ready(true);
      if (black_engine->get_game_result() == UNFINISHED)
         black_engine->wait_for_ready(true);

      result = determine_game_result(white_engine, black_engine);
   }

   white_engine->send_result_to_engine(result);
   black_engine->send_result_to_engine(result);

   if (result == ERROR_ILLEGAL_MOVE)
   {
      cout << "\n" << m_fen << "\n" << movelist << "\n";
      if (options.fourplayerchess)
         output_PGN4(m_fen, movelist);
   }

   // if (options.debug)
   // {
      // cout << "Game result: " << result << ", " << white_engine->get_game_result() << ", " << black_engine->get_game_result() << "\n";
      // cout << "engine evals: white = " << white_engine->m_score << ", black = " << black_engine->m_score << "\n";
   // }

   return result;
}

bool GameManager::is_engine_unresponsive(void)
{
   if (m_thread_running)
   {
      chrono::milliseconds elapsed_time_ms;
      elapsed_time_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - m_timestamp);
      chrono::milliseconds clock_ms = (m_turn == WHITE) ? m_white_clock_ms : m_black_clock_ms;

      if ((elapsed_time_ms > 5s) && (!m_engine1.m_is_ready || !m_engine2.m_is_ready))
      {
         if (!m_engine1.m_is_ready)
            cout << "Error: " << m_engine1.m_name << " is not ready after 5 seconds.\n";
         else
            cout << "Error: " << m_engine2.m_name << " is not ready after 5 seconds.\n";
         return true;
      }

      if ((clock_ms - elapsed_time_ms) < -10s)
      {
         if (((m_turn == WHITE) && !m_swap_sides) || ((m_turn == BLACK) && m_swap_sides))
            cout << "Error: " << m_engine1.m_name << " is not moving (clock < -10s).\n";
         else
            cout << "Error: " << m_engine2.m_name << " is not moving (clock < -10s).\n";
         return true;
      }
   }
   return false;
}

game_result GameManager::determine_game_result(Engine *white_engine, Engine *black_engine)
{
   game_result white_result, black_result, result;

   white_engine->update_game_result();
   black_engine->update_game_result();
   white_result = white_engine->get_game_result();
   black_result = black_engine->get_game_result();

   if ((white_result == ERROR_ILLEGAL_MOVE) || (black_result == ERROR_ILLEGAL_MOVE))
   {
      m_error = true;
      result = ERROR_ILLEGAL_MOVE;
   }
   else if ((white_result == ERROR_INVALID_POSITION) || (black_result == ERROR_INVALID_POSITION))
   {
      m_error = true;
      result = ERROR_INVALID_POSITION;
   }
   else if (white_engine->got_decisive_result() && black_engine->got_decisive_result() && (white_result != black_result))
   {
      cout << "Error: engines disagree on game result. " << white_result << ", " << black_result << "\n";
      m_error = true;
      result = UNDETERMINED;
   }
   else if (white_engine->got_decisive_result())
   {
      result = white_result;
   }
   else if (black_engine->got_decisive_result())
   {
      result = black_result;
   }
   else if (white_result == NO_LEGAL_MOVES)
   {
      if (black_engine->is_checkmating() || white_engine->is_getting_checkmated())
         result = BLACK_WIN;
      else
         result = DRAW;
   }
   else if (black_result == NO_LEGAL_MOVES)
   {
      if (white_engine->is_checkmating() || black_engine->is_getting_checkmated())
         result = WHITE_WIN;
      else
         result = DRAW;
   }
   else if (m_num_moves >= options.max_moves)
   {
      cout << "Draw due to maximum number of moves reached\n";
      result = DRAW;
   }
   else if ((white_result == UNFINISHED) && (black_result == UNFINISHED))
   {
      m_error = true;
      result = UNFINISHED;
   }
   else
   {
      m_error = true;
      result = UNDETERMINED;
   }

   return result;
}

player_color get_color_to_move_from_fen(string fen)
{
   if (fen.empty())
      return WHITE;

   // Chess.com 4 player chess (FEN4), RY vs BG teams:
   if (fen.rfind("R-", 0) == 0)
      return WHITE;
   if (fen.rfind("B-", 0) == 0)
      return BLACK;
   if (fen.rfind("Y-", 0) == 0)
      return WHITE;
   if (fen.rfind("G-", 0) == 0)
      return BLACK;

   // Normal FEN:
   if (fen.find(" w ") != string::npos)
      return WHITE;
   if (fen.find(" b ") != string::npos)
      return BLACK;

   cout << "Warning: couldn't get color to move from FEN: " << fen << "\n";
   return WHITE;
}

// Example: convert "h2h3" to "h2-h3"
void convert_move_to_dash_format(string &move)
{
   int i = 0;
   if (move.find("-") != string::npos)
      return;
   while (isalpha(move[i]))
      i++;
   while (isdigit(move[i]))
      i++;
   if ((i == 2) || (i == 3))
      move.insert(i, "-");
}

void output_PGN4(const string &fen, const string &movelist)
{
   vector<string> moves = get_tokens(movelist);
   cout << "\n[StartFen4 \"" << fen << "\"]\n";
   for (int i = 0; i < moves.size(); i++)
   {
      convert_move_to_dash_format(moves[i]);
      if ((i % 4) == 0)
         cout << ((i / 4) + 1) << ". " << moves[i] << " .. ";
      else if ((i % 4) == 3)
         cout << moves[i] << "\n";
      else
         cout << moves[i] << " .. ";
   }
   cout << "\n\n";
}
