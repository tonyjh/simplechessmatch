CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall

OS_NAME := $(shell uname -s)
LDFLAGS :=

ifeq ($(OS_NAME),Linux)
   LDFLAGS = -static -lboost_filesystem -lboost_program_options
endif

ifeq ($(OS_NAME),Darwin)
   CXX = clang++
   LDFLAGS = -lboost_filesystem -lboost_program_options
endif

ifeq ($(findstring MINGW,$(OS_NAME)),MINGW)
   LDFLAGS = -static -lboost_filesystem-mt -lboost_program_options-mt -lws2_32
endif

ifeq ($(findstring MSYS,$(OS_NAME)),MSYS)
   LDFLAGS = -static -lboost_filesystem-mt -lboost_program_options-mt -lws2_32
endif

TARGET = scm
SRCS = engine.cpp gamemanager.cpp simplechessmatch.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
