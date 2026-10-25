CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror

ROOT = ../..
INCLUDES = -I$(ROOT)/source

TARGET = name_test
SRCS = $(ROOT)/source/game/name_tables.cpp $(ROOT)/source/game/name_check.cpp name_test.cpp

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
