# simplechessmatch
Command line tool for running chess engine matches

## Notes

UCI and xboard protocols are supported.

At least for now, simplechessmatch itself does not know the rules of chess or chess variants. It trusts the engines know the rules.
Therefore, simplechessmatch does not do move legality checking.

***Not all engines will work!*** UCI engines that don't send mate scores usually won't work well with this tool, because the tool
will have trouble telling apart checkmate vs stalemate, when both engines behave this way.

Draw adjudication (threefold repetition, 50-move rule, insufficient material) isn't handled perfectly by this tool,
since it doesn't know the rules of chess. This tool was mainly created for 4-player teams chess, where draws aren't common.

## Compiling

To compile, Boost library must be installed.

**Windows:** Compiling with MS Visual Studio (C++) has been tested and is working.

**Linux:** Compiling with g++ has been tested and is working.

g++ -O3 engine.cpp gamemanager.cpp simplechessmatch.cpp -lboost_filesystem -lboost_program_options -o scm

## Command line options
```
  --help                 print help message
  --e1 arg               first engine's file name
  --e2 arg               second engine's file name
  --x1                   first engine uses xboard protocol. (UCI is the default
                         protocol.)
  --x2                   second engine uses xboard protocol. (UCI is the
                         default protocol.)
  --cores1 arg (=1)      first engine number of cores
  --cores2 arg (=1)      second engine number of cores
  --mem1 arg (=128)      first engine memory usage (MB)
  --mem2 arg (=128)      second engine memory usage (MB)
  --custom1 arg          first engine custom command. e.g. --custom1 "setoption
                         name Style value Risky"
  --custom2 arg          second engine custom command. Note: --custom1 and
                         --custom2 can be used more than once in the command
                         line.
  --debug1               enable debug for first engine
  --debug2               enable debug for second engine
  --tc arg (=10000)      time control base time (ms)
  --inc arg (=100)       time control increment (ms)
  --fixed arg (=0)       time control fixed time per move (ms). This must be
                         set to 0, unless engines should simply use a fixed
                         amount of time per move.
  --margin arg (=50)     An engine loses on time if its clock goes below zero
                         for this amount of time (ms).
  --games arg (=1000000) total number of games to play
  --threads arg (=1)     number of concurrent games to run
  --maxmoves arg (=1000) maximum number of moves per game (total) before
                         adjudicating draw
  --fens arg             file containing FENs for opening positions (one FEN
                         per line)
  --variant arg          variant name
  --4pc                  enable 4 player chess (teams) mode
  --continue             continue match if error occurs (e.g. illegal move)
  --pmoves               print out all moves
  --pgn arg              save games in PGN format to specified file name
                         (if file exists it will be overwritten)
  --pgn4 arg             save games in PGN4 format to specified file name
                         (if file exists it will be overwritten)
```
