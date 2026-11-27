Shine: fast fixed-point mp3 encoding
====================================

[shine](https://github.com/savonet/shine) is a blazing fast mp3 encoding library implemented in 
fixed-point arithmetic. The library can thus be used to perform super fast mp3 encoding on architectures
without a FPU, such as `armel`, etc.. It is also super fast on architectures with a FPU!

It has two quality levels. Level 0 is the original encoder: no psychoacoustic
model, and the fastest. Level 1 adds a psychoacoustic model, noise shaping,
short blocks, mid/side stereo, a lowpass and, at low bitrates, a lower
samplerate. It is still faster than LAME at its default settings. The model
is specified in [doc/psy](doc/psy/README.md).

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
config.mpeg.quality = 1; /* 0, the default, runs no psychoacoustic model */

/* See if samplerate and bitrate are valid */
if (shine_check_config(config.wave.samplerate, config.mpeg.bitrate) < 0)
  error("Unsupported samplerate/bitrate configuration.");

/* Initiate encoder */
s = shine_initialise(&config);

/* Number of samples (per channel) to feed the encoder with. */
int samples_per_pass = shine_samples_per_pass(s);

/* The samplerate of the stream: above quality 0, lower than the input's at
 * low bitrates. A call can then return no frame, or several. */
int stream_samplerate = shine_samplerate(s);

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
of 5 runs, in multiples of realtime, one thread.

| | 44.1 kHz stereo 128 kbps | 44.1 kHz stereo 192 kbps | 44.1 kHz stereo 320 kbps | 44.1 kHz mono 64 kbps |
|---|---|---|---|---|
| `shineenc -Q 0` | 900x | 818x | 720x | 1800x |
| `shineenc -Q 1` | 234x | 234x | 247x | 581x |
| WebAssembly, quality 0 (Node 26) | 479x | 434x | 393x | 930x |
| WebAssembly, quality 1 (Node 26) | 150x | 148x | 156x | 359x |
| LAME 4.0 `-q 7` | 310x | 300x | 290x | 643x |
| LAME 4.0, default | 154x | 158x | 146x | 286x |
| FDK AAC 2.0, AAC-LC | 240x | 222x | 200x | 474x |

Peak memory: 1.5 MB at level 0, 1.8 MB at level 1, 2.1 MB for FDK AAC, 2.7 MB
for LAME. FDK AAC runs through its library API with the afterburner on, as in
FFmpeg's `libfdk_aac`; at 64 kbps stereo it runs at 281x in AAC-LC, as level
1 does, and at 205x in HE-AAC.
`shineenc` is built with `CFLAGS=-O2`; it and LAME, run with `-b <bitrate>`,
code stereo as joint stereo, their default. Its default quality is `-q 3`; `-q 7` is its fastest
setting that still uses a psychoacoustic model.

On x86-64 the filter bank is also built for AVX2 and chosen at run time. On
an Intel Xeon Platinum 8488C, at 44.1 kHz stereo 128 kbps, level 1 ran at
88x realtime built with clang 18 and 67x with GCC 13, against 66x for LAME,
before the filter bank and MDCT got their fast transforms: it is not
measured again since.

The WebAssembly build is measured through its JavaScript API: see
[js/README.md](js/README.md), which also measures it in a browser.

How good is it?
---------------

The mean of an estimate of the MUSHRA listening-test score, from 0 to 100,
over 53 music and speech items, with the 2f-model of PEAQ
([doc/psy/tests.md](doc/psy/tests.md) §4). It is a measure, not a listening
test: differences under 5 points on one item say little.

| | 44.1 kHz stereo 64 kbps | 96 kbps | 128 kbps | 192 kbps | 44.1 kHz mono 64 kbps |
|---|---|---|---|---|---|
| `shineenc -Q 0` | 39.9 | 56.0 | 70.9 | 92.2 | 72.8 |
| `shineenc -Q 1` | 60.7 | 78.1 | 90.1 | 100.0 | 89.1 |
| LAME 4.0, default | 56.6 | 77.5 | 90.2 | 99.9 | 88.4 |
| FDK AAC 2.0, AAC-LC | 67.2 | 82.0 | 93.9 | 99.9 | 87.0 |
| FDK AAC 2.0, HE-AAC | 73.7 | | | | |
| FFmpeg 9.0 `aac` | 56.8 | 72.3 | 82.9 | 95.6 | 74.3 |

From 192 kbps in stereo and 128 kbps in mono, every MP3 encoder scores within
a point of 100.

AAC is the newer format: in stereo FDK AAC scores 4 to 6 points above level 1
up to 128 kbps, and the two are level from 160 kbps (99.1 against 98.9). At
64 kbps HE-AAC scores 13 points above it; PEAQ predates the spectral band
replication HE-AAC is built on, and that score is the least certain of the
table. Level 1 scores above FFmpeg's own AAC encoder at every bitrate, and
above FDK AAC in mono at 64 kbps.

Limitations
-----------

Level 1 scores level with or above LAME at its default settings at 32, 44.1
and 48 kHz, from 32 to 320 kbps, and runs faster. It is as close to the MP3
ceiling as we have been able to get, and narrows the gap to AAC with
standard MP3 streams: level with FDK AAC-LC from 160 kbps and ahead of it in
mono at 64 kbps. The score is a model: the choices it led to
that listeners have not confirmed yet, the bandwidth of the lowest
bitrates first, are in [TODO.md](TODO.md) with the work under way. Level 0 output is frozen: it
only changes to fix a bug.

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
