#include <emscripten.h>

#include <string_view>
using namespace std;

EM_JS(void, js_console_log, (char const* data, std::size_t _size), {
    const buf = new Uint8Array(wasmMemory.buffer, data, _size);
    console.log(new TextDecoder("utf-8").decode(buf));
  });

struct console {
  static void log(std::string_view str) {
    js_console_log(str.data(), str.size());
  }
};



int main() {

  emscripten_async_run_script("console.log('Hello From run_script');", 2000);
  console::log("JS inside C++");

  return 0;
}

