# LLVM/Clang Plugins

Clang plugins that extract declarations from C++ translation units using libtooling.

## Plugins

### `decls`

Prints the terse declaration of a named symbol.

```sh
clang -cc1 -load ./libdecls.so -plugin decls \
  -plugin-arg-decls <symbol-name> file.cpp
```

### `print-symbols`

Prints the full declaration of a symbol, and for classes/structs also prints the out-of-line member function definitions.

```sh
clang -cc1 -load ./libprint-symbols-plugin.so -plugin print-symbols \
  -plugin-arg-print-symbols symbol=<symbol-name> file.cpp
```

Accepts either `symbol=Foo` or just `Foo` as the argument.

## Building

Requires matching Clang/LLVM headers and libraries. Example with CMake:

```sh
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/usr/lib/llvm
make
```

Or compile directly:

```sh
clang++ -shared -fPIC -o libdecls.so decls.cpp \
  $(llvm-config --cxxflags) -lclang-cpp
```

## Notes

- Use `clang -cc1` because the plugin interface is part of clang's frontend API.
- The plugin `.so` must be built against the **same** Clang/LLVM version as the clang binary you invoke.
