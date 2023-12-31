#include "engine.h"

namespace bp = boost::process;

extern struct options_info options;

// Engine constructor
Engine::Engine(void)
{
   m_uci = false;
   m_child_proc = nullptr;
   m_number = FIRST;
   m_color = BLACK;
   m_result = UNFINISHED;
   m_resigned = false;
   m_offered_draw = false;
   m_is_ready = false;
   m_quit_cmd_sent = false;
   m_xb_feature_ping = false;
   m_xb_feature_colors = false;
   m_xb_features_done = false;
   m_xb_feature_setboard = true;
   m_xb_feature_usermove = false;
   m_xb_force_mode = false;
   m_debug = false;
   m_score = 0;
   m_line.reserve(200);
}

// Engine destructor
Engine::~Engine(void)
{
   if (m_child_proc != nullptr)
   {
      if (m_child_proc->running())
         m_child_proc->terminate();
      delete m_child_proc;
   }
}

int Engine::load_engine(const string &eng_file_name, int ID, engine_number engine_num, bool uci)
{
   m_file_name = eng_file_name;
   m_name = (engine_num == FIRST) ? "Engine1 (" + m_file_name + ")" : "Engine2 (" + m_file_name + ")";

   if (m_child_proc != nullptr)
   {
      if (m_child_proc->running())
         m_child_proc->terminate();
      delete m_child_proc;
   }
   try
   {
      m_child_proc = new bp::child(eng_file_name, bp::std_out > m_out_stream, bp::std_in < m_in_stream);
   }
   catch (...)
   {
      return 0;
   }

   m_ID = ID;
   m_number = engine_num;
   m_uci = uci;

   m_debug = (m_number == FIRST) ? options.debug_1 : options.debug_2;

   if (m_uci)
      send_engine_cmd("uci");
   else
      send_engine_cmd("xboard");

   return 1;
}

int Engine::get_features(void)
{
   if (m_xb_features_done)
      return 1; // only need to get features once

   send_engine_cmd("protover 2");
   m_is_ready = false;

   while (1)
   {
      if (readline() == 0)
         return 0;
      if (m_line[0] == '#')
         continue;

      if (m_line.find("colors=1", 0) != string::npos)
         m_xb_feature_colors = true;
      if (m_line.find("ping=1", 0) != string::npos)
         m_xb_feature_ping = true;
      if (m_line.find("setboard=0", 0) != string::npos)
         m_xb_feature_setboard = false;
      if (m_line.find("san=1", 0) != string::npos)
         send_engine_cmd("rejected san");
      if (m_line.find("usermove=1", 0) != string::npos)
         m_xb_feature_usermove = true;

      // An xboard engine should respond to the "protover 2" command with "feature done=1", or an error message like "Error (unknown command): protover".
      if (m_line.find("done=1", 0) != string::npos)
      {
         m_xb_features_done = true;
         m_is_ready = true;
         return 1;
      }
      else if (m_line.find("protover", 0) != string::npos)
      {
         // if the engine doesn't support "protover", assume it doesn't support "setboard" either.
         m_xb_feature_setboard = false;
         m_xb_features_done = true;
         m_is_ready = true;
         return 1;
      }
   }
}

void Engine::send_engine_cmd(const string &cmd)
{
   if (is_running())
   {
      if (m_debug)
         cout << "TO ENGINE " << m_ID << ": " << cmd << "\n";
      m_in_stream << cmd << "\n";
      m_in_stream.flush();
   }
}

void Engine::send_quit_cmd(void)
{
   m_quit_cmd_sent = true;
   send_engine_cmd("quit");
}

int Engine::readline(void)
{
   // note: getline is blocking.
   getline(m_out_stream, m_line);
   if (m_out_stream.eof())
   {
      if (m_debug)
         cout << "ENGINE " << m_ID << " DISCONNECTED\n";
      return 0;
   }
   rstrip(m_line);
   lstrip(m_line);
   if (m_debug)
      cout << "FROM ENGINE " << m_ID << ": " << m_line << "\n";
   return 1;
}

int Engine::wait_for_ready(bool check_output)
{
   m_is_ready = false;
   if (m_uci)
      send_engine_cmd("isready");
   else if (m_xb_feature_ping)
      send_engine_cmd("ping 1");
   else
      send_engine_cmd("protover 2");

   while (1)
   {
      if (readline() == 0)
         return 0;
      if (m_uci)
      {
         if (m_line.rfind("readyok", 0) == 0)
         {
            m_is_ready = true;
            return 1;
         }
      }
      else if (m_xb_feature_ping)
      {
         if (m_line.rfind("pong 1", 0) == 0)
         {
            m_is_ready = true;
            return 1;
         }
      }
      else
      {
         if ((m_line.find("done=1", 0) != string::npos) || (m_line.find("protover", 0) != string::npos))
         {
            m_is_ready = true;
            return 1;
         }
      }
      if (check_output)
         check_engine_output();
   }
}

int Engine::engine_new_game_setup(player_color color, player_color turn, int64_t start_time_ms, int64_t inc_time_ms, int64_t fixed_time_ms, const string &fen, const string &variant)
{
   m_result = UNFINISHED;
   m_resigned = false;
   m_offered_draw = false;
   m_color = color;
   m_score = 0;
   m_opponent_move = "none";

   if (m_uci)
   {
      send_engine_cmd("ucinewgame");
      if (!wait_for_ready(false))
         return 0;

      if (!variant.empty())
         send_engine_cmd("setoption name UCI_Variant value " + variant);

      if (turn == color)
      {
         if (!fen.empty())
            send_engine_cmd("position fen " + fen);
         else
            send_engine_cmd("position startpos");
      }
   }
   else
   {
      if (!get_features())
         return 0;
      send_engine_cmd("new");
      if (!wait_for_ready(false))
         return 0;

      if (!variant.empty())
         send_engine_cmd("variant " + variant);

      if (!fen.empty())
      {
         if (m_xb_feature_setboard)
         {
            send_engine_cmd("force");
            m_xb_force_mode = true;
            send_engine_cmd("setboard " + fen);
         }
         else
            xb_edit_board(fen);
      }

      send_engine_cmd("easy");
      send_engine_cmd("post");
      if (fixed_time_ms)
      {
         if ((fixed_time_ms % 1000) == 0)
            send_engine_cmd("st " + to_string((fixed_time_ms) / 1000));
         else
            send_engine_cmd("st " + to_string((float)(fixed_time_ms) / 1000.0));
      }
      else
      {
         // send "level" command.
         char cmd[100];
         if ((start_time_ms % 60000) == 0)
         {
            if ((inc_time_ms % 1000) == 0)
               snprintf(cmd, 100, "level 0 %d %d", (int)start_time_ms / 60000, (int)inc_time_ms / 1000);
            else
               snprintf(cmd, 100, "level 0 %d %0.3f", (int)start_time_ms / 60000, (float)inc_time_ms / 1000.0);
         }
         else
         {
            if ((inc_time_ms % 1000) == 0)
               snprintf(cmd, 100, "level 0 %d:%02d %d", (int)start_time_ms / 60000, (int)(start_time_ms % 60000) / 1000, (int)inc_time_ms / 1000);
            else
               snprintf(cmd, 100, "level 0 %d:%02d %0.3f", (int)start_time_ms / 60000, (int)(start_time_ms % 60000) / 1000, (float)inc_time_ms / 1000.0);
         }

         send_engine_cmd(cmd);
         send_engine_cmd("time " + to_string(start_time_ms / 10));
         send_engine_cmd("otim " + to_string(start_time_ms / 10));
      }
      if (m_xb_feature_colors)
      {
         if ((m_color == BLACK) && (turn == WHITE))
            send_engine_cmd("white");
         if ((m_color == WHITE) && (turn == BLACK))
            send_engine_cmd("black");
      }
   }

   return 1;
}

void Engine::engine_new_game_start(int64_t start_time_ms, int64_t inc_time_ms, int64_t fixed_time_ms)
{
   if (m_uci)
   {
      if (fixed_time_ms != 0)
         send_engine_cmd("go movetime " + to_string(fixed_time_ms));
      else
         send_engine_cmd("go wtime " + to_string(start_time_ms) + " btime " + to_string(start_time_ms) + " winc " + to_string(inc_time_ms) + " binc " + to_string(inc_time_ms));
   }
   else
   {
      send_engine_cmd("go");
      m_xb_force_mode = false;
   }
}

int Engine::get_engine_move(void)
{
   m_move = "";

   while (1)
   {
      if (readline() == 0)
         return 0;
      if (m_uci)
      {
         if (m_line.rfind("bestmove", 0) == 0)
         {
            m_move = get_first_token(m_line, 9);

            // If the engine sent a "null" move, then the engine has no legal moves, and is mated or stalemated.
            // The engine should send "0000" in this case, but many UCI engines send one of the alternatives below.
            if ((m_move == "0000") || (m_move == "(none)") || (m_move == "none") || (m_move == "a1a1") || (m_move == ""))
            {
               m_result = NO_LEGAL_MOVES;
               m_move = "";
            }

            return 1;
         }
      }
      else
      {
         if (m_line.rfind("move ", 0) == 0)
         {
            m_move = m_line.substr(5);
            // Handle multi-part moves (required for duck chess variant).
            // If move ends with a comma, the 2nd part of the move will be on the next line.
            if (m_move[m_move.length() - 1] == ',')
            {
               if (readline() == 0)
                  return 0;
               if (m_line.rfind("move ", 0) == 0)
                  m_move.append(m_line.substr(5));
            }
            return 1;
         }
      }
      check_engine_output();
      if (m_result != UNFINISHED)
         return 1;
   }
}

void Engine::check_engine_output(void)
{
   vector<string> tokens;

   if (m_uci)
   {
      // Note: Most or many UCI engines don't report illegal moves/positions. They might ignore them or attempt to process them.

      if (m_line.rfind("info", 0) == 0)
      {
         // check for score. e.g. "info score cp 123" or "info score mate -3"
         tokens = get_tokens(m_line);
         for (size_t i = 1; i < tokens.size() - 2; i++)
            if (tokens[i] == "score")
            {
               if (tokens[i + 1] == "cp")
               {
                  m_score = atoi(tokens[i + 2].c_str());
                  if (m_score >= mate_score)
                     m_score = mate_score - 1;
                  if (m_score <= mate_score_neg)
                     m_score = mate_score_neg + 1;
               }
               else if (tokens[i + 1] == "mate")
               {
                  int n = atoi(tokens[i + 2].c_str());
                  m_score = (n <= 0) ? (mate_score_neg + n) : (mate_score + n);
               }
            }
      }

      if (m_line.rfind("info string", 0) == 0)
      {
         if (m_line.find("Invalid move", 0) != string::npos)
         {
            cout << "Illegal move reported by " << m_name << "\n";
            m_result = ERROR_ILLEGAL_MOVE;
         }
         else if (m_line.find("Invalid FEN", 0) != string::npos)
         {
            cout << "Invalid position reported by " << m_name << "\n";
            m_result = ERROR_INVALID_POSITION;
         }
         // 4pchess (https://github.com/obryanlouis/4pchess) uses "RY won" / "BG won" / "Stalemate".
         else if (m_line.find("White won") != string::npos)
            m_result = WHITE_WIN;
         else if (m_line.find("Black won") != string::npos)
            m_result = BLACK_WIN;
         else if (m_line.find("RY won") != string::npos)
            m_result = WHITE_WIN;
         else if (m_line.find("BG won") != string::npos)
            m_result = BLACK_WIN;
         else if (m_line.find("Stalemate") != string::npos)
            m_result = DRAW;
         else if (m_line.find("offer draw") != string::npos)
            m_offered_draw = true;
      }
   }
   else
   {
      if (m_line[0] == '#')
         return;
      if ((m_line.rfind("Illegal move:", 0) == 0) ||
          (m_line.rfind("Error (unknown command): " + m_opponent_move) == 0))
      {
         cout << "Illegal move reported by " << m_name << "\n";
         m_result = ERROR_ILLEGAL_MOVE;
      }
      else if (m_line.rfind("tellusererror Illegal position", 0) == 0)
      {
         cout << "Invalid position reported by " << m_name << "\n";
         m_result = ERROR_INVALID_POSITION;
      }
      else if (m_line.rfind("resign", 0) == 0)
      {
         m_result = (m_color == WHITE) ? BLACK_WIN : WHITE_WIN;
         m_resigned = true;
      }
      else if (m_line.rfind("1-0", 0) == 0)
         m_result = WHITE_WIN;
      else if (m_line.rfind("0-1", 0) == 0)
         m_result = BLACK_WIN;
      else if (m_line.rfind("1/2-1/2", 0) == 0)
         m_result = DRAW;
      else if (m_line.rfind("offer draw", 0) == 0)
         m_offered_draw = true;
      else if (isdigit(m_line[0]))
      {
         int ply, score, time, nodes;
         stringstream ss(m_line);
         if (ss >> ply >> score >> time >> nodes)
         {
            m_score = score;
            if (m_score > (mate_score + 999))
               m_score = (mate_score + 999);
            if (m_score < (mate_score_neg - 999))
               m_score = mate_score_neg - 999;
         }
      }
   }
}

void Engine::send_move_and_clocks_to_engine(const string &move, const string &startfen, const string &movelist, int64_t engine_clock_ms, int64_t opp_clock_ms, int64_t inc_ms, int64_t fixed_time_ms)
{
   m_opponent_move = move;
   if (m_uci)
   {
      if (startfen.empty())
         send_engine_cmd("position startpos moves " + movelist);
      else
         send_engine_cmd("position fen " + startfen + " moves " + movelist);

      if (fixed_time_ms == 0)
      {
         int64_t wtime, btime;
         wtime = (m_color == WHITE) ? engine_clock_ms : opp_clock_ms;
         btime = (m_color == WHITE) ? opp_clock_ms : engine_clock_ms;
         send_engine_cmd("go wtime " + to_string(wtime) + " btime " + to_string(btime) + " winc " + to_string(inc_ms) + " binc " + to_string(inc_ms));
      }
      else
         send_engine_cmd("go movetime " + to_string(fixed_time_ms));
   }
   else
   {
      if (fixed_time_ms == 0)
      {
         send_engine_cmd("time " + to_string(engine_clock_ms / 10));
         send_engine_cmd("otim " + to_string(opp_clock_ms / 10));
      }
      if (m_xb_feature_usermove)
         send_engine_cmd("usermove " + move);
      else
         send_engine_cmd(move);
      if (m_xb_force_mode)
      {
         send_engine_cmd("go");
         m_xb_force_mode = false;
      }
   }
}

void Engine::send_result_to_engine(game_result result)
{
   if (!m_uci)
   {
      if (result == WHITE_WIN)
         send_engine_cmd("result 1-0 {White won}");
      else if (result == BLACK_WIN)
         send_engine_cmd("result 0-1 {Black won}");
      else if (result == DRAW)
         send_engine_cmd("result 1/2-1/2 {Draw}");
      else
         send_engine_cmd("result *");
   }
}

bool Engine::is_running(void)
{
   if (m_child_proc != nullptr)
      if (m_child_proc->running())
         return true;
   return false;
}

// This function may need to be used if engine doesn't respond to "quit" command in a timely manner.
void Engine::force_exit(void)
{
   if (is_running())
   {
      m_child_proc->terminate();
      cout << m_name << " (" << m_ID << "): forced exit\n";
   }
}

// Note: The following functions: has_checkmate, is_checkmated, is_checkmating, and is_getting_checkmated
// all rely on the engine's eval score, and not all engines always provide the eval score all the time.

bool Engine::has_checkmate(void)
{
   // only true for immediate checkmate, not for mate-in-2 etc.
   return (m_score == (mate_score + 1));
}

bool Engine::is_checkmated(void)
{
   // only true for immediate checkmate, not for mate-in-2 etc.
   return (m_score == mate_score_neg);
}

bool Engine::is_checkmating(void)
{
   // true if engine is checkmating in any number of moves.
   return (m_score > mate_score);
}

bool Engine::is_getting_checkmated(void)
{
   // true if engine is getting checkmated in any number of moves.
   return (m_score <= mate_score_neg);
}

bool Engine::is_drawish(void)
{
   // true if engine's score is drawish (close to 0).
   return ((uint)ABS(m_score) <= options.draw_score);
}

bool Engine::got_decisive_result(void)
{
   return ((m_result == WHITE_WIN) || (m_result == BLACK_WIN) || (m_result == DRAW));
}

game_result Engine::get_game_result(void)
{
   return m_result;
}

void Engine::update_game_result(void)
{
   // Update engine's result based on engine's eval score, if possible.
   // Only update result if the engine didn't already report an explicit win/loss/draw result.
   if (m_result == UNFINISHED)
   {
      if (has_checkmate())
         m_result = (m_color == WHITE) ? WHITE_WIN : BLACK_WIN;
      else if (is_checkmated())
         m_result = (m_color == WHITE) ? BLACK_WIN : WHITE_WIN;
   }
   else if (m_result == NO_LEGAL_MOVES)
   {
      if (is_getting_checkmated())
         m_result = (m_color == WHITE) ? BLACK_WIN : WHITE_WIN;
      // else, leave m_result set to NO_LEGAL_MOVES. It could be either stalemate or checkmate.
   }
}

string Engine::get_eval(void)
{
   string s;
   if (m_score > mate_score)
      s = "mate in " + to_string(m_score - mate_score);
   else if (m_score <= mate_score_neg)
      s = "mated in " + to_string(mate_score_neg - m_score);
   else
      s = to_string(m_score);
   return s;
}

// This function is only for old xboard engines that support the "edit" command instead of the "setboard" command.
// This function will only work for normal chess.
void Engine::xb_edit_board(const string &fen)
{
   uint s = 0, fen_index = 0;
   uint empty;
   size_t len = fen.length();
   char file, rank;
   stringstream cmd;
   player_color color = WHITE;

   const string white_pieces = "PNBRQK";
   const string black_pieces = "pnbrqk";
   const char *fen_buf = fen.c_str();

   if (get_color_to_move_from_fen(fen) == BLACK)
   {
      send_engine_cmd("force");
      send_engine_cmd("a2a3");
      m_xb_force_mode = true;
   }

   send_engine_cmd("edit");
   send_engine_cmd("#");
   while (s < 64)
   {
      if (white_pieces.find(fen_buf[fen_index]) != string::npos)
      {
         file = 'a' + (s % 8);
         rank = '1' + 7 - (s / 8);
         if (color != WHITE)
            send_engine_cmd("c");
         color = WHITE;
         cmd.str("");
         cmd << fen_buf[fen_index] << file << rank;
         send_engine_cmd(cmd.str());
         s++;
      }
      else if (black_pieces.find(fen_buf[fen_index]) != string::npos)
      {
         file = 'a' + (s % 8);
         rank = '1' + 7 - (s / 8);
         if (color != BLACK)
            send_engine_cmd("c");
         color = BLACK;
         cmd.str("");
         cmd << (char)toupper(fen_buf[fen_index]) << file << rank;
         send_engine_cmd(cmd.str());
         s++;
      }
      else if ((fen_buf[fen_index] >= '1') && (fen_buf[fen_index] <= '8'))
      {
         empty = atoi(&fen_buf[fen_index]);
         if (empty > 8)
            break; // invalid FEN
         s += empty;
      }
      if (++fen_index >= len)
         break;
   }
   send_engine_cmd(".");
}

void rstrip(string &s)
{
   // if string is null terminated before the end of the string, truncate the string.
   size_t len = strlen(s.c_str());
   if (len < s.length())
      s.erase(len);
   // strip spaces/tabs/newlines/etc.
   size_t end = s.find_last_not_of(" \t\r\n");
   s.erase((end == string::npos) ? 0 : (end + 1));
}

void lstrip(string &s)
{
   // strip spaces/tabs/newlines/etc.
   s.erase(0, s.find_first_not_of(" \t\r\n"));
}

string get_first_token(const string &s, size_t pos)
{
   size_t start = s.find_first_not_of(" \t", pos);
   if (start == string::npos)
      return "";
   size_t end = s.find_first_of(" \t", start + 1);
   return s.substr(start, (end == string::npos) ? string::npos : (end - start));
}

vector<string> get_tokens(const string &s)
{
   vector<string> tokens;
   size_t start;
   size_t end = 0;

   while (1)
   {
      start = s.find_first_not_of(" \t", end);
      if (start == string::npos)
         break;
      end = s.find_first_of(" \t", start + 1);
      tokens.push_back(s.substr(start, (end == string::npos) ? string::npos : (end - start)));
      if (end == string::npos)
         break;
   }

   return tokens;
}

player_color get_color_to_move_from_fen(const string &fen)
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
