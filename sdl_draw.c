/*
 * Mandelbrot Set Explorer - SDL2 Version
 * Converted from Windows GDI to SDL2
 *
 * Desktop Controls:
 *   Mouse Wheel: Smooth zoom in/out (1.15x factor)
 *   Left Click:  Zoom in 1.5x centered on cursor
 *   Right Click: Zoom out 1.5x centered on cursor
 *   ESC:         Exit application
 *
 * Touch Controls (Android/Mobile):
 *   Single Finger Drag: Pan the view
 *   Two Finger Pinch:   Zoom in/out
 *   Single Tap:         Zoom in 1.5x centered on tap
 *   Double Tap:         Zoom out 1.5x centered on tap
 */

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define WIDTH 800
#define HEIGHT 600

// Mandelbrot parameters
static double zoom = 1.0;
static double center_x = -0.5;
static double center_y = 0.0;
static int max_iter = 256;

// Zoom limits to prevent issues
#define MIN_ZOOM 0.1
#define MAX_ZOOM 1e14

// SDL globals
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint32_t *pixels = NULL;
static int needsRedraw = 1;
static int windowWidth = WIDTH;
static int windowHeight = HEIGHT;

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
    double scale = 4.0 / (WIDTH * zoom);

    SDL_Log("RENDER: center=(%.6f, %.6f) zoom=%.2f scale=%.10f", center_x, center_y, zoom, scale);

    // Sanity check - ensure we're not rendering garbage
    if (zoom <= 0 || !isfinite(center_x) || !isfinite(center_y) || !isfinite(zoom))
    {
        SDL_Log("ERROR: Invalid render parameters! Resetting to defaults.");
        center_x = -0.5;
        center_y = 0.0;
        zoom = 1.0;
        scale = 4.0 / (WIDTH * zoom);
    }

    for (int py = 0; py < HEIGHT; py++)
    {
        double ci = center_y + (py - HEIGHT / 2.0) * scale;
        for (int px = 0; px < WIDTH; px++)
        {
            double cr = center_x + (px - WIDTH / 2.0) * scale;
            int iter = mandelbrot(cr, ci);
            pixels[py * WIDTH + px] = getColor(iter);
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
                if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT)
                    pixels[py * WIDTH + px] = color;
                px = debugTapX;
                py = debugTapY + i;
                if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT)
                    pixels[py * WIDTH + px] = color;
            }
        }
    }

    // Update texture with new pixel data
    SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(uint32_t));
}

// Handle mouse button events for zooming
static void handleMouseButton(SDL_MouseButtonEvent *event)
{
    double scale = 4.0 / (WIDTH * zoom);
    double worldX = center_x + (event->x - WIDTH / 2.0) * scale;
    double worldY = center_y + (event->y - HEIGHT / 2.0) * scale;

    if (event->button == SDL_BUTTON_LEFT)
    {
        // Zoom in
        zoom *= 1.5;
        // Recalculate center to keep clicked point under cursor
        double newScale = 4.0 / (WIDTH * zoom);
        center_x = worldX - (event->x - WIDTH / 2.0) * newScale;
        center_y = worldY - (event->y - HEIGHT / 2.0) * newScale;
        needsRedraw = 1;
    }
    else if (event->button == SDL_BUTTON_RIGHT)
    {
        // Zoom out
        zoom /= 1.5;
        // Recalculate center to keep clicked point under cursor
        double newScale = 4.0 / (WIDTH * zoom);
        center_x = worldX - (event->x - WIDTH / 2.0) * newScale;
        center_y = worldY - (event->y - HEIGHT / 2.0) * newScale;
        needsRedraw = 1;
    }
}

// Handle mouse wheel events for smooth zooming
static void handleMouseWheel(SDL_MouseWheelEvent *event, int mouseX, int mouseY)
{
    double scale = 4.0 / (WIDTH * zoom);
    double worldX = center_x + (mouseX - WIDTH / 2.0) * scale;
    double worldY = center_y + (mouseY - HEIGHT / 2.0) * scale;

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
    double newScale = 4.0 / (WIDTH * zoom);
    center_x = worldX - (mouseX - WIDTH / 2.0) * newScale;
    center_y = worldY - (mouseY - HEIGHT / 2.0) * newScale;

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
        isPanning = 1;
        lastPanX = x;
        lastPanY = y;
        initialTapX = x; // Store initial position for tap detection
        initialTapY = y;
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
        float centerPixelX = (finger1_x + finger2_x) / 2.0f * WIDTH;
        float centerPixelY = (finger1_y + finger2_y) / 2.0f * HEIGHT;
        double scale = 4.0 / (WIDTH * zoom);
        pinchCenterX = center_x + (centerPixelX - WIDTH / 2.0) * scale;
        pinchCenterY = center_y + (centerPixelY - HEIGHT / 2.0) * scale;
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

    if (numFingers == 1 && isPanning && id == finger1_id)
    {
        // Check for tap (minimal movement from initial touch position)
        float dx = fabsf(ex - initialTapX) * windowWidth;
        float dy = fabsf(ey - initialTapY) * windowHeight;

        SDL_Log("TAP CHECK: dx=%.1f dy=%.1f (threshold=20)", dx, dy);

        if (dx < 20 && dy < 20)
        {
            Uint32 currentTime = SDL_GetTicks();

            // SDL stretches texture to fill window, so simple linear mapping works
            // Normalized touch coords [0,1] map directly to render coords [0,WIDTH/HEIGHT]
            float tapX = ex * WIDTH;
            float tapY = ey * HEIGHT;

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
                // Single tap: zoom in centered on tap location
                double scale = 4.0 / (WIDTH * zoom);
                double worldX = center_x + (tapX - WIDTH / 2.0) * scale;
                double worldY = center_y + (tapY - HEIGHT / 2.0) * scale;

                double newZoom = zoom * 1.5;
                if (newZoom <= MAX_ZOOM)
                {
                    zoom = newZoom;
                    center_x = worldX;
                    center_y = worldY;
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

    if (numFingers == 1 && isPanning && id == finger1_id)
    {
        // Single finger pan - use normalized delta, map to rendering space
        float dx = x - lastPanX;
        float dy = y - lastPanY;

        double scale = 4.0 / (WIDTH * zoom);
        center_x -= dx * WIDTH * scale;
        center_y -= dy * HEIGHT * scale;

        lastPanX = x;
        lastPanY = y;
        finger1_x = x;
        finger1_y = y;
        needsRedraw = 1;
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
            float centerPixelX = (finger1_x + finger2_x) / 2.0f * WIDTH;
            float centerPixelY = (finger1_y + finger2_y) / 2.0f * HEIGHT;
            double newScale = 4.0 / (WIDTH * zoom);
            center_x = pinchCenterX - (centerPixelX - WIDTH / 2.0) * newScale;
            center_y = pinchCenterY - (centerPixelY - HEIGHT / 2.0) * newScale;

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
        WIDTH,
        HEIGHT,
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

    // Create texture for pixel buffer (ARGB8888 format)
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        WIDTH,
        HEIGHT);
    if (!texture)
    {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Allocate pixel buffer
    pixels = (uint32_t *)malloc(WIDTH * HEIGHT * sizeof(uint32_t));
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
                handleMouseButton(&event.button);
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
                    needsRedraw = 1;
                }
                break;
            }
        }

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
