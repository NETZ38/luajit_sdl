--[[
    Mandelbrot Set Explorer - SDL2 Version (LuaJIT FFI)
    Converted from Windows GDI to SDL2

    Controls:
      Mouse Wheel: Smooth zoom in/out (1.15x factor)
      Left Click:  Zoom in 1.5x centered on cursor
      Right Click: Zoom out 1.5x centered on cursor
      ESC:         Exit application
]]

local ffi = require("ffi")
local bit = require("bit")

---@diagnostic disable: undefined-field

-- SDL2 FFI definitions
ffi.cdef [[
    typedef struct SDL_Window SDL_Window;
    typedef struct SDL_Renderer SDL_Renderer;
    typedef struct SDL_Texture SDL_Texture;

    typedef struct SDL_Keysym {
        int32_t scancode;
        int32_t sym;
        uint16_t mod;
        uint32_t unused;
    } SDL_Keysym;

    typedef struct SDL_KeyboardEvent {
        uint32_t type;
        uint32_t timestamp;
        uint32_t windowID;
        uint8_t state;
        uint8_t repeat_;
        uint8_t padding2;
        uint8_t padding3;
        SDL_Keysym keysym;
    } SDL_KeyboardEvent;

    typedef struct SDL_MouseButtonEvent {
        uint32_t type;
        uint32_t timestamp;
        uint32_t windowID;
        uint32_t which;
        uint8_t button;
        uint8_t state;
        uint8_t clicks;
        uint8_t padding1;
        int32_t x;
        int32_t y;
    } SDL_MouseButtonEvent;

    typedef struct SDL_MouseWheelEvent {
        uint32_t type;
        uint32_t timestamp;
        uint32_t windowID;
        uint32_t which;
        int32_t x;
        int32_t y;
        uint32_t direction;
        float preciseX;
        float preciseY;
        int32_t mouseX;
        int32_t mouseY;
    } SDL_MouseWheelEvent;

    typedef struct SDL_QuitEvent {
        uint32_t type;
        uint32_t timestamp;
    } SDL_QuitEvent;

    typedef union SDL_Event {
        uint32_t type;
        SDL_KeyboardEvent key;
        SDL_MouseButtonEvent button;
        SDL_MouseWheelEvent wheel;
        SDL_QuitEvent quit;
        uint8_t padding[128];
    } SDL_Event;

    typedef struct SDL_Rect {
        int x, y, w, h;
    } SDL_Rect;

    // SDL constants
    enum {
        SDL_INIT_VIDEO = 0x00000020,
        SDL_WINDOW_SHOWN = 0x00000004,
        SDL_WINDOWPOS_CENTERED = 0x2FFF0000,
        SDL_RENDERER_ACCELERATED = 0x00000002,
        SDL_TEXTUREACCESS_STREAMING = 1,
        SDL_PIXELFORMAT_ARGB8888 = 0x16362004,
        SDL_QUIT = 0x100,
        SDL_KEYDOWN = 0x300,
        SDL_MOUSEBUTTONDOWN = 0x401,
        SDL_MOUSEWHEEL = 0x403,
        SDLK_ESCAPE = 27,
        SDL_BUTTON_LEFT = 1,
        SDL_BUTTON_RIGHT = 3
    };

    // SDL functions
    int SDL_Init(uint32_t flags);
    void SDL_Quit(void);
    const char* SDL_GetError(void);

    SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags);
    void SDL_DestroyWindow(SDL_Window* window);

    SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, int index, uint32_t flags);
    void SDL_DestroyRenderer(SDL_Renderer* renderer);
    int SDL_RenderClear(SDL_Renderer* renderer);
    int SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect);
    void SDL_RenderPresent(SDL_Renderer* renderer);

    SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, uint32_t format, int access, int w, int h);
    void SDL_DestroyTexture(SDL_Texture* texture);
    int SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch);

    int SDL_PollEvent(SDL_Event* event);
    uint32_t SDL_GetMouseState(int* x, int* y);
    void SDL_Delay(uint32_t ms);
]]

-- Load SDL2 library
local SDL
if ffi.os == "Windows" then
    SDL = ffi.load("SDL2")
else
    SDL = ffi.load("SDL2")
end

-- Constants
local WIDTH = 800
local HEIGHT = 600

-- Mandelbrot parameters
local zoom = 1.0
local center_x = -0.5
local center_y = 0.0
local max_iter = 256

-- SDL globals
local window = nil
local renderer = nil
local texture = nil
local pixels = nil
local needsRedraw = true

-- Calculate escape iterations for a point in the complex plane
local function mandelbrot(cr, ci)
    local zr, zi = 0.0, 0.0
    local zr2, zi2 = 0.0, 0.0
    local iter = 0

    while zr2 + zi2 <= 4.0 and iter < max_iter do
        zi = 2.0 * zr * zi + ci
        zr = zr2 - zi2 + cr
        zr2 = zr * zr
        zi2 = zi * zi
        iter = iter + 1
    end
    return iter
end

-- Map iteration count to a color (ARGB format: 0xAARRGGBB)
local function getColor(iter)
    if iter == max_iter then
        return 0xFF000000 -- Black for points in the set
    end

    -- Normalize iteration count
    local t = iter / max_iter

    -- Create smooth color gradient using polynomial
    local r = math.floor(9.0 * (1 - t) * t * t * t * 255)
    local g = math.floor(15.0 * (1 - t) * (1 - t) * t * t * 255)
    local b = math.floor(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255)

    -- Clamp values
    if r > 255 then r = 255 end
    if g > 255 then g = 255 end
    if b > 255 then b = 255 end

    return bit.bor(0xFF000000, bit.lshift(r, 16), bit.lshift(g, 8), b)
end

-- Render the Mandelbrot set to the pixel buffer
local function renderMandelbrot()
    local scale = 4.0 / (WIDTH * zoom)

    for py = 0, HEIGHT - 1 do
        local ci = center_y + (py - HEIGHT / 2.0) * scale
        for px = 0, WIDTH - 1 do
            local cr = center_x + (px - WIDTH / 2.0) * scale
            local iter = mandelbrot(cr, ci)
            pixels[py * WIDTH + px] = getColor(iter)
        end
    end

    -- Update texture with new pixel data
    SDL.SDL_UpdateTexture(texture, nil, pixels, WIDTH * 4)
end

-- Handle mouse button events for zooming
local function handleMouseButton(event)
    local scale = 4.0 / (WIDTH * zoom)
    local mouseX = center_x + (event.x - WIDTH / 2.0) * scale
    local mouseY = center_y + (event.y - HEIGHT / 2.0) * scale

    if event.button == SDL.SDL_BUTTON_LEFT then
        -- Zoom in
        zoom = zoom * 1.5
        center_x = mouseX
        center_y = mouseY
        needsRedraw = true
    elseif event.button == SDL.SDL_BUTTON_RIGHT then
        -- Zoom out
        zoom = zoom / 1.5
        center_x = mouseX
        center_y = mouseY
        needsRedraw = true
    end
end

-- Handle mouse wheel events for smooth zooming
local function handleMouseWheel(event, mouseX, mouseY)
    local scale = 4.0 / (WIDTH * zoom)
    local worldX = center_x + (mouseX - WIDTH / 2.0) * scale
    local worldY = center_y + (mouseY - HEIGHT / 2.0) * scale

    if event.y > 0 then
        -- Scroll up - zoom in
        zoom = zoom * 1.15
    elseif event.y < 0 then
        -- Scroll down - zoom out
        zoom = zoom / 1.15
    end

    -- Recalculate to keep point under cursor fixed
    local newScale = 4.0 / (WIDTH * zoom)
    center_x = worldX - (mouseX - WIDTH / 2.0) * newScale
    center_y = worldY - (mouseY - HEIGHT / 2.0) * newScale

    needsRedraw = true
end

-- Main function
local function main()
    -- Initialize SDL
    if SDL.SDL_Init(SDL.SDL_INIT_VIDEO) < 0 then
        print("SDL_Init failed: " .. ffi.string(SDL.SDL_GetError()))
        return 1
    end

    -- Create window
    window = SDL.SDL_CreateWindow(
        "Mandelbrot Explorer (SDL2 + LuaJIT)",
        SDL.SDL_WINDOWPOS_CENTERED,
        SDL.SDL_WINDOWPOS_CENTERED,
        WIDTH,
        HEIGHT,
        SDL.SDL_WINDOW_SHOWN
    )
    if window == nil then
        print("SDL_CreateWindow failed: " .. ffi.string(SDL.SDL_GetError()))
        SDL.SDL_Quit()
        return 1
    end

    -- Create renderer
    renderer = SDL.SDL_CreateRenderer(window, -1, SDL.SDL_RENDERER_ACCELERATED)
    if renderer == nil then
        print("SDL_CreateRenderer failed: " .. ffi.string(SDL.SDL_GetError()))
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
        return 1
    end

    -- Create texture for pixel buffer (ARGB8888 format)
    texture = SDL.SDL_CreateTexture(
        renderer,
        SDL.SDL_PIXELFORMAT_ARGB8888,
        SDL.SDL_TEXTUREACCESS_STREAMING,
        WIDTH,
        HEIGHT
    )
    if texture == nil then
        print("SDL_CreateTexture failed: " .. ffi.string(SDL.SDL_GetError()))
        SDL.SDL_DestroyRenderer(renderer)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
        return 1
    end

    -- Allocate pixel buffer
    pixels = ffi.new("uint32_t[?]", WIDTH * HEIGHT)

    -- Main loop
    local running = true
    local event = ffi.new("SDL_Event")
    local mouseXPtr = ffi.new("int[1]")
    local mouseYPtr = ffi.new("int[1]")

    while running do
        -- Process events
        while SDL.SDL_PollEvent(event) ~= 0 do
            if event.type == SDL.SDL_QUIT then
                running = false
            elseif event.type == SDL.SDL_KEYDOWN then
                if event.key.keysym.sym == SDL.SDLK_ESCAPE then
                    running = false
                end
            elseif event.type == SDL.SDL_MOUSEBUTTONDOWN then
                handleMouseButton(event.button)
            elseif event.type == SDL.SDL_MOUSEWHEEL then
                SDL.SDL_GetMouseState(mouseXPtr, mouseYPtr)
                handleMouseWheel(event.wheel, mouseXPtr[0], mouseYPtr[0])
            end
        end

        -- Render if needed
        if needsRedraw then
            print(string.format("Rendering... (zoom: %.2f, center: %.6f, %.6f)", zoom, center_x, center_y))
            renderMandelbrot()
            needsRedraw = false
            print("Done!")
        end

        -- Present
        SDL.SDL_RenderClear(renderer)
        SDL.SDL_RenderCopy(renderer, texture, nil, nil)
        SDL.SDL_RenderPresent(renderer)

        -- Small delay to prevent CPU spinning
        SDL.SDL_Delay(16)
    end

    -- Cleanup
    SDL.SDL_DestroyTexture(texture)
    SDL.SDL_DestroyRenderer(renderer)
    SDL.SDL_DestroyWindow(window)
    SDL.SDL_Quit()

    return 0
end

-- Run the program
os.exit(main())
