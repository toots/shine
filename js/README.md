Shine encoder library for Javascript
====================================

This package contains a build of the [shine](https://github.com/toots/shine) fixed-point
mp3 encoder compiled to WebAssembly with clang. The module is embedded in the package,
so it works as is in Node.js (16 and later), browsers and bundlers. It is an ES module.

Install
-------

Using `npm`:

```shell
npm install @toots/shine.js
```

Using `yarn`:

```shell
yarn add @toots/shine.js
```

Using `pnpm`:

```shell
pnpm install @toots/shine.js
```

In a HTML page, import it as a module:

```html
<script type="module">
  import { Shine } from "./dist/shine.mjs";
</script>
```

See: [test/browser](https://github.com/toots/shine/tree/main/js/test/browser) for an example.

How to use?
-----------

The encoding API should be quite straight forward:

```js
import { Shine, StereoMode } from "@toots/shine.js";

await Shine.initialized;

const shine = new Shine({
  samplerate: 44100,
  bitrate: 128,
  channels: 2,
  stereoMode: StereoMode.STEREO,
});

// All the magic happens here
while (..) {
  // data here is an array of channels.
  // Channels must have the same number of samples
  // and both be either Int16Array or Float32Array.
  const encoded = shine.encode(data);
  ...
}

// Encode the remaining samples and flush the encoder.
const flushed = shine.close();
```

`stereoMode` defaults to `MONO` for one channel and `JOINT_STEREO` for two.

Building
--------

The package is built from the top level of the repository, with a clang that
can target `wasm32`, `wasm-ld` and a [wasi-libc](https://github.com/WebAssembly/wasi-libc)
sysroot, e.g. `clang lld wasi-libc` on Debian and Ubuntu, or `llvm lld wasi-libc`
with Homebrew:

```shell
./bootstrap
./configure WASM_CC=/opt/homebrew/opt/llvm/bin/clang   # when clang is not the right one
make wasm         # js/dist
make check-wasm   # compares the output with the native shineenc
```

`configure` looks for the sysroot in the usual places; set `WASI_SYSROOT`
otherwise. `--enable-wasm` makes a missing toolchain an error.

How fast is it?
---------------

On a macbook pro (`arm64`/M1 pro), Node.js 26, encoding 285 s of stereo music
at 128 kbps: about 205x realtime, against about 310x for the native `shineenc`.

`test/browser` measures the encoding rate in a browser: run `make` there and
open `http://localhost:8000/test/browser/`. A rate above `1X` means that the
browser should be suitable for real-time encoding.
