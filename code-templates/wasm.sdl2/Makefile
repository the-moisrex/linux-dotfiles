wasm:
	emcc main.cpp -std=c++20 -s WASM=1 -s USE_SDL=2 -s ALLOW_MEMORY_GROWTH=1 -g -O0 --post-js public/post.js -o public/main.html
	if [ -f .pid ]; then kill $$(cat .pid); fi
	npm run start &

normal:
	g++ main.cpp -std=c++20 -g `sdl2-config --libs --cflags`

all: wasm
