all:
	emcc main.cpp -s WASM=1 -o public/main.html
	if [ -f .pid ]; then kill $$(cat .pid); fi
	npm run start &
