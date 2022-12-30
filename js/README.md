Shine encoder library for Javascript
====================================

This directory contains a build of shine for Javascript using 
[kripken/emscripten](https://github.com/kripken/emscripten) and
located in `dist/libshine.js`

How to use?
-----------

The encoding API should be quite straight forward:

```js
await Shine.initialized;

shine = new Shine({
  samplerate: 44100,
  bitrate: 128,
  channels: 2,
  mode: Shine.STEREO
});
  
// All the magic happens here
while (..) {
  // data here is an array of channels.
  // Channels must have the same number of samples
  // and both be either Int16Array or Float32Array.
  encoded = shine.encode(data);
  ...
}

// Close and get remaining data.
flushed = shine.close();
...
```

How fast is it?
---------------

You can run the test suite located in `test/`. Encoding rate above `1X` means that 
the browser should be suitable for real-time encoding.

Results, as of December 30, 2022:

Chrome (`108.0.5359.124`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.08 seconds
Encoding rate: 67.96X
sa
Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.03
Encoding rate: 160.00X
```

Firefox (`108.0.1`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.06 seconds
Encoding rate: 99.52X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.03
Encoding rate: 178.46X
```

Safari (`16.2`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.12 seconds
Encoding rate: 46.44X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.02
Encoding rate: 210.91X
```

NodeJS (`v19.3.0`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.06 seconds
Encoding rate: 94.45X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.03
Encoding rate: 178.46X
```
