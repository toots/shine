Shine: fast fixed-point mp3 encoding
====================================

[shine](https://github.com/savonet/shine) is a blazing fast mp3 encoding library implemented in 
fixed-point arithmetic. The library can thus be used to perform super fast mp3 encoding on architectures
without a FPU, such as `armel`, etc.. It is also super fast on architectures with a FPU!

How to use?
-----------

The encoding API should be quite straight forward:

```c
#include <shine/layer3.h>

shine_config_t config;

shine_set_config_mpeg_defaults(&config.mpeg);
config.wave.channels = 2;
config.wave.samplerate = 44100;
config.mpeg.mode = SHINE_JOINT_STEREO;
config.mpeg.bitrate = 128;

/* See if samplerate and bitrate are valid */
if (shine_check_config(config.wave.samplerate, config.mpeg.bitrate) < 0)
  error("Unsupported samplerate/bitrate configuration.");

/* Initiate encoder */
s = shine_initialise(&config);

/* Number of samples (per channel) to feed the encoder with. */
int samples_per_pass = shine_samples_per_pass(s);

/* All the magic happens here */
while (read(buffer, infile, samples_per_pass)) {
  data = shine_encode_buffer(s, buffer, &written);
  write(data, written);
}

/* Flush and write remaining data. */
data = shine_flush(s, &written);
write(data, written);

/* Close encoder. */
shine_close(s);
```

Formatting
----------

C sources follow `.clang-format` and JavaScript follows `js/.prettierrc`, with
the versions pinned in `.pre-commit-config.yaml`. CI checks them. To format on
each commit:

```bash
pre-commit install
```

`pre-commit run --all-files` formats the whole tree.

Testing
-------

```bash
make check
```

Each program in `tests/` checks one concern, usually over every supported
samplerate, bitrate and channel mode, using the encoding and frame parsing
helpers in `tests/helpers.h`. A regression fix should come with a test that
fails without the fix: add `tests/<name>.c` and list it in `TESTS` and a
`tests_<name>_SOURCES` line in `Makefile.am`.

`tests/level` checks the decoded output level with FFmpeg, and is skipped when
`ffmpeg` is not in the `PATH`.

`tests/golden` checks the hash of every encoded stream against
`tests/golden.txt`, so any change to the encoder's output fails. Changes meant
to alter the output regenerate the file, and say why in the commit message:

```bash
SHINE_UPDATE_GOLDEN=1 make check TESTS=tests/golden
```

CI also runs the tests under AddressSanitizer and UndefinedBehaviorSanitizer.
To do the same locally:

```bash
./configure CC=clang \
  CFLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all" \
  LDFLAGS="-fsanitize=address,undefined"
make check
```

`make check-wasm` tests the WebAssembly build of the JavaScript package against
the native encoder, when `configure` found a WebAssembly toolchain: see
[js/README.md](js/README.md).

How fast is it?
---------------

On an Apple M5 (`arm64`, November 2026), encoding 3 minutes of music: median
of 7 runs (5 for LAME), in multiples of realtime.

| | 44.1 kHz stereo 128 kbps (joint) | 44.1 kHz stereo 320 kbps | 22.05 kHz mono 64 kbps |
|---|---|---|---|
| `shineenc` | 445x | 382x | 1360x |
| WebAssembly (Node 26) | 201x | 184x | 730x |
| LAME 4.0 `-q 7` | 267x | 309x | 1014x |
| LAME 4.0 `-q 2` | 68x | 81x | 192x |

`shineenc` is built with `CFLAGS=-O2`. LAME runs with `-b <bitrate>` and
`-m j`, `-m s` or `-m m`: `-q 2` is its default quality, `-q 7` its fastest
setting that still uses a psychoacoustic model, which shine does not have.

Limitations
-----------

The code for the encoder has been written a long time ago (see below) and 
the only work done on this fork consists of reorganizing the code and making a 
proper shared API out of it. Thus, the encoder may not be exempt of bugs.

Also, the encoding algorithm is rather simple. In particular, it does not
have any Psychoacoustic Model.

A bit of history
----------------

This code was dug out from the dusty crates of those times before internet 
and github. It apparently was created by Gabriel Bouvigne sometime around 
the end of the 20th century. The encoder was converted circa 2001 by Pete 
Everett to fixed-point arithmetic for the RISC OS. Last we know, Patrick 
Roberts had worked on the code to make it multi-platform and more library
oriented. That was around 2006.

You can consult `README.old` and the various source files for more 
informations on this code.
