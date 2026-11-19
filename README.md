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

**Windows:** Windows binary can be built with MS Visual Studio (C++) or with MSYS2 / g++.

**Linux:** Linux binary can be built with g++.

## Command line options
```
  --help                                print help message
  --e1 arg                              first engine's file name
  --e2 arg                              second engine's file name
  --x1                                  first engine uses xboard protocol. (UCI
                                        is the default protocol.) Add
                                        "setboard" to always use setboard
                                        instead of edit for opening positions.
  --x2                                  second engine uses xboard protocol.
                                        (UCI is the default protocol.) Add
                                        "setboard" to always use setboard
                                        instead of edit for opening positions.
  --autosave arg (=0)                   number of game pairs interval for match
                                        progress auto-saving. 0 disables
                                        auto-saving.
  --autosave-file arg (=matchprogress.json)
                                        file name for match progress
                                        auto-saving (default:
                                        matchprogress.json)
  --resume [=arg(=matchprogress.json)]  resume an interrupted match from the
                                        saved .json file (default:
                                        matchprogress.json)
  --cores1 arg (=1)                     first engine number of cores
  --cores2 arg (=1)                     second engine number of cores
  --mem1 arg (=128)                     first engine memory usage (MB)
  --mem2 arg (=128)                     second engine memory usage (MB)
  --custom1 arg                         first engine custom command. e.g.
                                        --custom1 "setoption name Style value
                                        Risky"
  --custom2 arg                         second engine custom command. Note:
                                        --custom1 and --custom2 can be used
                                        more than once in the command line.
  --debug1                              enable debug for first engine
  --debug2                              enable debug for second engine
  --matescore1 arg (=100000)            first engine mate score threshold: eval
                                        scores above this (or below its
                                        negative) are treated as mate scores
  --matescore2 arg (=100000)            second engine mate score threshold
  --tc arg (=10000)                     time control base time (ms)
  --inc arg (=100)                      time control increment (ms)
  --fixed arg (=0)                      time control fixed time per move (ms).
                                        This must be set to 0, unless engines
                                        should simply use a fixed amount of
                                        time per move.
  --staticclocks arg (=0)               For debugging only: static clocks time
                                        per move (ms). Engines receive the same
                                        wtime/btime (rtime/btime/ytime/gtime
                                        for 4PC, time/otim for xboard) on every
                                        move.
  --depth arg (=0)                      fixed search depth per move for
                                        depth-limited matches. Cannot be
                                        combined with --tc, --inc,
                                        --staticclocks, --timeodds1 or
                                        --timeodds2. Allowed time per move is
                                        capped at 600000 ms (10 minutes) unless
                                        --fixed is given.
  --margin arg (=50)                    An engine loses on time if its clock
                                        goes below zero for this amount of time
                                        (ms).
  --timeodds1 arg (=1.0)                first engine time odds: e.g. set to 2.0
                                        to give 1st engine 2x time (affects
                                        base / increment / fixed / staticclocks
                                        time control values)
  --timeodds2 arg (=1.0)                second engine time odds
  --games arg (=1000000)                total number of games to play
  --threads arg (=1)                    number of concurrent games to run
  --maxseconds arg (=0)                 stop the match after this many seconds
                                        (0 = no limit)
  --stopfile arg (=scm_stop)            stop the match when a file with this
                                        name appears (default: scm_stop)
  --maxmoves arg (=1000)                maximum number of moves per game
                                        (total) before adjudicating draw
                                        regardless of scores
  --earlywin                            adjudicate win result early if both
                                        engines report mate scores
  --earlydraw                           adjudicate draw result early if both
                                        engine scores are in range (-drawscore
                                        <= score <= drawscore) for a total of
                                        drawmoves moves
  --drawscore arg (=25)                 drawscore (centipawns) value for
                                        "earlydraw" setting
  --drawmoves arg (=20)                 drawmoves value for "earlydraw" setting
  --fens arg                            file containing FENs for opening
                                        positions (one FEN per line)
  --fenoffset arg (=0)                  skip the first N positions in the FENs
                                        file.
  --variant arg                         variant name
  --4pc                                 enable 4 player chess (teams) mode
  --legacy-clocks                       use legacy 2-clock system instead of
                                        independent 4-player clocks
  --continue                            continue match if error occurs (e.g.
                                        illegal move)
  --pmoves                              print out all moves
  --pgn arg                             save games in PGN format to specified
                                        file name
                                        (if file exists it will be overwritten)
  --pgn4 arg                            save games in PGN4 format to specified
                                        file name
                                        (if file exists it will be overwritten)
  --pgnclocks                           write clock comments ({[%clk
                                        H:MM:SS.d]}) into PGN and PGN4 output
  --compact                             compact single-line status output with
                                        detailed match statistics
  --sprt                                Enable SPRT test. Test stops when
                                        bounds are reached.
  --sprt-elo-model arg (=normalized)    SPRT Elo model ('normalized' or
                                        'logistic')
  --sprt-elo0 arg (=0.0)                SPRT H0 (null hypothesis) Elo.
  --sprt-elo1 arg (=5.0)                SPRT H1 (alternative hypothesis) Elo.
  --sprt-alpha arg (=0.05)              SPRT alpha (type I error).
  --sprt-beta arg (=0.05)               SPRT beta (type II error).
```

## Resuming an interrupted match

Run a match with `--autosave <N>` to periodically save progress to `matchprogress.json`,
or the file name specified with `--autosave-file <filename>`.
The save happens once every N game pairs.
Incomplete game pairs that were in progress (before interruption/resume) are discarded and will be replayed.
If PGN/PGN4 saving is enabled, the PGN may contain duplicate games from replayed pairs.
If the process is terminated abruptly, games completed after the last autosave/checkpoint may also be missing from the PGN.

If the match is interrupted (manually or due to power loss, etc.), resume it with:

```
scm --resume
or
scm --resume <filename>
```

The saved file also restores the stop settings described below, so a time-limited match
continues on its remaining time budget.

## Stopping a match automatically

Use `--maxseconds <seconds>` to end the match after a time limit, or `--stopfile <filename>`
(default `scm_stop`) to end it when that file appears. Both stop gracefully: no new games are
started, the match is shut down normally, and autosave, PGN and final statistics are still
written. Games in progress at that moment are abandoned (just like a keypress stop), so the
reported game counts only include completed games.

Both settings are stored in the match progress file and restored by `--resume`, so an
interrupted time-limited match keeps its limit. `--maxseconds` counts total match time,
including the time already recorded in the progress file; a match whose budget was already
used up stops again as soon as it is resumed. `scm` never deletes the stop file, so a leftover
one would stop a resumed match immediately; delete it before resuming if you want the match to
continue.

