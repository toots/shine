import Shine from "@toots/shine.js";
import { Reader } from "wav";
import fs from "fs";

const convertInterleavedBuffer = (buf, channels, samples) => {
  const ret = new Array(channels);
  for (let chan = 0; chan < channels; chan++) {
    ret[chan] = new Int16Array(samples);
    for (let i = 0; i < samples; i++) {
      ret[chan][i] = buf.readInt16LE(2 * (i * channels + chan));
    }
  }

  return ret;
};

const exec = async () => {
  await Shine.initialized;

  console.log("");
  console.log("Executing encoding test");

  const bitrate = 128;
  const str = fs.createReadStream("../lib/encode.wav");
  const fd = fs.openSync("./encode.mp3", "w");
  const reader = new Reader();

  str.pipe(reader);

  const write = encoded => {
    if (encoded.length <= 0) return;

    const buf = Buffer.from(encoded);
    fs.writeSync(fd, buf, 0, buf.length);
  };

  reader.on("format", format => {
    console.log("Got WAV file.");

    const shine = new Shine({
      bitrate: bitrate,
      samplerate: format.sampleRate,
      channels: format.channels,
    });

    console.log("Encoding..");
    const started = new Date();
    let duration = 0.0;
    const samplerate = parseFloat(format.sampleRate);

    reader.on("data", buf => {
      const samples = buf.length / (2 * format.channels);

      duration += samples / samplerate;

      write(shine.encode(convertInterleavedBuffer(buf, format.channels, samples)));
    });

    reader.on("end", () => {
      write(shine.close());

      const ended = new Date();
      const encodingTime = (ended.getTime() - started.getTime()) / 1000;
      console.log("Done encoding.");
      console.log(`File duration: ${duration.toFixed(2)} seconds`);
      console.log(`Encoding time: ${encodingTime.toFixed(2)} seconds`);
      console.log(`Encoding rate: ${(duration / encodingTime).toFixed(2)}X`);
      process.exit(0);
    });
  });
};

exec();
