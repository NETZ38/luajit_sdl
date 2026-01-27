/*
 * Mandelbrot Set Explorer - SDL2 C++ Version
 * Modern C++23 OOP conversion from sdl_draw.c
 *
 * Desktop Controls:
 *   Mouse Wheel:       Smooth zoom in/out (1.15x factor)
 *   Left Click:        Zoom in 1.5x centered on cursor
 *   Right Click:       Zoom out 1.5x centered on cursor
 *   Left/Right Drag:   Pan the view (drag to move around)
 *   Middle Drag:       Pan the view (alternative)
 *   Left Hold (still): Continuous zoom IN toward cursor position
 *   Right Hold (still):Continuous zoom OUT from cursor position
 *   ESC:               Exit application
 *
 * Touch Controls (Android/Mobile):
 *   Single Finger Drag: Pan the view
 *   Two Finger Pinch:   Zoom in/out
 *   Single Tap:         Zoom in 1.5x centered on tap
 *   Double Tap:         Reset view to default
 *   Finger Hold:        Continuous zoom in toward finger (no movement)
 */

#include <SDL2/SDL.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <optional>
#include <algorithm>

// ============================================================================
// Configuration Constants
// ============================================================================

namespace Config
{
    constexpr int INITIAL_WIDTH = 800;
    constexpr int INITIAL_HEIGHT = 600;
    constexpr double MIN_ZOOM = 0.1;
    constexpr double MAX_ZOOM = 1e14;
    constexpr Uint32 DOUBLE_TAP_TIME = 800;
    constexpr int DOUBLE_TAP_DIST = 200;
    constexpr Uint32 TAP_DEBOUNCE_TIME = 500;
    constexpr Uint32 HOLD_ZOOM_DELAY = 150;
    constexpr double HOLD_ZOOM_RATE = 1.16;
    constexpr double WHEEL_ZOOM_FACTOR = 1.15;
    constexpr double CLICK_ZOOM_FACTOR = 1.5;
    constexpr int DEFAULT_MAX_ITER = 256;
}

// ============================================================================
// Pure Functions (Stateless Computations)
// ============================================================================

namespace Mandelbrot
{

    // Calculate escape iterations for a point in the complex plane
    [[nodiscard]] inline int iterate(double cr, double ci, int maxIter) noexcept
    {
        double zr = 0.0, zi = 0.0;
        double zr2 = 0.0, zi2 = 0.0;
        int iter = 0;

        while (zr2 + zi2 <= 4.0 && iter < maxIter)
        {
            zi = 2.0 * zr * zi + ci;
            zr = zr2 - zi2 + cr;
            zr2 = zr * zr;
            zi2 = zi * zi;
            ++iter;
        }
        return iter;
    }

    // Map iteration count to a color (ARGB format: 0xAARRGGBB)
    [[nodiscard]] inline std::uint32_t getColor(int iter, int maxIter) noexcept
    {
        if (iter == maxIter)
        {
            return 0xFF000000; // Black for points in the set
        }

        // Normalize iteration count
        double t = static_cast<double>(iter) / maxIter;

        // Create smooth color gradient using polynomial
        int r = static_cast<int>(9.0 * (1 - t) * t * t * t * 255);
        int g = static_cast<int>(15.0 * (1 - t) * (1 - t) * t * t * 255);
        int b = static_cast<int>(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255);

        // Clamp values
        r = std::clamp(r, 0, 255);
        g = std::clamp(g, 0, 255);
        b = std::clamp(b, 0, 255);

        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

} // namespace Mandelbrot

// ============================================================================
// State Structures
// ============================================================================

struct ViewState
{
    double zoom = 1.0;
    double centerX = -0.5;
    double centerY = 0.0;
    int maxIter = Config::DEFAULT_MAX_ITER;

    [[nodiscard]] double getScale(int renderWidth) const noexcept
    {
        return 4.0 / (renderWidth * zoom);
    }

    void reset() noexcept
    {
        zoom = 1.0;
        centerX = -0.5;
        centerY = 0.0;
    }

    void clampZoom() noexcept
    {
        zoom = std::clamp(zoom, Config::MIN_ZOOM, Config::MAX_ZOOM);
    }

    // Convert screen coordinates to world coordinates
    void screenToWorld(int px, int py, int w, int h, double &wx, double &wy) const noexcept
    {
        double scale = getScale(w);
        wx = centerX + (px - w / 2.0) * scale;
        wy = centerY + (py - h / 2.0) * scale;
    }

    // Zoom toward a world point, keeping it fixed on screen
    void zoomTowardPoint(double worldX, double worldY, int screenX, int screenY,
                         int w, int h, double factor) noexcept
    {
        zoom *= factor;
        clampZoom();
        double newScale = getScale(w);
        centerX = worldX - (screenX - w / 2.0) * newScale;
        centerY = worldY - (screenY - h / 2.0) * newScale;
    }
};

struct MouseState
{
    int buttonHeld = 0; // 0, SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE
    int holdX = 0, holdY = 0;
    Uint32 holdStartTime = 0;
    bool holdZoomActive = false;

    bool isPanning = false;
    int panLastX = 0, panLastY = 0;

    void reset() noexcept
    {
        buttonHeld = 0;
        holdZoomActive = false;
        isPanning = false;
    }
};

struct TouchState
{
    // Finger tracking
    int numFingers = 0;
    SDL_FingerID finger1Id = 0, finger2Id = 0;
    float finger1X = 0, finger1Y = 0;
    float finger2X = 0, finger2Y = 0;

    // Panning
    bool isPanning = false;
    float lastPanX = 0, lastPanY = 0;
    float initialTapX = 0, initialTapY = 0;

    // Pinch-to-zoom
    float initialPinchDist = 0;
    double initialZoom = 1.0;
    double pinchCenterX = 0, pinchCenterY = 0;

    // Hold-to-zoom
    bool holdZoomActive = false;
    Uint32 holdStartTime = 0;
    float holdX = 0, holdY = 0;

    // Tap detection
    Uint32 lastTapTime = 0;
    float lastTapX = 0, lastTapY = 0;
    Uint32 lastZoomTime = 0;

    void reset() noexcept
    {
        numFingers = 0;
        finger1Id = finger2Id = 0;
        isPanning = false;
        holdZoomActive = false;
    }

    [[nodiscard]] float getDistance(int windowW, int windowH) const noexcept
    {
        float dx = (finger2X - finger1X) * windowW;
        float dy = (finger2Y - finger1Y) * windowH;
        return std::sqrt(dx * dx + dy * dy);
    }
};

struct DebugOverlay
{
    int tapX = -1, tapY = -1;
    Uint32 tapTime = 0;

    void setMarker(int x, int y) noexcept
    {
        tapX = x;
        tapY = y;
        tapTime = SDL_GetTicks();
    }

    void clear() noexcept
    {
        tapX = tapY = -1;
    }

    [[nodiscard]] bool isActive(Uint32 now, Uint32 duration = 2000) const noexcept
    {
        return tapX >= 0 && tapY >= 0 && (now - tapTime) < duration;
    }

    void draw(std::uint32_t *pixels, int w, int h) const noexcept
    {
        if (tapX < 0 || tapY < 0)
            return;

        Uint32 age = SDL_GetTicks() - tapTime;
        if (age > 2000)
            return;

        constexpr std::uint32_t color = 0xFF00FF00; // Green
        constexpr int size = 20;

        for (int i = -size; i <= size; ++i)
        {
            int px = tapX + i;
            int py = tapY;
            if (px >= 0 && px < w && py >= 0 && py < h)
                pixels[py * w + px] = color;
            px = tapX;
            py = tapY + i;
            if (px >= 0 && px < w && py >= 0 && py < h)
                pixels[py * w + px] = color;
        }
    }
};

// ============================================================================
// RAII Resource Manager for SDL
// ============================================================================

class SDLResources
{
public:
    // Non-copyable
    SDLResources(const SDLResources &) = delete;
    SDLResources &operator=(const SDLResources &) = delete;

    // Movable
    SDLResources(SDLResources &&other) noexcept
        : window_(other.window_), renderer_(other.renderer_), texture_(other.texture_), pixels_(std::move(other.pixels_)), width_(other.width_), height_(other.height_), ownsSDL_(other.ownsSDL_)
    {
        other.window_ = nullptr;
        other.renderer_ = nullptr;
        other.texture_ = nullptr;
        other.ownsSDL_ = false;
    }

    SDLResources &operator=(SDLResources &&other) noexcept
    {
        if (this != &other)
        {
            cleanup();
            window_ = other.window_;
            renderer_ = other.renderer_;
            texture_ = other.texture_;
            pixels_ = std::move(other.pixels_);
            width_ = other.width_;
            height_ = other.height_;
            ownsSDL_ = other.ownsSDL_;
            other.window_ = nullptr;
            other.renderer_ = nullptr;
            other.texture_ = nullptr;
            other.ownsSDL_ = false;
        }
        return *this;
    }

    ~SDLResources()
    {
        cleanup();
    }

    // Factory method - returns nullopt on failure
    [[nodiscard]] static std::optional<SDLResources> create(int width, int height, const char *title)
    {
        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return std::nullopt;
        }

        // Disable mouse event synthesis from touch on mobile
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

        // Create window
        SDL_Window *window = SDL_CreateWindow(
            title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            width,
            height,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

        if (!window)
        {
            SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return std::nullopt;
        }

        // Get actual window size (may differ on mobile)
        int actualWidth, actualHeight;
        SDL_GetWindowSize(window, &actualWidth, &actualHeight);

        // Create renderer
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return std::nullopt;
        }

        // Create texture
        SDL_Texture *texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            actualWidth,
            actualHeight);

        if (!texture)
        {
            SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return std::nullopt;
        }

        // Allocate pixel buffer
        std::vector<std::uint32_t> pixels(actualWidth * actualHeight);

        return SDLResources(window, renderer, texture, std::move(pixels), actualWidth, actualHeight, true);
    }

    // Accessors
    [[nodiscard]] SDL_Window *window() const noexcept { return window_; }
    [[nodiscard]] SDL_Renderer *renderer() const noexcept { return renderer_; }
    [[nodiscard]] SDL_Texture *texture() const noexcept { return texture_; }
    [[nodiscard]] std::uint32_t *pixels() noexcept { return pixels_.data(); }
    [[nodiscard]] const std::uint32_t *pixels() const noexcept { return pixels_.data(); }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

    // Resize handling
    bool resize(int newWidth, int newHeight)
    {
        if (newWidth == width_ && newHeight == height_)
            return true;

        // Recreate texture
        SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            newWidth,
            newHeight);

        if (!texture_)
        {
            SDL_Log("Failed to recreate texture: %s", SDL_GetError());
            return false;
        }

        // Resize pixel buffer
        pixels_.resize(newWidth * newHeight);
        width_ = newWidth;
        height_ = newHeight;

        SDL_Log("RESIZE: %dx%d", width_, height_);
        return true;
    }

    // Update texture from pixel buffer
    void updateTexture()
    {
        SDL_UpdateTexture(texture_, nullptr, pixels_.data(), width_ * sizeof(std::uint32_t));
    }

    // Get current window size
    void getWindowSize(int &w, int &h) const
    {
        SDL_GetWindowSize(window_, &w, &h);
    }

private:
    SDLResources(SDL_Window *w, SDL_Renderer *r, SDL_Texture *t,
                 std::vector<std::uint32_t> &&p, int width, int height, bool ownsSDL)
        : window_(w), renderer_(r), texture_(t), pixels_(std::move(p)), width_(width), height_(height), ownsSDL_(ownsSDL)
    {
    }

    void cleanup()
    {
        // Cleanup in reverse order of creation
        // pixels_ is automatically cleaned by vector destructor
        if (texture_)
        {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        if (renderer_)
        {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (ownsSDL_)
        {
            SDL_Quit();
            ownsSDL_ = false;
        }
    }

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    std::vector<std::uint32_t> pixels_;
    int width_ = 0;
    int height_ = 0;
    bool ownsSDL_ = false;
};

// ============================================================================
// Main Application Class
// ============================================================================

class MandelbrotApp
{
public:
    MandelbrotApp() = default;

    bool init(int width = Config::INITIAL_WIDTH, int height = Config::INITIAL_HEIGHT)
    {
        auto resources = SDLResources::create(width, height, "Mandelbrot Explorer (SDL2 C++)");
        if (!resources)
        {
            return false;
        }
        sdl_ = std::move(resources);
        sdl_->getWindowSize(windowWidth_, windowHeight_);
        return true;
    }

    void run()
    {
        SDL_Event event;

        while (running_)
        {
            // Process events
            while (SDL_PollEvent(&event))
            {
                handleEvent(event);
            }

            // Update continuous zoom if button/finger is held
            updateMouseHoldZoom();
            updateTouchHoldZoom();

            // Render if needed
            if (needsRedraw_)
            {
                render();
                needsRedraw_ = false;
            }

            // Present
            SDL_RenderClear(sdl_->renderer());
            SDL_RenderCopy(sdl_->renderer(), sdl_->texture(), nullptr, nullptr);
            SDL_RenderPresent(sdl_->renderer());

            // Small delay to prevent CPU spinning
            SDL_Delay(16);
        }
    }

private:
    // ─── Rendering ───

    void render()
    {
        int w = sdl_->width();
        int h = sdl_->height();
        double scale = view_.getScale(w);

        SDL_Log("RENDER: center=(%.6f, %.6f) zoom=%.2f scale=%.10f size=%dx%d",
                view_.centerX, view_.centerY, view_.zoom, scale, w, h);

        // Sanity check
        if (view_.zoom <= 0 || !std::isfinite(view_.centerX) ||
            !std::isfinite(view_.centerY) || !std::isfinite(view_.zoom))
        {
            SDL_Log("ERROR: Invalid render parameters! Resetting to defaults.");
            view_.reset();
            scale = view_.getScale(w);
        }

        auto *pixels = sdl_->pixels();
        for (int py = 0; py < h; ++py)
        {
            double ci = view_.centerY + (py - h / 2.0) * scale;
            for (int px = 0; px < w; ++px)
            {
                double cr = view_.centerX + (px - w / 2.0) * scale;
                int iter = Mandelbrot::iterate(cr, ci, view_.maxIter);
                pixels[py * w + px] = Mandelbrot::getColor(iter, view_.maxIter);
            }
        }

        // Draw debug overlay
        debug_.draw(pixels, w, h);

        // Update texture
        sdl_->updateTexture();
    }

    // ─── Event Handling ───

    void handleEvent(const SDL_Event &event)
    {
        switch (event.type)
        {
        case SDL_QUIT:
            running_ = false;
            break;

        case SDL_KEYDOWN:
            handleKeyDown(event.key);
            break;

        case SDL_MOUSEBUTTONDOWN:
            handleMouseButtonDown(event.button);
            break;

        case SDL_MOUSEBUTTONUP:
            handleMouseButtonUp(event.button);
            break;

        case SDL_MOUSEMOTION:
            handleMouseMotion(event.motion);
            break;

        case SDL_MOUSEWHEEL:
        {
            int mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            handleMouseWheel(event.wheel, mouseX, mouseY);
            break;
        }

        case SDL_FINGERDOWN:
            handleFingerDown(event.tfinger);
            break;

        case SDL_FINGERUP:
            handleFingerUp(event.tfinger);
            break;

        case SDL_FINGERMOTION:
            handleFingerMotion(event.tfinger);
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                event.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                handleWindowResize();
            }
            break;
        }
    }

    void handleKeyDown(const SDL_KeyboardEvent &event)
    {
        if (event.keysym.sym == SDLK_ESCAPE)
        {
            running_ = false;
        }
    }

    void handleMouseButtonDown(const SDL_MouseButtonEvent &event)
    {
        if (event.button == SDL_BUTTON_MIDDLE)
        {
            mouse_.isPanning = true;
            mouse_.panLastX = event.x;
            mouse_.panLastY = event.y;
            mouse_.buttonHeld = SDL_BUTTON_MIDDLE;
        }
        else if (event.button == SDL_BUTTON_LEFT || event.button == SDL_BUTTON_RIGHT)
        {
            mouse_.buttonHeld = event.button;
            mouse_.holdX = event.x;
            mouse_.holdY = event.y;
            mouse_.holdStartTime = SDL_GetTicks();
            mouse_.holdZoomActive = false;
            mouse_.isPanning = false;
        }
    }

    void handleMouseButtonUp(const SDL_MouseButtonEvent &event)
    {
        if (event.button != mouse_.buttonHeld)
            return;

        // Middle button was just panning
        if (event.button == SDL_BUTTON_MIDDLE)
        {
            mouse_.isPanning = false;
            mouse_.buttonHeld = 0;
            return;
        }

        // If we were panning with left/right, don't zoom
        if (mouse_.isPanning)
        {
            mouse_.reset();
            return;
        }

        // If we never entered hold-zoom mode, do a single click zoom
        if (!mouse_.holdZoomActive)
        {
            int w = sdl_->width();
            int h = sdl_->height();

            double worldX, worldY;
            view_.screenToWorld(event.x, event.y, w, h, worldX, worldY);

            double factor = (event.button == SDL_BUTTON_LEFT) ? Config::CLICK_ZOOM_FACTOR
                                                              : (1.0 / Config::CLICK_ZOOM_FACTOR);
            view_.zoomTowardPoint(worldX, worldY, event.x, event.y, w, h, factor);
            needsRedraw_ = true;
        }

        mouse_.buttonHeld = 0;
        mouse_.holdZoomActive = false;
    }

    void handleMouseMotion(const SDL_MouseMotionEvent &event)
    {
        if (mouse_.buttonHeld == 0)
            return;

        // For left/right, check if we should start panning
        if (!mouse_.isPanning && (mouse_.buttonHeld == SDL_BUTTON_LEFT ||
                                  mouse_.buttonHeld == SDL_BUTTON_RIGHT))
        {
            int dx = std::abs(event.x - mouse_.holdX);
            int dy = std::abs(event.y - mouse_.holdY);
            if (dx > 10 || dy > 10)
            {
                mouse_.isPanning = true;
                mouse_.holdZoomActive = false;
                mouse_.panLastX = event.x;
                mouse_.panLastY = event.y;
            }
        }

        if (mouse_.isPanning)
        {
            int dx = event.x - mouse_.panLastX;
            int dy = event.y - mouse_.panLastY;

            double scale = view_.getScale(sdl_->width());
            view_.centerX -= dx * scale;
            view_.centerY -= dy * scale;

            mouse_.panLastX = event.x;
            mouse_.panLastY = event.y;
            needsRedraw_ = true;
        }
    }

    void handleMouseWheel(const SDL_MouseWheelEvent &event, int mouseX, int mouseY)
    {
        int w = sdl_->width();
        int h = sdl_->height();

        double worldX, worldY;
        view_.screenToWorld(mouseX, mouseY, w, h, worldX, worldY);

        double factor = (event.y > 0) ? Config::WHEEL_ZOOM_FACTOR
                                      : (1.0 / Config::WHEEL_ZOOM_FACTOR);
        view_.zoomTowardPoint(worldX, worldY, mouseX, mouseY, w, h, factor);
        needsRedraw_ = true;
    }

    void handleFingerDown(const SDL_TouchFingerEvent &event)
    {
        float x = event.x;
        float y = event.y;

        // Normalize if needed
        if (x > 1.0f || y > 1.0f)
        {
            SDL_Log("WARNING: Touch coords not normalized! x=%.1f y=%.1f - normalizing", x, y);
            x /= windowWidth_;
            y /= windowHeight_;
        }

        SDL_Log("FINGER DOWN: id=%lld x=%.3f y=%.3f numFingers=%d",
                static_cast<long long>(event.fingerId), x, y, touch_.numFingers);

        if (touch_.numFingers == 0)
        {
            touch_.finger1Id = event.fingerId;
            touch_.finger1X = x;
            touch_.finger1Y = y;
            touch_.numFingers = 1;
            touch_.isPanning = false;
            touch_.lastPanX = x;
            touch_.lastPanY = y;
            touch_.initialTapX = x;
            touch_.initialTapY = y;
            touch_.holdStartTime = SDL_GetTicks();
            touch_.holdX = x;
            touch_.holdY = y;
            touch_.holdZoomActive = false;
        }
        else if (touch_.numFingers == 1)
        {
            touch_.finger2Id = event.fingerId;
            touch_.finger2X = x;
            touch_.finger2Y = y;
            touch_.numFingers = 2;
            touch_.isPanning = false;

            // Initialize pinch gesture
            touch_.initialPinchDist = touch_.getDistance(windowWidth_, windowHeight_);
            touch_.initialZoom = view_.zoom;

            // Calculate pinch center in world coordinates
            int w = sdl_->width();
            int h = sdl_->height();
            float centerPixelX = (touch_.finger1X + touch_.finger2X) / 2.0f * w;
            float centerPixelY = (touch_.finger1Y + touch_.finger2Y) / 2.0f * h;
            view_.screenToWorld(static_cast<int>(centerPixelX), static_cast<int>(centerPixelY),
                                w, h, touch_.pinchCenterX, touch_.pinchCenterY);
        }
    }

    void handleFingerUp(const SDL_TouchFingerEvent &event)
    {
        float ex = event.x;
        float ey = event.y;

        // Normalize if needed
        if (ex > 1.0f || ey > 1.0f)
        {
            ex /= windowWidth_;
            ey /= windowHeight_;
        }

        SDL_Log("FINGER UP: id=%lld x=%.3f y=%.3f window=%dx%d",
                static_cast<long long>(event.fingerId), ex, ey, windowWidth_, windowHeight_);

        if (touch_.numFingers == 1 && event.fingerId == touch_.finger1Id)
        {
            // Check for tap
            float dx = std::abs(ex - touch_.initialTapX) * windowWidth_;
            float dy = std::abs(ey - touch_.initialTapY) * windowHeight_;

            SDL_Log("TAP CHECK: dx=%.1f dy=%.1f (threshold=20) isPanning=%d", dx, dy, touch_.isPanning);

            if (!touch_.isPanning && dx < 20 && dy < 20)
            {
                processTap(ex, ey);
            }

            touch_.reset();
        }
        else if (touch_.numFingers == 2)
        {
            // One finger released during pinch
            if (event.fingerId == touch_.finger1Id)
            {
                touch_.finger1Id = touch_.finger2Id;
                touch_.finger1X = touch_.finger2X;
                touch_.finger1Y = touch_.finger2Y;
            }
            touch_.finger2Id = 0;
            touch_.numFingers = 1;
            touch_.isPanning = true;
            touch_.lastPanX = touch_.finger1X;
            touch_.lastPanY = touch_.finger1Y;
            touch_.initialTapX = touch_.finger1X;
            touch_.initialTapY = touch_.finger1Y;
        }
        else if (event.fingerId == touch_.finger1Id)
        {
            touch_.reset();
        }
    }

    void processTap(float ex, float ey)
    {
        Uint32 currentTime = SDL_GetTicks();
        int w = sdl_->width();
        int h = sdl_->height();

        float tapX = ex * w;
        float tapY = ey * h;

        debug_.setMarker(static_cast<int>(tapX), static_cast<int>(tapY));

        SDL_Log("TAP: normalized=(%.3f,%.3f) -> render=(%.1f,%.1f) window=%dx%d",
                ex, ey, tapX, tapY, windowWidth_, windowHeight_);

        // Check for double-tap
        float tapDist = std::sqrt((tapX - touch_.lastTapX) * (tapX - touch_.lastTapX) +
                                  (tapY - touch_.lastTapY) * (tapY - touch_.lastTapY));
        Uint32 timeSinceLastTap = currentTime - touch_.lastTapTime;

        SDL_Log("DOUBLE-TAP CHECK: timeSince=%u dist=%.1f (need <%u ms, <%d px)",
                timeSinceLastTap, tapDist, Config::DOUBLE_TAP_TIME, Config::DOUBLE_TAP_DIST);

        if (timeSinceLastTap < Config::DOUBLE_TAP_TIME &&
            tapDist < Config::DOUBLE_TAP_DIST)
        {
            // Double-tap: reset view
            SDL_Log("DOUBLE-TAP: Resetting view to default");
            view_.reset();
            touch_.lastZoomTime = SDL_GetTicks();
            needsRedraw_ = true;
            touch_.lastTapTime = 0;
            touch_.lastTapX = touch_.lastTapY = 0;
        }
        else if (currentTime - touch_.lastZoomTime < Config::TAP_DEBOUNCE_TIME)
        {
            // Debounce
            SDL_Log("TAP DEBOUNCED: ignoring (only %u ms since last zoom)",
                    currentTime - touch_.lastZoomTime);
            touch_.lastTapX = tapX;
            touch_.lastTapY = tapY;
        }
        else
        {
            // Single tap: zoom in
            double worldX, worldY;
            view_.screenToWorld(static_cast<int>(tapX), static_cast<int>(tapY), w, h, worldX, worldY);

            if (view_.zoom * Config::CLICK_ZOOM_FACTOR <= Config::MAX_ZOOM)
            {
                view_.zoomTowardPoint(worldX, worldY, static_cast<int>(tapX),
                                      static_cast<int>(tapY), w, h, Config::CLICK_ZOOM_FACTOR);
                touch_.lastZoomTime = SDL_GetTicks();
                needsRedraw_ = true;
            }

            touch_.lastTapTime = currentTime;
            touch_.lastTapX = tapX;
            touch_.lastTapY = tapY;
        }
    }

    void handleFingerMotion(const SDL_TouchFingerEvent &event)
    {
        float x = event.x;
        float y = event.y;

        // Normalize if needed
        if (x > 1.0f || y > 1.0f)
        {
            x /= windowWidth_;
            y /= windowHeight_;
        }

        if (touch_.numFingers == 1 && event.fingerId == touch_.finger1Id)
        {
            float moveDist = std::sqrt((x - touch_.initialTapX) * (x - touch_.initialTapX) +
                                       (y - touch_.initialTapY) * (y - touch_.initialTapY)) *
                             windowWidth_;

            SDL_Log("FINGER MOTION: moveDist=%.1f isPanning=%d holdZoom=%d",
                    moveDist, touch_.isPanning, touch_.holdZoomActive);

            // Disable hold-zoom if moved
            if (moveDist > 5)
            {
                touch_.holdZoomActive = false;
                touch_.holdStartTime = 0xFFFFFFFF;
            }

            if (moveDist > 15 && !touch_.isPanning)
            {
                touch_.isPanning = true;
                touch_.lastPanX = x;
                touch_.lastPanY = y;
                SDL_Log("PANNING STARTED");
            }

            if (touch_.isPanning)
            {
                float dx = x - touch_.lastPanX;
                float dy = y - touch_.lastPanY;

                int w = sdl_->width();
                int h = sdl_->height();
                double scale = view_.getScale(w);
                view_.centerX -= dx * w * scale;
                view_.centerY -= dy * h * scale;

                touch_.lastPanX = x;
                touch_.lastPanY = y;
                touch_.finger1X = x;
                touch_.finger1Y = y;
                needsRedraw_ = true;
            }
        }
        else if (touch_.numFingers == 2)
        {
            // Update finger position
            if (event.fingerId == touch_.finger1Id)
            {
                touch_.finger1X = x;
                touch_.finger1Y = y;
            }
            else if (event.fingerId == touch_.finger2Id)
            {
                touch_.finger2X = x;
                touch_.finger2Y = y;
            }

            // Calculate pinch zoom
            float currentDist = touch_.getDistance(windowWidth_, windowHeight_);
            if (touch_.initialPinchDist > 0)
            {
                double zoomFactor = currentDist / touch_.initialPinchDist;
                double newZoom = std::clamp(touch_.initialZoom * zoomFactor,
                                            Config::MIN_ZOOM, Config::MAX_ZOOM);
                view_.zoom = newZoom;

                // Keep pinch center fixed
                int w = sdl_->width();
                int h = sdl_->height();
                float centerPixelX = (touch_.finger1X + touch_.finger2X) / 2.0f * w;
                float centerPixelY = (touch_.finger1Y + touch_.finger2Y) / 2.0f * h;
                double newScale = view_.getScale(w);
                view_.centerX = touch_.pinchCenterX - (centerPixelX - w / 2.0) * newScale;
                view_.centerY = touch_.pinchCenterY - (centerPixelY - h / 2.0) * newScale;

                needsRedraw_ = true;
            }
        }
    }

    void handleWindowResize()
    {
        sdl_->getWindowSize(windowWidth_, windowHeight_);

        if (windowWidth_ != sdl_->width() || windowHeight_ != sdl_->height())
        {
            sdl_->resize(windowWidth_, windowHeight_);
        }
        needsRedraw_ = true;
    }

    // ─── Continuous Zoom Updates ───

    void updateMouseHoldZoom()
    {
        if (mouse_.buttonHeld == 0 || mouse_.isPanning)
            return;

        Uint32 now = SDL_GetTicks();
        if (!mouse_.holdZoomActive)
        {
            if (now - mouse_.holdStartTime >= Config::HOLD_ZOOM_DELAY)
            {
                mouse_.holdZoomActive = true;
            }
            else
            {
                return;
            }
        }

        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);

        int w = sdl_->width();
        int h = sdl_->height();

        double worldX, worldY;
        view_.screenToWorld(mouseX, mouseY, w, h, worldX, worldY);

        double factor = (mouse_.buttonHeld == SDL_BUTTON_LEFT)
                            ? Config::HOLD_ZOOM_RATE
                            : (1.0 / Config::HOLD_ZOOM_RATE);

        double newZoom = view_.zoom * factor;
        if (newZoom >= Config::MIN_ZOOM && newZoom <= Config::MAX_ZOOM)
        {
            view_.zoomTowardPoint(worldX, worldY, mouseX, mouseY, w, h, factor);
            needsRedraw_ = true;
        }
    }

    void updateTouchHoldZoom()
    {
        if (touch_.numFingers != 1 || touch_.isPanning)
            return;

        Uint32 now = SDL_GetTicks();
        if (!touch_.holdZoomActive)
        {
            if (now - touch_.holdStartTime >= Config::HOLD_ZOOM_DELAY)
            {
                touch_.holdZoomActive = true;
            }
            else
            {
                return;
            }
        }

        int w = sdl_->width();
        int h = sdl_->height();
        float touchPixelX = touch_.holdX * w;
        float touchPixelY = touch_.holdY * h;

        double worldX, worldY;
        view_.screenToWorld(static_cast<int>(touchPixelX), static_cast<int>(touchPixelY),
                            w, h, worldX, worldY);

        double newZoom = view_.zoom * Config::HOLD_ZOOM_RATE;
        if (newZoom <= Config::MAX_ZOOM)
        {
            view_.zoomTowardPoint(worldX, worldY, static_cast<int>(touchPixelX),
                                  static_cast<int>(touchPixelY), w, h, Config::HOLD_ZOOM_RATE);
            needsRedraw_ = true;
        }
    }

    // ─── Member Variables ───

    std::optional<SDLResources> sdl_;
    ViewState view_;
    MouseState mouse_;
    TouchState touch_;
    DebugOverlay debug_;

    int windowWidth_ = Config::INITIAL_WIDTH;
    int windowHeight_ = Config::INITIAL_HEIGHT;
    bool needsRedraw_ = true;
    bool running_ = true;
};

// ============================================================================
// Main Entry Point
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char *argv[])
{
    MandelbrotApp app;

    if (!app.init())
    {
        return 1;
    }

    app.run();
    return 0;
}
