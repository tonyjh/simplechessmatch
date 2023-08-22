# simplechessmatch
command line utility for chess engine matches

Under development

To compile, Boost library must be installed.

Can be compiled for Windows with MS Visual Studio (C++)

Can be compiled for Linux:

g++ -O3 engine.cpp gamemanager.cpp simplechessmatch.cpp -lboost_filesystem -lboost_program_options -o scm

For help, use command line:

scm --help
