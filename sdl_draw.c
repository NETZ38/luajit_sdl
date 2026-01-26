/*
 * Mandelbrot Set Explorer - SDL2 Version
 * Converted from Windows GDI to SDL2
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
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define INITIAL_WIDTH 800
#define INITIAL_HEIGHT 600

// Mandelbrot parameters
static double zoom = 1.0;
static double center_x = -0.5;
static double center_y = 0.0;
static int max_iter = 256;

// Dynamic render size (matches window)
static int renderWidth = INITIAL_WIDTH;
static int renderHeight = INITIAL_HEIGHT;

// Zoom limits to prevent issues
#define MIN_ZOOM 0.1
#define MAX_ZOOM 1e14

// SDL globals
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint32_t *pixels = NULL;
static int needsRedraw = 1;
static int windowWidth = INITIAL_WIDTH;
static int windowHeight = INITIAL_HEIGHT;

// Touch state for multi-touch gestures
static int numFingers = 0;
static SDL_FingerID finger1_id = 0, finger2_id = 0;
static float finger1_x = 0, finger1_y = 0;
static float finger2_x = 0, finger2_y = 0;
static float initialPinchDist = 0;
static double initialZoom = 1.0;
static double pinchCenterX = 0, pinchCenterY = 0;
static int isPanning = 0;
static float lastPanX = 0, lastPanY = 0;
static float initialTapX = 0, initialTapY = 0; // Track initial touch position for tap detection

// Double-tap detection
static Uint32 lastTapTime = 0;
static float lastTapX = 0, lastTapY = 0;
#define DOUBLE_TAP_TIME 800   // Very easy - almost 1 second window
#define DOUBLE_TAP_DIST 200   // Very easy - large position tolerance
#define TAP_DEBOUNCE_TIME 500 // Minimum time between single-tap zooms

// Debug: visual marker for tap position
static int debugTapX = -1, debugTapY = -1;
static Uint32 debugTapTime = 0;
static Uint32 lastZoomTime = 0; // Debounce for zoom actions

// Hold-to-zoom state (mouse and touch)
static int mouseButtonHeld = 0;            // Which button is held (0=none, 1=left, 3=right)
static int mouseHoldX = 0, mouseHoldY = 0; // Position where button was pressed
static Uint32 mouseHoldStartTime = 0;      // When button was pressed
static int holdZoomActive = 0;             // Whether we're in hold-zoom mode (vs click-zoom)
#define HOLD_ZOOM_DELAY 150                // ms before hold-zoom activates (allows quick clicks)
#define HOLD_ZOOM_RATE 1.16                // Zoom factor per frame when holding (~16% per frame)

// Touch hold-to-zoom state
static int touchHoldZoomActive = 0;          // Whether touch hold-zoom is active
static Uint32 touchHoldStartTime = 0;        // When single finger touch started
static float touchHoldX = 0, touchHoldY = 0; // Position of touch hold

// Mouse panning state
static int mousePanning = 0;                     // Whether mouse is panning
static int mousePanLastX = 0, mousePanLastY = 0; // Last mouse position for pan delta

// Calculate escape iterations for a point in the complex plane
static int mandelbrot(double cr, double ci)
{
    double zr = 0.0, zi = 0.0;
    double zr2 = 0.0, zi2 = 0.0;
    int iter = 0;

    while (zr2 + zi2 <= 4.0 && iter < max_iter)
    {
        zi = 2.0 * zr * zi + ci;
        zr = zr2 - zi2 + cr;
        zr2 = zr * zr;
        zi2 = zi * zi;
        iter++;
    }
    return iter;
}

// Map iteration count to a color (ARGB format: 0xAARRGGBB)
static uint32_t getColor(int iter)
{
    if (iter == max_iter)
    {
        return 0xFF000000; // Black for points in the set
    }

    // Normalize iteration count
    double t = (double)iter / max_iter;

    // Create smooth color gradient using polynomial
    int r = (int)(9.0 * (1 - t) * t * t * t * 255);
    int g = (int)(15.0 * (1 - t) * (1 - t) * t * t * 255);
    int b = (int)(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255);

    // Clamp values
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// Render the Mandelbrot set to the pixel buffer
static void renderMandelbrot(void)
{
    double scale = 4.0 / (renderWidth * zoom);

    SDL_Log("RENDER: center=(%.6f, %.6f) zoom=%.2f scale=%.10f size=%dx%d", center_x, center_y, zoom, scale, renderWidth, renderHeight);

    // Sanity check - ensure we're not rendering garbage
    if (zoom <= 0 || !isfinite(center_x) || !isfinite(center_y) || !isfinite(zoom))
    {
        SDL_Log("ERROR: Invalid render parameters! Resetting to defaults.");
        center_x = -0.5;
        center_y = 0.0;
        zoom = 1.0;
        scale = 4.0 / (renderWidth * zoom);
    }

    for (int py = 0; py < renderHeight; py++)
    {
        double ci = center_y + (py - renderHeight / 2.0) * scale;
        for (int px = 0; px < renderWidth; px++)
        {
            double cr = center_x + (px - renderWidth / 2.0) * scale;
            int iter = mandelbrot(cr, ci);
            pixels[py * renderWidth + px] = getColor(iter);
        }
    }

    // Draw debug marker if active (green cross at tap position)
    if (debugTapX >= 0 && debugTapY >= 0)
    {
        Uint32 age = SDL_GetTicks() - debugTapTime;
        if (age > 2000)
        {
            debugTapX = -1;
            debugTapY = -1;
        }
        else
        {
            uint32_t color = 0xFF00FF00; // Green
            int size = 20;
            for (int i = -size; i <= size; i++)
            {
                int px = debugTapX + i;
                int py = debugTapY;
                if (px >= 0 && px < renderWidth && py >= 0 && py < renderHeight)
                    pixels[py * renderWidth + px] = color;
                px = debugTapX;
                py = debugTapY + i;
                if (px >= 0 && px < renderWidth && py >= 0 && py < renderHeight)
                    pixels[py * renderWidth + px] = color;
            }
        }
    }

    // Update texture with new pixel data
    SDL_UpdateTexture(texture, NULL, pixels, renderWidth * sizeof(uint32_t));
}

// Handle mouse button events for zooming (down and up)
static void handleMouseButtonDown(SDL_MouseButtonEvent *event)
{
    if (event->button == SDL_BUTTON_MIDDLE)
    {
        // Middle button starts panning immediately
        mousePanning = 1;
        mousePanLastX = event->x;
        mousePanLastY = event->y;
        mouseButtonHeld = SDL_BUTTON_MIDDLE;
    }
    else if (event->button == SDL_BUTTON_LEFT || event->button == SDL_BUTTON_RIGHT)
    {
        mouseButtonHeld = event->button;
        mouseHoldX = event->x;
        mouseHoldY = event->y;
        mouseHoldStartTime = SDL_GetTicks();
        holdZoomActive = 0; // Will activate after HOLD_ZOOM_DELAY
        mousePanning = 0;
    }
}

static void handleMouseButtonUp(SDL_MouseButtonEvent *event)
{
    if (event->button == mouseButtonHeld)
    {
        // Middle button was just panning
        if (event->button == SDL_BUTTON_MIDDLE)
        {
            mousePanning = 0;
            mouseButtonHeld = 0;
            return;
        }

        // If we were panning with left/right, don't zoom
        if (mousePanning)
        {
            mousePanning = 0;
            mouseButtonHeld = 0;
            holdZoomActive = 0;
            return;
        }

        // If we never entered hold-zoom mode, do a single click zoom
        if (!holdZoomActive)
        {
            double scale = 4.0 / (renderWidth * zoom);
            double worldX = center_x + (event->x - renderWidth / 2.0) * scale;
            double worldY = center_y + (event->y - renderHeight / 2.0) * scale;

            if (event->button == SDL_BUTTON_LEFT)
            {
                // Zoom in
                zoom *= 1.5;
            }
            else if (event->button == SDL_BUTTON_RIGHT)
            {
                // Zoom out
                zoom /= 1.5;
            }

            // Recalculate center to keep clicked point under cursor
            double newScale = 4.0 / (renderWidth * zoom);
            center_x = worldX - (event->x - renderWidth / 2.0) * newScale;
            center_y = worldY - (event->y - renderHeight / 2.0) * newScale;
            needsRedraw = 1;
        }
        mouseButtonHeld = 0;
        holdZoomActive = 0;
    }
}

// Handle mouse motion for panning
static void handleMouseMotion(SDL_MouseMotionEvent *event)
{
    if (mouseButtonHeld == 0)
        return;

    // For middle button, we're always panning (already set in mousedown)
    // For left/right, check if we should start panning (mouse moved while button held)
    if (!mousePanning && (mouseButtonHeld == SDL_BUTTON_LEFT || mouseButtonHeld == SDL_BUTTON_RIGHT))
    {
        int dx = abs(event->x - mouseHoldX);
        int dy = abs(event->y - mouseHoldY);
        if (dx > 10 || dy > 10)
        {
            // Movement detected - start panning, cancel hold-zoom
            mousePanning = 1;
            holdZoomActive = 0; // Cancel any zoom that may have started
            mousePanLastX = event->x;
            mousePanLastY = event->y;
        }
    }

    if (mousePanning)
    {
        // Pan the view
        int dx = event->x - mousePanLastX;
        int dy = event->y - mousePanLastY;

        double scale = 4.0 / (renderWidth * zoom);
        center_x -= dx * scale;
        center_y -= dy * scale;

        mousePanLastX = event->x;
        mousePanLastY = event->y;
        needsRedraw = 1;
    }
}

// Continuous zoom while mouse button is held
static void updateMouseHoldZoom(void)
{
    if (mouseButtonHeld == 0 || mousePanning)
        return;

    Uint32 now = SDL_GetTicks();
    if (!holdZoomActive)
    {
        // Check if we've held long enough to activate hold-zoom
        if (now - mouseHoldStartTime >= HOLD_ZOOM_DELAY)
        {
            holdZoomActive = 1;
        }
        else
        {
            return; // Not yet in hold-zoom mode
        }
    }

    // Get current mouse position for zooming toward it
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);

    double scale = 4.0 / (renderWidth * zoom);
    double worldX = center_x + (mouseX - renderWidth / 2.0) * scale;
    double worldY = center_y + (mouseY - renderHeight / 2.0) * scale;

    if (mouseButtonHeld == SDL_BUTTON_LEFT)
    {
        // Zoom in
        double newZoom = zoom * HOLD_ZOOM_RATE;
        if (newZoom <= MAX_ZOOM)
            zoom = newZoom;
    }
    else if (mouseButtonHeld == SDL_BUTTON_RIGHT)
    {
        // Zoom out
        double newZoom = zoom / HOLD_ZOOM_RATE;
        if (newZoom >= MIN_ZOOM)
            zoom = newZoom;
    }

    // Recalculate center to keep point under cursor fixed
    double newScale = 4.0 / (renderWidth * zoom);
    center_x = worldX - (mouseX - renderWidth / 2.0) * newScale;
    center_y = worldY - (mouseY - renderHeight / 2.0) * newScale;

    needsRedraw = 1;
}

// Handle mouse wheel events for smooth zooming
static void handleMouseWheel(SDL_MouseWheelEvent *event, int mouseX, int mouseY)
{
    double scale = 4.0 / (renderWidth * zoom);
    double worldX = center_x + (mouseX - renderWidth / 2.0) * scale;
    double worldY = center_y + (mouseY - renderHeight / 2.0) * scale;

    if (event->y > 0)
    {
        // Scroll up - zoom in
        zoom *= 1.15;
    }
    else if (event->y < 0)
    {
        // Scroll down - zoom out
        zoom /= 1.15;
    }

    // Recalculate to keep point under cursor fixed
    double newScale = 4.0 / (renderWidth * zoom);
    center_x = worldX - (mouseX - renderWidth / 2.0) * newScale;
    center_y = worldY - (mouseY - renderHeight / 2.0) * newScale;

    needsRedraw = 1;
}

// Calculate distance between two touch points
static float getTouchDistance(float x1, float y1, float x2, float y2)
{
    float dx = (x2 - x1) * windowWidth;
    float dy = (y2 - y1) * windowHeight;
    return sqrtf(dx * dx + dy * dy);
}

// Handle touch finger down
static void handleFingerDown(SDL_TouchFingerEvent *event)
{
    float x = event->x;
    float y = event->y;
    SDL_FingerID id = event->fingerId;

    // SDL touch coordinates should be normalized [0,1], but verify
    // If they appear to be pixel coordinates, normalize them
    if (x > 1.0f || y > 1.0f)
    {
        SDL_Log("WARNING: Touch coords not normalized! x=%.1f y=%.1f - normalizing", x, y);
        x = x / windowWidth;
        y = y / windowHeight;
    }

    SDL_Log("FINGER DOWN: id=%lld x=%.3f y=%.3f numFingers=%d", (long long)id, x, y, numFingers);

    if (numFingers == 0)
    {
        finger1_id = id;
        finger1_x = x;
        finger1_y = y;
        numFingers = 1;
        isPanning = 0; // Don't start panning immediately - wait for movement
        lastPanX = x;
        lastPanY = y;
        initialTapX = x; // Store initial position for tap detection
        initialTapY = y;
        // Set up for potential hold-to-zoom
        touchHoldStartTime = SDL_GetTicks();
        touchHoldX = x;
        touchHoldY = y;
        touchHoldZoomActive = 0;
    }
    else if (numFingers == 1)
    {
        finger2_id = id;
        finger2_x = x;
        finger2_y = y;
        numFingers = 2;
        isPanning = 0;

        // Initialize pinch gesture
        initialPinchDist = getTouchDistance(finger1_x, finger1_y, finger2_x, finger2_y);
        initialZoom = zoom;

        // Calculate pinch center in world coordinates (map to rendering space)
        float centerPixelX = (finger1_x + finger2_x) / 2.0f * renderWidth;
        float centerPixelY = (finger1_y + finger2_y) / 2.0f * renderHeight;
        double scale = 4.0 / (renderWidth * zoom);
        pinchCenterX = center_x + (centerPixelX - renderWidth / 2.0) * scale;
        pinchCenterY = center_y + (centerPixelY - renderHeight / 2.0) * scale;
    }
}

// Handle touch finger up
static void handleFingerUp(SDL_TouchFingerEvent *event)
{
    SDL_FingerID id = event->fingerId;
    float ex = event->x;
    float ey = event->y;

    // Normalize if needed
    if (ex > 1.0f || ey > 1.0f)
    {
        ex = ex / windowWidth;
        ey = ey / windowHeight;
    }

    SDL_Log("FINGER UP: id=%lld x=%.3f y=%.3f window=%dx%d",
            (long long)id, ex, ey, windowWidth, windowHeight);

    if (numFingers == 1 && id == finger1_id)
    {
        // Check for tap (minimal movement from initial touch position)
        // Only process tap if we weren't panning
        float dx = fabsf(ex - initialTapX) * windowWidth;
        float dy = fabsf(ey - initialTapY) * windowHeight;

        SDL_Log("TAP CHECK: dx=%.1f dy=%.1f (threshold=20) isPanning=%d", dx, dy, isPanning);

        if (!isPanning && dx < 20 && dy < 20)
        {
            Uint32 currentTime = SDL_GetTicks();

            // Normalized touch coords [0,1] map directly to render coords
            float tapX = ex * renderWidth;
            float tapY = ey * renderHeight;

            // Set debug marker to show where we think the tap is
            debugTapX = (int)tapX;
            debugTapY = (int)tapY;
            debugTapTime = SDL_GetTicks();

            SDL_Log("TAP: normalized=(%.3f,%.3f) -> render=(%.1f,%.1f) window=%dx%d",
                    ex, ey, tapX, tapY, windowWidth, windowHeight);

            // Check for double-tap FIRST (before debounce check)
            float tapDist = sqrtf((tapX - lastTapX) * (tapX - lastTapX) +
                                  (tapY - lastTapY) * (tapY - lastTapY));
            Uint32 timeSinceLastTap = currentTime - lastTapTime;

            SDL_Log("DOUBLE-TAP CHECK: timeSince=%u dist=%.1f (need <%d ms, <%.0f px)",
                    timeSinceLastTap, tapDist, DOUBLE_TAP_TIME, (float)DOUBLE_TAP_DIST);

            if (timeSinceLastTap < DOUBLE_TAP_TIME && tapDist < DOUBLE_TAP_DIST)
            {
                // Double-tap: RESET VIEW
                SDL_Log("DOUBLE-TAP: Resetting view to default");
                center_x = -0.5;
                center_y = 0.0;
                zoom = 1.0;
                lastZoomTime = SDL_GetTicks();
                needsRedraw = 1;
                lastTapTime = 0;
                lastTapX = 0;
                lastTapY = 0;
            }
            // Debounce: skip single-tap zoom if we zoomed too recently
            else if (currentTime - lastZoomTime < TAP_DEBOUNCE_TIME)
            {
                SDL_Log("TAP DEBOUNCED: ignoring (only %u ms since last zoom)", currentTime - lastZoomTime);
                // Update position for double-tap distance check, but not time
                lastTapX = tapX;
                lastTapY = tapY;
            }
            else
            {
                // Single tap: zoom in keeping tap location fixed
                double scale = 4.0 / (renderWidth * zoom);
                double worldX = center_x + (tapX - renderWidth / 2.0) * scale;
                double worldY = center_y + (tapY - renderHeight / 2.0) * scale;

                double newZoom = zoom * 1.5;
                if (newZoom <= MAX_ZOOM)
                {
                    zoom = newZoom;
                    // Recalculate center to keep tapped point under finger
                    double newScale = 4.0 / (renderWidth * zoom);
                    center_x = worldX - (tapX - renderWidth / 2.0) * newScale;
                    center_y = worldY - (tapY - renderHeight / 2.0) * newScale;
                    lastZoomTime = SDL_GetTicks();
                    needsRedraw = 1;
                }
                lastTapTime = currentTime;
                lastTapX = tapX;
                lastTapY = tapY;
            }
        }
        finger1_id = 0;
        numFingers = 0;
        isPanning = 0;
        touchHoldZoomActive = 0;
    }
    else if (numFingers == 2)
    {
        // One finger released during pinch - determine which one
        if (id == finger1_id)
        {
            // Finger 1 released, finger 2 becomes finger 1
            finger1_id = finger2_id;
            finger1_x = finger2_x;
            finger1_y = finger2_y;
        }
        // else finger2 released, finger1 stays
        finger2_id = 0;
        numFingers = 1;
        // Resume panning with remaining finger
        isPanning = 1;
        lastPanX = finger1_x;
        lastPanY = finger1_y;
        initialTapX = finger1_x; // Reset for potential tap
        initialTapY = finger1_y;
    }
    else if (id == finger1_id)
    {
        finger1_id = 0;
        numFingers = 0;
        isPanning = 0;
    }
}

// Continuous zoom while finger is held (without panning)
static void updateTouchHoldZoom(void)
{
    // Only zoom if we have exactly 1 finger and we're not panning
    if (numFingers != 1 || isPanning)
        return;

    Uint32 now = SDL_GetTicks();
    if (!touchHoldZoomActive)
    {
        // Check if we've held long enough to activate hold-zoom
        if (now - touchHoldStartTime >= HOLD_ZOOM_DELAY)
        {
            touchHoldZoomActive = 1;
        }
        else
        {
            return; // Not yet in hold-zoom mode
        }
    }

    // Zoom toward the touch position
    float touchPixelX = touchHoldX * renderWidth;
    float touchPixelY = touchHoldY * renderHeight;

    double scale = 4.0 / (renderWidth * zoom);
    double worldX = center_x + (touchPixelX - renderWidth / 2.0) * scale;
    double worldY = center_y + (touchPixelY - renderHeight / 2.0) * scale;

    // Always zoom in for touch hold
    double newZoom = zoom * HOLD_ZOOM_RATE;
    if (newZoom <= MAX_ZOOM)
        zoom = newZoom;

    // Recalculate center to keep point under finger fixed
    double newScale = 4.0 / (renderWidth * zoom);
    center_x = worldX - (touchPixelX - renderWidth / 2.0) * newScale;
    center_y = worldY - (touchPixelY - renderHeight / 2.0) * newScale;

    needsRedraw = 1;
}

// Handle touch finger motion
static void handleFingerMotion(SDL_TouchFingerEvent *event)
{
    float x = event->x;
    float y = event->y;
    SDL_FingerID id = event->fingerId;

    // Normalize if needed
    if (x > 1.0f || y > 1.0f)
    {
        x = x / windowWidth;
        y = y / windowHeight;
    }

    if (numFingers == 1 && id == finger1_id)
    {
        // Check if we've moved enough to start panning
        float moveDist = sqrtf((x - initialTapX) * (x - initialTapX) +
                               (y - initialTapY) * (y - initialTapY)) *
                         windowWidth;

        SDL_Log("FINGER MOTION: moveDist=%.1f isPanning=%d holdZoom=%d", moveDist, isPanning, touchHoldZoomActive);

        // Any movement at all (> 5 pixels) should disable hold-zoom potential
        if (moveDist > 5)
        {
            touchHoldZoomActive = 0;
            touchHoldStartTime = 0xFFFFFFFF; // Prevent hold-zoom from ever activating
        }

        if (moveDist > 15) // Movement threshold to start panning
        {
            if (!isPanning)
            {
                // Start panning
                isPanning = 1;
                lastPanX = x;
                lastPanY = y;
                SDL_Log("PANNING STARTED");
            }
        }

        if (isPanning)
        {
            // Single finger pan - use normalized delta, map to rendering space
            float dx = x - lastPanX;
            float dy = y - lastPanY;

            double scale = 4.0 / (renderWidth * zoom);
            center_x -= dx * renderWidth * scale;
            center_y -= dy * renderHeight * scale;

            lastPanX = x;
            lastPanY = y;
            finger1_x = x;
            finger1_y = y;
            needsRedraw = 1;
        }
    }
    else if (numFingers == 2)
    {
        // Update the correct finger position by ID
        if (id == finger1_id)
        {
            finger1_x = x;
            finger1_y = y;
        }
        else if (id == finger2_id)
        {
            finger2_x = x;
            finger2_y = y;
        }

        // Calculate new pinch distance and zoom
        float currentDist = getTouchDistance(finger1_x, finger1_y, finger2_x, finger2_y);
        if (initialPinchDist > 0)
        {
            double zoomFactor = currentDist / initialPinchDist;
            double newZoom = initialZoom * zoomFactor;

            // Clamp zoom to valid range
            if (newZoom < MIN_ZOOM)
                newZoom = MIN_ZOOM;
            if (newZoom > MAX_ZOOM)
                newZoom = MAX_ZOOM;
            zoom = newZoom;

            // Keep pinch center fixed (map to rendering space)
            float centerPixelX = (finger1_x + finger2_x) / 2.0f * renderWidth;
            float centerPixelY = (finger1_y + finger2_y) / 2.0f * renderHeight;
            double newScale = 4.0 / (renderWidth * zoom);
            center_x = pinchCenterX - (centerPixelX - renderWidth / 2.0) * newScale;
            center_y = pinchCenterY - (centerPixelY - renderHeight / 2.0) * newScale;

            needsRedraw = 1;
        }
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Disable mouse event synthesis from touch on mobile
    // This prevents double-handling of touch inputs
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    // Create window (resizable for orientation changes on mobile)
    window = SDL_CreateWindow(
        "Mandelbrot Explorer (SDL2)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        INITIAL_WIDTH,
        INITIAL_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Get actual window size (may differ on mobile/Android)
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Set initial render size to match window
    renderWidth = windowWidth;
    renderHeight = windowHeight;

    // Create texture for pixel buffer (ARGB8888 format)
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderWidth,
        renderHeight);
    if (!texture)
    {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Allocate pixel buffer
    pixels = (uint32_t *)malloc(renderWidth * renderHeight * sizeof(uint32_t));
    if (!pixels)
    {
        SDL_Log("Failed to allocate pixel buffer");
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Main loop
    int running = 1;
    SDL_Event event;

    while (running)
    {
        // Process events
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                running = 0;
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = 0;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                handleMouseButtonDown(&event.button);
                break;

            case SDL_MOUSEBUTTONUP:
                handleMouseButtonUp(&event.button);
                break;

            case SDL_MOUSEMOTION:
                handleMouseMotion(&event.motion);
                break;

            case SDL_MOUSEWHEEL:
            {
                int mouseX, mouseY;
                SDL_GetMouseState(&mouseX, &mouseY);
                handleMouseWheel(&event.wheel, mouseX, mouseY);
                break;
            }

            case SDL_FINGERDOWN:
                handleFingerDown(&event.tfinger);
                break;

            case SDL_FINGERUP:
                handleFingerUp(&event.tfinger);
                break;

            case SDL_FINGERMOTION:
                handleFingerMotion(&event.tfinger);
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

                    // Resize texture and pixel buffer to match new window size
                    if (windowWidth != renderWidth || windowHeight != renderHeight)
                    {
                        renderWidth = windowWidth;
                        renderHeight = windowHeight;

                        // Recreate texture at new size
                        SDL_DestroyTexture(texture);
                        texture = SDL_CreateTexture(
                            renderer,
                            SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING,
                            renderWidth,
                            renderHeight);

                        // Reallocate pixel buffer
                        free(pixels);
                        pixels = (uint32_t *)malloc(renderWidth * renderHeight * sizeof(uint32_t));

                        SDL_Log("RESIZE: %dx%d", renderWidth, renderHeight);
                    }
                    needsRedraw = 1;
                }
                break;
            }
        }

        // Update continuous zoom if button/finger is held
        updateMouseHoldZoom();
        updateTouchHoldZoom();

        // Render if needed
        if (needsRedraw)
        {
            renderMandelbrot();
            needsRedraw = 0;
        }

        // Present
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        // Small delay to prevent CPU spinning
        SDL_Delay(16);
    }

    // Cleanup
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
