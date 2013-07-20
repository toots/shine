Shine encoder library for Javascript
====================================

This directory contains a build of shine for Javascript using 
[kripken/emscripten](https://github.com/kripken/emscripten) and
located in `dist/libshine.js`

How to use?
-----------

The encoding API should be quite straight forward:

```
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

You can run the test suite located in `test/`. As of now (19/07/2013), only firefox
seems to show performances decent enough for real-time encoding. However, if Chrome/V8 
was to support [asm.js](http:asmjs.org), performances would probably greatly improve.

Chrome:
```
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 3.67
Encoding rate: 1.27X
```

Firefox:
```
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 1.07
Encoding rate: 4.34X
```

Safari:
```
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 5.38
Encoding rate: 0.86X
```

NodeJS:
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 12.27 seconds
Encoding rate: 0.45X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 6.82
Encoding rate: 0.68X
```
