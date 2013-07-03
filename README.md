Revived shine fixed-point mp3 encoder
=====================================

[savonet/shine](https://github.com/savonet/shine) is a library for encoding mp3 data which
is implemented in fixed-point arithmetic. The library can thus be used to implement super fast
mp3 encoding on architectures without a FPU, such as `armel`, etc.. In fact, it is also super
fast on architectures with a FPU!

How to use?
-----------

The encoding API should be quite straight forward:

```
#include <shine/layer3.h>
  
(...)

/* See if samplerate and bitrate are valid */
if (shine_check_config(config.wave.samplerate, config.mpeg.bitr) < 0)
  error("Unsupported samplerate/bitrate configuration.");

/* Initiate encoder */
s = shine_initialise(&config);

/* Number of samples (per channel) to feed the encoder with. */
int samples_per_pass = shine_samples_per_pass(s);

/* All the magic happens here */
while (read(buffer, infile, samples_per_pass)) {
  data = shine_encode_buffer(s,buffer,&written);
  write(data, written);
}

/* Flush and write remaining data. */
data = shine_flush(s,&written);
write(written, data);

/* Close encoder. */
shine_close(s);
```

How fast is it?
---------------

On a [Raspberry Pi](http://www.raspberrypi.org/) (`ARM`, no `FPU`):

Lame, `8m53s`:
```
pi@raspbmc:/tmp$ lame /tmp/bla.wav /tmp/bla.mp3 
LAME 3.98.4 32bits (http://www.mp3dev.org/)
Using polyphase lowpass filter, transition band: 16538 Hz - 17071 Hz
Encoding /tmp/bla.wav to /tmp/bla.mp3
Encoding as 44.1 kHz j-stereo MPEG-1 Layer III (11x) 128 kbps qval=3
    Frame          |  CPU time/estim | REAL time/estim | play/CPU |    ETA 
  5763/5764  (100%)|    8:44/    8:44|    8:53/    8:53|   0.2871x|    0:00 
----------------------------------------------------------------------------
   kbps        LR    MS  %     long switch short %
  128.0       47.8  52.2        98.1   1.2   0.7
Writing LAME Tag...done
ReplayGain: -4.9d
```

Shine, `47s`:
```
pi@raspbmc:/tmp$ shineenc /tmp/bla.wav /tmp/bla.mp3 
shineenc (Liquidsoap version)
WAV PCM DATA, stereo 44100Hz 16bit, Length:  0: 2:30
MPEG-I layer III, stereo  Psychoacoustic Model: Shine
Bitrate=128 kbps  De-emphasis: none   Original 
Encoding "/tmp/bla.wav" to "/tmp/bla.mp3"
 Finished in  0: 0:47
```

The difference is quite remarkable...!

Now, on a mac airbook (`x86_64`, `FPU`):

Lame, `9s`:
```
toots@zulu /tmp  % lame /tmp/bla.wav /tmp/bla.mp3
LAME 3.99.5 64bits (http://lame.sf.net)
Using polyphase lowpass filter, transition band: 16538 Hz - 17071 Hz
Encoding /tmp/bla.wav to /tmp/bla.mp3
Encoding as 44.1 kHz j-stereo MPEG-1 Layer III (11x) 128 kbps qval=3
    Frame          |  CPU time/estim | REAL time/estim | play/CPU |    ETA 
  5763/5763  (100%)|    0:07/    0:07|    0:09/    0:09|   18.924x|    0:00 
----------------------------------------------------------------------------
   kbps        LR    MS  %     long switch short %
  128.0       55.5  44.5        97.6   1.4   1.0
Writing LAME Tag...done
ReplayGain: -4.9dB
```

Shine, `5s`:
```
toots@zulu /tmp  % shineenc /tmp/bla.wav /tmp/bla.mp3
shineenc (Liquidsoap version)
WAV PCM DATA, stereo 44100Hz 16bit, Length:  0: 2:30
MPEG-I layer III, stereo  Psychoacoustic Model: Shine
Bitrate=128 kbps  De-emphasis: none   Original 
Encoding "/tmp/bla.wav" to "/tmp/bla.mp3"
 Finished in  0: 0: 5
```

Not so bad eh!

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
Everett to fixed-point arithmetic for the RISC OS. Latest we know, Patrick 
Roberts had worked on the code to make it multi-platform and more library
oriented. That was around 2006.

You can consult `README.old` and the various source files for more 
informations on this code.
