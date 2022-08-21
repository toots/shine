Shine encoder library for Javascript
====================================

This directory contains a build of shine for Javascript using 
[kripken/emscripten](https://github.com/kripken/emscripten) and
located in `dist/libshine.js`

How to use?
-----------

The encoding API should be quite straight forward:

```js
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

Results, as of August 20, 2022:

Chrome (`104.0.5112.101`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.35 seconds
Encoding rate: 16.11X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.21
Encoding rate: 22.42X
```

Firefox (`103.0.2`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.36 seconds
Encoding rate: 15.31X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.24
Encoding rate: 19.50X
```

Safari (`15.3`):
```
Executing encoding test
Got WAV file.
Encoding..
Done encoding.
File duration: 5.57 seconds
Encoding time: 0.69 seconds
Encoding rate: 8.09X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.41
Encoding rate: 11.32X
```

NodeJS (`v14.19.1`):
```
File duration: 5.57 seconds
Encoding time: 0.53 seconds
Encoding rate: 10.44X

Executing rounds test
Encoding 50 buffers of 4096 samples
Done encoding
Total duration: 4.64
Encoding time: 0.38
Encoding rate: 12.34X
```
