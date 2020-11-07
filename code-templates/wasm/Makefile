all:
	emcc main.cpp -std=c++20 -s WASM=1 -O0 --post-js public/post.js -o public/main.html
	if [ -f .pid ]; then kill $$(cat .pid); fi
	npm run start &
