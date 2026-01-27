# Compiler selection: use 'make COMPILER=gcc' or 'make COMPILER=clang' (default: clang)
COMPILER ?= gcc

ifeq ($(COMPILER), gcc)
	CXX = g++
	CC = gcc
else ifeq ($(COMPILER), clang)
	CXX = clang++
	CC = clang
else
	$(error Unsupported compiler: $(COMPILER). Use 'gcc' or 'clang')
endif

all: produce_executable execute_executable

# Build the C++ version (main target)
produce_executable:
	$(CXX) -std=c++23 -o "sdl_draw" -g -Wall -Wextra "sdl_draw.cpp" $$(sdl2-config --cflags --libs)

execute_executable:
	./sdl_draw

# Build the C version (reference)
c_version:
	$(CC) -o "sdl_draw_c" -g -Wall "sdl_draw.c" $$(sdl2-config --cflags --libs) -lm
