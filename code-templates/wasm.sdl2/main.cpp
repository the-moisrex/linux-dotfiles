#ifdef WASM
#  include <emscripten.h>
#endif

#include <SDL2/SDL.h>
#include <complex>

int main() {
  // Init SDL.
  int width = 600, height = 600;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_CreateWindowAndRenderer(width, height, SDL_WINDOW_OPENGL, &window,
                              &renderer);

  // Generate a palette with random colors.
  enum { MAX_ITER_COUNT = 256 };
  SDL_Color palette[MAX_ITER_COUNT];
  srand(time(0));
  for (int i = 0; i < MAX_ITER_COUNT; ++i) {
    palette[i] = {
        .r = (uint8_t)rand(),
        .g = (uint8_t)rand(),
        .b = (uint8_t)rand(),
        .a = 255,
    };
  }

  // Calculate and draw the Mandelbrot set.
  std::complex<double> center(0.5, 0.5);
  double scale = 4.0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      std::complex<double> point((double)x / width, (double)y / height);
      std::complex<double> c = (point - center) * scale;
      std::complex<double> z(0, 0);
      int i = 0;
      for (; i < MAX_ITER_COUNT - 1; i++) {
        z = z * z + c;
        if (abs(z) > 2.0)
          break;
      }
      SDL_Color color = palette[i];
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      SDL_RenderDrawPoint(renderer, x, y);
    }
  }

  // Render everything we've drawn to the canvas.
  SDL_RenderPresent(renderer);

  // SDL_Quit();
}

