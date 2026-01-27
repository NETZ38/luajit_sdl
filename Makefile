all: produce_executable execute_executable

# Build the C++ version (main target)
produce_executable:
	g++ -std=c++23 -o "sdl_draw" -g -Wall -Wextra "sdl_draw.cpp" $$(sdl2-config --cflags --libs)

execute_executable:
	./sdl_draw

# Build the C version (reference)
c_version:
	gcc -o "sdl_draw_c" -g -Wall "sdl_draw.c" $$(sdl2-config --cflags --libs) -lm
