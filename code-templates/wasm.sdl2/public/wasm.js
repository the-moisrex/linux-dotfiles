
const file = "hello.wasm";

const imports = {
    env: {
        console_log: console.log
    }
};

WebAssembly.instantiateStreaming(fetch(file), imports)
    .then( wasm => {
        window.wasm = wasm; // make it publicly available
        console.log("WASM Ready");
    }).catch( err => {
        console.error(err);
    });
