--[[
    Mandelbrot Set Explorer - SDL2 C++ Port (LuaJIT FFI)
    Modern OOP rewrite matching sdl_draw.cpp architecture

    Desktop Controls:
      Mouse Wheel:       Smooth zoom in/out (1.15x factor)
      Left Click:        Zoom in 1.5x centered on cursor
      Right Click:       Zoom out 1.5x centered on cursor
      Left/Right Drag:   Pan the view (drag to move around)
      Middle Drag:       Pan the view (alternative)
      Left Hold (still): Continuous zoom IN toward cursor position
      Right Hold (still):Continuous zoom OUT from cursor position
      ESC:               Exit application

    Touch Controls (Android/Mobile):
      Single Finger Drag: Pan the view
      Two Finger Pinch:   Zoom in/out
      Single Tap:         Zoom in 1.5x centered on tap
      Double Tap:         Reset view to default
      Finger Hold:        Continuous zoom in toward finger (no movement)
]]

local ffi = require("ffi")
local bit = require("bit")

---@diagnostic disable: undefined-field

-- ============================================================================
-- Local Caching for Performance
-- ============================================================================

local floor = math.floor
local sqrt = math.sqrt
local abs = math.abs
local max = math.max
local min = math.min
local band = bit.band
local bor = bit.bor
local lshift = bit.lshift

-- ============================================================================
-- Configuration Constants
-- ============================================================================

local Config = {
    INITIAL_WIDTH = 800,
    INITIAL_HEIGHT = 600,
    MIN_ZOOM = 0.1,
    MAX_ZOOM = 1e14,
    DOUBLE_TAP_TIME = 800,
    DOUBLE_TAP_DIST = 200,
    TAP_DEBOUNCE_TIME = 500,
    HOLD_ZOOM_DELAY = 150,
    HOLD_ZOOM_RATE = 1.16,
    WHEEL_ZOOM_FACTOR = 1.15,
    CLICK_ZOOM_FACTOR = 1.5,
    DEFAULT_MAX_ITER = 256,
}

-- ============================================================================
-- SDL2 FFI Definitions
-- ============================================================================

ffi.cdef [[
    // Opaque types
    typedef struct SDL_Window SDL_Window;
    typedef struct SDL_Renderer SDL_Renderer;
    typedef struct SDL_Texture SDL_Texture;

    // 64-bit types for touch
    typedef int64_t SDL_TouchID;
    typedef int64_t SDL_FingerID;

    // Keyboard event
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

    // Mouse events
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

    typedef struct SDL_MouseMotionEvent {
        uint32_t type;
        uint32_t timestamp;
        uint32_t windowID;
        uint32_t which;
        uint32_t state;
        int32_t x;
        int32_t y;
        int32_t xrel;
        int32_t yrel;
    } SDL_MouseMotionEvent;

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

    // Touch events
    typedef struct SDL_TouchFingerEvent {
        uint32_t type;
        uint32_t timestamp;
        SDL_TouchID touchId;
        SDL_FingerID fingerId;
        float x;
        float y;
        float dx;
        float dy;
        float pressure;
        uint32_t windowID;
    } SDL_TouchFingerEvent;

    // Window events
    typedef struct SDL_WindowEvent {
        uint32_t type;
        uint32_t timestamp;
        uint32_t windowID;
        uint8_t event;
        uint8_t padding1;
        uint8_t padding2;
        uint8_t padding3;
        int32_t data1;
        int32_t data2;
    } SDL_WindowEvent;

    typedef struct SDL_QuitEvent {
        uint32_t type;
        uint32_t timestamp;
    } SDL_QuitEvent;

    // Event union
    typedef union SDL_Event {
        uint32_t type;
        SDL_KeyboardEvent key;
        SDL_MouseButtonEvent button;
        SDL_MouseMotionEvent motion;
        SDL_MouseWheelEvent wheel;
        SDL_TouchFingerEvent tfinger;
        SDL_WindowEvent window;
        SDL_QuitEvent quit;
        uint8_t padding[128];
    } SDL_Event;

    typedef struct SDL_Rect {
        int x, y, w, h;
    } SDL_Rect;

    // SDL functions
    int SDL_Init(uint32_t flags);
    void SDL_Quit(void);
    const char* SDL_GetError(void);
    uint32_t SDL_GetTicks(void);
    int SDL_SetHint(const char* name, const char* value);

    SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags);
    void SDL_DestroyWindow(SDL_Window* window);
    void SDL_GetWindowSize(SDL_Window* window, int* w, int* h);

    SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, int index, uint32_t flags);
    void SDL_DestroyRenderer(SDL_Renderer* renderer);
    int SDL_RenderClear(SDL_Renderer* renderer);
    int SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture,
                       const SDL_Rect* srcrect, const SDL_Rect* dstrect);
    void SDL_RenderPresent(SDL_Renderer* renderer);

    SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, uint32_t format,
                                   int access, int w, int h);
    void SDL_DestroyTexture(SDL_Texture* texture);
    int SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect,
                          const void* pixels, int pitch);

    int SDL_PollEvent(SDL_Event* event);
    uint32_t SDL_GetMouseState(int* x, int* y);
    void SDL_Delay(uint32_t ms);
]]

-- SDL constants
local SDL_INIT_VIDEO = 0x00000020
local SDL_WINDOW_SHOWN = 0x00000004
local SDL_WINDOW_RESIZABLE = 0x00000020
local SDL_WINDOWPOS_CENTERED = 0x2FFF0000
local SDL_RENDERER_ACCELERATED = 0x00000002
local SDL_TEXTUREACCESS_STREAMING = 1
local SDL_PIXELFORMAT_ARGB8888 = 0x16362004

-- Event types
local SDL_QUIT = 0x100
local SDL_WINDOWEVENT = 0x200
local SDL_KEYDOWN = 0x300
local SDL_MOUSEMOTION = 0x400
local SDL_MOUSEBUTTONDOWN = 0x401
local SDL_MOUSEBUTTONUP = 0x402
local SDL_MOUSEWHEEL = 0x403
local SDL_FINGERDOWN = 0x700
local SDL_FINGERUP = 0x701
local SDL_FINGERMOTION = 0x702

-- Window event subtypes
local SDL_WINDOWEVENT_RESIZED = 5
local SDL_WINDOWEVENT_SIZE_CHANGED = 6

-- Keys and buttons
local SDLK_ESCAPE = 27
local SDL_BUTTON_LEFT = 1
local SDL_BUTTON_MIDDLE = 2
local SDL_BUTTON_RIGHT = 3

-- Load SDL2 library
local SDL = ffi.load("SDL2")

-- Cache SDL functions
local SDL_GetTicks = SDL.SDL_GetTicks
local SDL_PollEvent = SDL.SDL_PollEvent
local SDL_GetMouseState = SDL.SDL_GetMouseState

-- ============================================================================
-- State Structures
-- ============================================================================

-- ViewState: viewport and zoom state
local ViewState = {}
ViewState.__index = ViewState

function ViewState.new()
    return setmetatable({
        zoom = 1.0,
        centerX = -0.5,
        centerY = 0.0,
        maxIter = Config.DEFAULT_MAX_ITER,
    }, ViewState)
end

function ViewState:getScale(renderWidth)
    return 4.0 / (renderWidth * self.zoom)
end

function ViewState:reset()
    self.zoom = 1.0
    self.centerX = -0.5
    self.centerY = 0.0
end

function ViewState:clampZoom()
    self.zoom = max(Config.MIN_ZOOM, min(Config.MAX_ZOOM, self.zoom))
end

function ViewState:screenToWorld(px, py, w, h)
    local scale = self:getScale(w)
    local wx = self.centerX + (px - w / 2.0) * scale
    local wy = self.centerY + (py - h / 2.0) * scale
    return wx, wy
end

function ViewState:zoomTowardPoint(worldX, worldY, screenX, screenY, w, h, factor)
    self.zoom = self.zoom * factor
    self:clampZoom()
    local newScale = self:getScale(w)
    self.centerX = worldX - (screenX - w / 2.0) * newScale
    self.centerY = worldY - (screenY - h / 2.0) * newScale
end

-- MouseState: mouse interaction tracking
local MouseState = {}
MouseState.__index = MouseState

function MouseState.new()
    return setmetatable({
        buttonHeld = 0,
        holdX = 0,
        holdY = 0,
        holdStartTime = 0,
        holdZoomActive = false,
        isPanning = false,
        panLastX = 0,
        panLastY = 0,
    }, MouseState)
end

function MouseState:reset()
    self.buttonHeld = 0
    self.holdZoomActive = false
    self.isPanning = false
end

-- TouchState: multi-touch gesture tracking
local TouchState = {}
TouchState.__index = TouchState

function TouchState.new()
    return setmetatable({
        numFingers = 0,
        finger1Id = 0LL,
        finger2Id = 0LL,
        finger1X = 0,
        finger1Y = 0,
        finger2X = 0,
        finger2Y = 0,
        isPanning = false,
        lastPanX = 0,
        lastPanY = 0,
        initialTapX = 0,
        initialTapY = 0,
        initialPinchDist = 0,
        initialZoom = 1.0,
        pinchCenterX = 0,
        pinchCenterY = 0,
        holdZoomActive = false,
        holdStartTime = 0,
        holdX = 0,
        holdY = 0,
        lastTapTime = 0,
        lastTapX = 0,
        lastTapY = 0,
        lastZoomTime = 0,
    }, TouchState)
end

function TouchState:reset()
    self.numFingers = 0
    self.finger1Id = 0LL
    self.finger2Id = 0LL
    self.isPanning = false
    self.holdZoomActive = false
end

function TouchState:getDistance(windowW, windowH)
    local dx = (self.finger2X - self.finger1X) * windowW
    local dy = (self.finger2Y - self.finger1Y) * windowH
    return sqrt(dx * dx + dy * dy)
end

-- DebugOverlay: visual tap marker
local DebugOverlay = {}
DebugOverlay.__index = DebugOverlay

function DebugOverlay.new()
    return setmetatable({
        tapX = -1,
        tapY = -1,
        tapTime = 0,
    }, DebugOverlay)
end

function DebugOverlay:setMarker(x, y)
    self.tapX = x
    self.tapY = y
    self.tapTime = SDL_GetTicks()
end

function DebugOverlay:draw(pixels, w, h)
    if self.tapX < 0 or self.tapY < 0 then return end

    local age = SDL_GetTicks() - self.tapTime
    if age > 2000 then return end

    local color = 0xFF00FF00 -- Green
    local size = 20

    for i = -size, size do
        local px = self.tapX + i
        local py = self.tapY
        if px >= 0 and px < w and py >= 0 and py < h then
            pixels[py * w + px] = color
        end
        px = self.tapX
        py = self.tapY + i
        if px >= 0 and px < w and py >= 0 and py < h then
            pixels[py * w + px] = color
        end
    end
end

-- ============================================================================
-- SDL Resources (RAII-style cleanup)
-- ============================================================================

local SDLResources = {}
SDLResources.__index = SDLResources

function SDLResources.create(width, height, title)
    if SDL.SDL_Init(SDL_INIT_VIDEO) < 0 then
        print("SDL_Init failed: " .. ffi.string(SDL.SDL_GetError()))
        return nil
    end

    SDL.SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS", "0")
    SDL.SDL_SetHint("SDL_MOUSE_TOUCH_EVENTS", "0")

    local self = setmetatable({}, SDLResources)

    self.window = SDL.SDL_CreateWindow(
        title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, bor(SDL_WINDOW_SHOWN, SDL_WINDOW_RESIZABLE))
    if self.window == nil then
        print("SDL_CreateWindow failed: " .. ffi.string(SDL.SDL_GetError()))
        SDL.SDL_Quit()
        return nil
    end

    local wPtr, hPtr = ffi.new("int[1]"), ffi.new("int[1]")
    SDL.SDL_GetWindowSize(self.window, wPtr, hPtr)
    self.width, self.height = wPtr[0], hPtr[0]
    self._wPtr, self._hPtr = wPtr, hPtr

    self.renderer = SDL.SDL_CreateRenderer(self.window, -1, SDL_RENDERER_ACCELERATED)
    if self.renderer == nil then
        print("SDL_CreateRenderer failed: " .. ffi.string(SDL.SDL_GetError()))
        SDL.SDL_DestroyWindow(self.window)
        SDL.SDL_Quit()
        return nil
    end

    self.texture = SDL.SDL_CreateTexture(self.renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, self.width, self.height)
    if self.texture == nil then
        print("SDL_CreateTexture failed: " .. ffi.string(SDL.SDL_GetError()))
        SDL.SDL_DestroyRenderer(self.renderer)
        SDL.SDL_DestroyWindow(self.window)
        SDL.SDL_Quit()
        return nil
    end

    self.pixels = ffi.new("uint32_t[?]", self.width * self.height)
    return self
end

function SDLResources:resize(newWidth, newHeight)
    if newWidth == self.width and newHeight == self.height then return true end
    SDL.SDL_DestroyTexture(self.texture)
    self.texture = SDL.SDL_CreateTexture(self.renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, newWidth, newHeight)
    if self.texture == nil then return false end
    self.pixels = ffi.new("uint32_t[?]", newWidth * newHeight)
    self.width, self.height = newWidth, newHeight
    print(string.format("RESIZE: %dx%d", self.width, self.height))
    return true
end

function SDLResources:updateTexture()
    SDL.SDL_UpdateTexture(self.texture, nil, self.pixels, self.width * 4)
end

function SDLResources:getWindowSize()
    SDL.SDL_GetWindowSize(self.window, self._wPtr, self._hPtr)
    return self._wPtr[0], self._hPtr[0]
end

function SDLResources:cleanup()
    if self.texture then SDL.SDL_DestroyTexture(self.texture) end
    if self.renderer then SDL.SDL_DestroyRenderer(self.renderer) end
    if self.window then SDL.SDL_DestroyWindow(self.window) end
    SDL.SDL_Quit()
end

-- ============================================================================
-- Main Application Class
-- ============================================================================

local MandelbrotApp = {}
MandelbrotApp.__index = MandelbrotApp

function MandelbrotApp.new()
    local self = setmetatable({}, MandelbrotApp)
    self.sdl = nil
    self.view = ViewState.new()
    self.mouse = MouseState.new()
    self.touch = TouchState.new()
    self.debug = DebugOverlay.new()
    self.windowWidth = Config.INITIAL_WIDTH
    self.windowHeight = Config.INITIAL_HEIGHT
    self.needsRedraw = true
    self.running = true
    self._event = ffi.new("SDL_Event")
    self._mouseXPtr = ffi.new("int[1]")
    self._mouseYPtr = ffi.new("int[1]")
    return self
end

function MandelbrotApp:init(width, height)
    width = width or Config.INITIAL_WIDTH
    height = height or Config.INITIAL_HEIGHT
    self.sdl = SDLResources.create(width, height, "Mandelbrot Explorer (SDL2 + LuaJIT)")
    if not self.sdl then return false end
    self.windowWidth, self.windowHeight = self.sdl:getWindowSize()
    return true
end

function MandelbrotApp:run()
    local event = self._event
    while self.running do
        while SDL_PollEvent(event) ~= 0 do
            self:handleEvent(event)
        end
        self:updateMouseHoldZoom()
        self:updateTouchHoldZoom()
        if self.needsRedraw then
            self:render()
            self.needsRedraw = false
        end
        SDL.SDL_RenderClear(self.sdl.renderer)
        SDL.SDL_RenderCopy(self.sdl.renderer, self.sdl.texture, nil, nil)
        SDL.SDL_RenderPresent(self.sdl.renderer)
        SDL.SDL_Delay(16)
    end
    self.sdl:cleanup()
end

-- ─── Rendering ───

function MandelbrotApp:render()
    local w, h = self.sdl.width, self.sdl.height
    local pixels = self.sdl.pixels
    local view = self.view
    local scale = view:getScale(w)
    local centerX, centerY = view.centerX, view.centerY
    local maxIter = view.maxIter
    local halfW, halfH = w / 2.0, h / 2.0

    print(string.format("RENDER: center=(%.6f, %.6f) zoom=%.2f size=%dx%d",
        centerX, centerY, view.zoom, w, h))

    for py = 0, h - 1 do
        local ci = centerY + (py - halfH) * scale
        local rowOffset = py * w
        for px = 0, w - 1 do
            local cr = centerX + (px - halfW) * scale
            local zr, zi, zr2, zi2, iter = 0.0, 0.0, 0.0, 0.0, 0
            while zr2 + zi2 <= 4.0 and iter < maxIter do
                zi = 2.0 * zr * zi + ci
                zr = zr2 - zi2 + cr
                zr2, zi2 = zr * zr, zi * zi
                iter = iter + 1
            end
            local color
            if iter == maxIter then
                color = 0xFF000000
            else
                local t = iter / maxIter
                local r = min(255, max(0, floor(9.0 * (1 - t) * t * t * t * 255)))
                local g = min(255, max(0, floor(15.0 * (1 - t) * (1 - t) * t * t * 255)))
                local b = min(255, max(0, floor(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255)))
                color = bor(0xFF000000, lshift(r, 16), lshift(g, 8), b)
            end
            pixels[rowOffset + px] = color
        end
    end
    self.debug:draw(pixels, w, h)
    self.sdl:updateTexture()
end

-- ─── Event Handling ───

function MandelbrotApp:handleEvent(event)
    local t = event.type
    if t == SDL_QUIT then
        self.running = false
    elseif t == SDL_KEYDOWN and event.key.keysym.sym == SDLK_ESCAPE then
        self.running = false
    elseif t == SDL_MOUSEBUTTONDOWN then
        self:handleMouseButtonDown(event.button)
    elseif t == SDL_MOUSEBUTTONUP then
        self:handleMouseButtonUp(event.button)
    elseif t == SDL_MOUSEMOTION then
        self:handleMouseMotion(event.motion)
    elseif t == SDL_MOUSEWHEEL then
        SDL_GetMouseState(self._mouseXPtr, self._mouseYPtr)
        self:handleMouseWheel(event.wheel, self._mouseXPtr[0], self._mouseYPtr[0])
    elseif t == SDL_FINGERDOWN then
        self:handleFingerDown(event.tfinger)
    elseif t == SDL_FINGERUP then
        self:handleFingerUp(event.tfinger)
    elseif t == SDL_FINGERMOTION then
        self:handleFingerMotion(event.tfinger)
    elseif t == SDL_WINDOWEVENT then
        local we = event.window.event
        if we == SDL_WINDOWEVENT_SIZE_CHANGED or we == SDL_WINDOWEVENT_RESIZED then
            self:handleWindowResize()
        end
    end
end

function MandelbrotApp:handleMouseButtonDown(e)
    if e.button == SDL_BUTTON_MIDDLE then
        self.mouse.isPanning = true
        self.mouse.panLastX = e.x
        self.mouse.panLastY = e.y
        self.mouse.buttonHeld = SDL_BUTTON_MIDDLE
    elseif e.button == SDL_BUTTON_LEFT or e.button == SDL_BUTTON_RIGHT then
        self.mouse.buttonHeld = e.button
        self.mouse.holdX = e.x
        self.mouse.holdY = e.y
        self.mouse.holdStartTime = SDL_GetTicks()
        self.mouse.holdZoomActive = false
        self.mouse.isPanning = false
    end
end

function MandelbrotApp:handleMouseButtonUp(e)
    local m = self.mouse
    if e.button ~= m.buttonHeld then return end

    if e.button == SDL_BUTTON_MIDDLE then
        m.isPanning = false
        m.buttonHeld = 0
        return
    end

    if m.isPanning then
        m:reset()
        return
    end

    if not m.holdZoomActive then
        local wx, wy = self.view:screenToWorld(e.x, e.y, self.sdl.width, self.sdl.height)
        local factor = (e.button == SDL_BUTTON_LEFT) and Config.CLICK_ZOOM_FACTOR
            or (1.0 / Config.CLICK_ZOOM_FACTOR)
        self.view:zoomTowardPoint(wx, wy, e.x, e.y, self.sdl.width, self.sdl.height, factor)
        self.needsRedraw = true
    end

    m.buttonHeld = 0
    m.holdZoomActive = false
end

function MandelbrotApp:handleMouseMotion(e)
    local m = self.mouse
    if m.buttonHeld == 0 then return end

    if not m.isPanning and (m.buttonHeld == SDL_BUTTON_LEFT or m.buttonHeld == SDL_BUTTON_RIGHT) then
        if abs(e.x - m.holdX) > 10 or abs(e.y - m.holdY) > 10 then
            m.isPanning = true
            m.holdZoomActive = false
            m.panLastX = e.x
            m.panLastY = e.y
        end
    end

    if m.isPanning then
        local scale = self.view:getScale(self.sdl.width)
        self.view.centerX = self.view.centerX - (e.x - m.panLastX) * scale
        self.view.centerY = self.view.centerY - (e.y - m.panLastY) * scale
        m.panLastX = e.x
        m.panLastY = e.y
        self.needsRedraw = true
    end
end

function MandelbrotApp:handleMouseWheel(e, mx, my)
    local wx, wy = self.view:screenToWorld(mx, my, self.sdl.width, self.sdl.height)
    local factor = (e.y > 0) and Config.WHEEL_ZOOM_FACTOR or (1.0 / Config.WHEEL_ZOOM_FACTOR)
    self.view:zoomTowardPoint(wx, wy, mx, my, self.sdl.width, self.sdl.height, factor)
    self.needsRedraw = true
end

function MandelbrotApp:handleFingerDown(e)
    local x, y = e.x, e.y
    if x > 1 or y > 1 then
        x = x / self.windowWidth
        y = y / self.windowHeight
    end

    local t = self.touch
    if t.numFingers == 0 then
        t.finger1Id = e.fingerId
        t.finger1X = x
        t.finger1Y = y
        t.numFingers = 1
        t.isPanning = false
        t.lastPanX = x
        t.lastPanY = y
        t.initialTapX = x
        t.initialTapY = y
        t.holdStartTime = SDL_GetTicks()
        t.holdX = x
        t.holdY = y
        t.holdZoomActive = false
    elseif t.numFingers == 1 then
        t.finger2Id = e.fingerId
        t.finger2X = x
        t.finger2Y = y
        t.numFingers = 2
        t.isPanning = false
        t.initialPinchDist = t:getDistance(self.windowWidth, self.windowHeight)
        t.initialZoom = self.view.zoom
        local cpx = (t.finger1X + t.finger2X) / 2 * self.sdl.width
        local cpy = (t.finger1Y + t.finger2Y) / 2 * self.sdl.height
        t.pinchCenterX, t.pinchCenterY = self.view:screenToWorld(cpx, cpy, self.sdl.width, self.sdl.height)
    end
end

function MandelbrotApp:handleFingerUp(e)
    local ex, ey = e.x, e.y
    if ex > 1 or ey > 1 then
        ex = ex / self.windowWidth
        ey = ey / self.windowHeight
    end

    local t = self.touch
    if t.numFingers == 1 and e.fingerId == t.finger1Id then
        local dx = abs(ex - t.initialTapX) * self.windowWidth
        local dy = abs(ey - t.initialTapY) * self.windowHeight
        if not t.isPanning and dx < 20 and dy < 20 then
            self:processTap(ex, ey)
        end
        t:reset()
    elseif t.numFingers == 2 then
        if e.fingerId == t.finger1Id then
            t.finger1Id = t.finger2Id
            t.finger1X = t.finger2X
            t.finger1Y = t.finger2Y
        end
        t.finger2Id = 0LL
        t.numFingers = 1
        t.isPanning = true
        t.lastPanX = t.finger1X
        t.lastPanY = t.finger1Y
        t.initialTapX = t.finger1X
        t.initialTapY = t.finger1Y
    elseif e.fingerId == t.finger1Id then
        t:reset()
    end
end

function MandelbrotApp:processTap(ex, ey)
    local now = SDL_GetTicks()
    local w, h = self.sdl.width, self.sdl.height
    local t = self.touch
    local tapX, tapY = ex * w, ey * h

    self.debug:setMarker(floor(tapX), floor(tapY))

    local dist = sqrt((tapX - t.lastTapX) ^ 2 + (tapY - t.lastTapY) ^ 2)
    if now - t.lastTapTime < Config.DOUBLE_TAP_TIME and dist < Config.DOUBLE_TAP_DIST then
        -- Double-tap: reset view
        self.view:reset()
        t.lastZoomTime = now
        self.needsRedraw = true
        t.lastTapTime = 0
        t.lastTapX = 0
        t.lastTapY = 0
    elseif now - t.lastZoomTime < Config.TAP_DEBOUNCE_TIME then
        -- Debounce
        t.lastTapX = tapX
        t.lastTapY = tapY
    else
        -- Single tap: zoom in
        local wx, wy = self.view:screenToWorld(tapX, tapY, w, h)
        if self.view.zoom * Config.CLICK_ZOOM_FACTOR <= Config.MAX_ZOOM then
            self.view:zoomTowardPoint(wx, wy, tapX, tapY, w, h, Config.CLICK_ZOOM_FACTOR)
            t.lastZoomTime = now
            self.needsRedraw = true
        end
        t.lastTapTime = now
        t.lastTapX = tapX
        t.lastTapY = tapY
    end
end

function MandelbrotApp:handleFingerMotion(e)
    local x, y = e.x, e.y
    if x > 1 or y > 1 then
        x = x / self.windowWidth
        y = y / self.windowHeight
    end

    local t = self.touch
    if t.numFingers == 1 and e.fingerId == t.finger1Id then
        local dist = sqrt((x - t.initialTapX) ^ 2 + (y - t.initialTapY) ^ 2) * self.windowWidth
        if dist > 5 then
            t.holdZoomActive = false
            t.holdStartTime = 0xFFFFFFFF
        end
        if dist > 15 and not t.isPanning then
            t.isPanning = true
            t.lastPanX = x
            t.lastPanY = y
        end
        if t.isPanning then
            local scale = self.view:getScale(self.sdl.width)
            self.view.centerX = self.view.centerX - (x - t.lastPanX) * self.sdl.width * scale
            self.view.centerY = self.view.centerY - (y - t.lastPanY) * self.sdl.height * scale
            t.lastPanX = x
            t.lastPanY = y
            t.finger1X = x
            t.finger1Y = y
            self.needsRedraw = true
        end
    elseif t.numFingers == 2 then
        if e.fingerId == t.finger1Id then
            t.finger1X = x
            t.finger1Y = y
        elseif e.fingerId == t.finger2Id then
            t.finger2X = x
            t.finger2Y = y
        end
        if t.initialPinchDist > 0 then
            local factor = t:getDistance(self.windowWidth, self.windowHeight) / t.initialPinchDist
            self.view.zoom = max(Config.MIN_ZOOM, min(Config.MAX_ZOOM, t.initialZoom * factor))
            local cpx = (t.finger1X + t.finger2X) / 2 * self.sdl.width
            local cpy = (t.finger1Y + t.finger2Y) / 2 * self.sdl.height
            local scale = self.view:getScale(self.sdl.width)
            self.view.centerX = t.pinchCenterX - (cpx - self.sdl.width / 2) * scale
            self.view.centerY = t.pinchCenterY - (cpy - self.sdl.height / 2) * scale
            self.needsRedraw = true
        end
    end
end

function MandelbrotApp:handleWindowResize()
    self.windowWidth, self.windowHeight = self.sdl:getWindowSize()
    if self.windowWidth ~= self.sdl.width or self.windowHeight ~= self.sdl.height then
        self.sdl:resize(self.windowWidth, self.windowHeight)
    end
    self.needsRedraw = true
end

-- ─── Continuous Zoom Updates ───

function MandelbrotApp:updateMouseHoldZoom()
    local m = self.mouse
    if m.buttonHeld == 0 or m.isPanning then return end

    local now = SDL_GetTicks()
    if not m.holdZoomActive then
        if now - m.holdStartTime >= Config.HOLD_ZOOM_DELAY then
            m.holdZoomActive = true
        else
            return
        end
    end

    SDL_GetMouseState(self._mouseXPtr, self._mouseYPtr)
    local mx, my = self._mouseXPtr[0], self._mouseYPtr[0]
    local wx, wy = self.view:screenToWorld(mx, my, self.sdl.width, self.sdl.height)
    local factor = (m.buttonHeld == SDL_BUTTON_LEFT) and Config.HOLD_ZOOM_RATE
        or (1.0 / Config.HOLD_ZOOM_RATE)
    local newZoom = self.view.zoom * factor
    if newZoom >= Config.MIN_ZOOM and newZoom <= Config.MAX_ZOOM then
        self.view:zoomTowardPoint(wx, wy, mx, my, self.sdl.width, self.sdl.height, factor)
        self.needsRedraw = true
    end
end

function MandelbrotApp:updateTouchHoldZoom()
    local t = self.touch
    if t.numFingers ~= 1 or t.isPanning then return end

    local now = SDL_GetTicks()
    if not t.holdZoomActive then
        if now - t.holdStartTime >= Config.HOLD_ZOOM_DELAY then
            t.holdZoomActive = true
        else
            return
        end
    end

    local px, py = t.holdX * self.sdl.width, t.holdY * self.sdl.height
    local wx, wy = self.view:screenToWorld(px, py, self.sdl.width, self.sdl.height)
    if self.view.zoom * Config.HOLD_ZOOM_RATE <= Config.MAX_ZOOM then
        self.view:zoomTowardPoint(wx, wy, px, py, self.sdl.width, self.sdl.height, Config.HOLD_ZOOM_RATE)
        self.needsRedraw = true
    end
end

-- ============================================================================
-- Main Entry Point
-- ============================================================================

local function main()
    local app = MandelbrotApp.new()
    if not app:init() then
        return 1
    end
    app:run()
    return 0
end

os.exit(main())
