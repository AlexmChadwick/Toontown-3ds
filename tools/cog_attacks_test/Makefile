CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror

ROOT = ../..
INCLUDES = -I$(ROOT)/source

TARGET = cog_attacks_test
SRCS = cog_attacks_test.cpp

all: $(TARGET)
	./$(TARGET) $(ROOT)/tools/cog_attacks_test/reference_vectors.json

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
