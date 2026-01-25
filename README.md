# Mandelbrot Set Explorer (SDL2)

An interactive Mandelbrot fractal explorer using SDL2. Available in both C and LuaJIT versions.

## Features

- Real-time Mandelbrot set rendering with smooth color gradients
- Interactive zoom centered on mouse cursor
- Cross-platform using SDL2

## Controls

| Input | Action |
|-------|--------|
| Mouse Wheel | Smooth zoom in/out (1.15x factor) |
| Left Click | Zoom in 1.5x centered on cursor |
| Right Click | Zoom out 1.5x centered on cursor |
| ESC | Exit application |

## Files

- `sdl_draw.c` — C implementation
- `sdl_draw.lua` — LuaJIT FFI implementation
- `SDL2.dll` — SDL2 runtime (Windows)
- `Makefile` — Build script for C version

## Building (C version)

Requires SDL2 development libraries.

### MSYS2/MinGW (Windows)

```bash
# Install SDL2
pacman -S mingw-w64-x86_64-SDL2

# Build and run
make
```

### Linux

```bash
# Install SDL2 (Debian/Ubuntu)
sudo apt install libsdl2-dev

# Build and run
make
```

## Running (LuaJIT version)

Requires LuaJIT and SDL2.

```bash
luajit sdl_draw.lua
```

Or on Windows, double-click `sdl_draw.lua_start.cmd`.

## Distribution

For standalone distribution on Windows, include:
- `sdl_draw.exe` (C version) or `sdl_draw.lua` + `luajit.exe` (Lua version)
- `SDL2.dll`

## License

Public domain / MIT
