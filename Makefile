all: produce_executable execute_executable

produce_executable:
	gcc -o "sdl_draw" -g -Wall "sdl_draw.c" $$(sdl2-config --cflags --libs) -lm

execute_executable:
	./sdl_draw
