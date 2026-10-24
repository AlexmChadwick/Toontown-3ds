CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror

ROOT = ../..
INCLUDES = -I$(ROOT)/source

TARGET = battle_cam_test
SRCS = $(ROOT)/source/world/battle_camera.cpp battle_cam_test.cpp

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
