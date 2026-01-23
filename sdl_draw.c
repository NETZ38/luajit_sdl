/*
 * Mandelbrot Set Explorer - SDL2 Version
 * Converted from Windows GDI to SDL2
 *
 * Controls:
 *   Mouse Wheel: Smooth zoom in/out (1.15x factor)
 *   Left Click:  Zoom in 1.5x centered on cursor
 *   Right Click: Zoom out 1.5x centered on cursor
 *   ESC:         Exit application
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

// SDL globals
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint32_t *pixels = NULL;
static int needsRedraw = 1;

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

    // Update texture with new pixel data
    SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(uint32_t));
}

// Handle mouse button events for zooming
static void handleMouseButton(SDL_MouseButtonEvent *event)
{
    double scale = 4.0 / (WIDTH * zoom);
    double mouseX = center_x + (event->x - WIDTH / 2.0) * scale;
    double mouseY = center_y + (event->y - HEIGHT / 2.0) * scale;

    if (event->button == SDL_BUTTON_LEFT)
    {
        // Zoom in
        zoom *= 1.5;
        center_x = mouseX;
        center_y = mouseY;
        needsRedraw = 1;
    }
    else if (event->button == SDL_BUTTON_RIGHT)
    {
        // Zoom out
        zoom /= 1.5;
        center_x = mouseX;
        center_y = mouseY;
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

    // Create window
    window = SDL_CreateWindow(
        "Mandelbrot Explorer (SDL2)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH,
        HEIGHT,
        SDL_WINDOW_SHOWN);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

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
