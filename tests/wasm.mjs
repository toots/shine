// The WebAssembly build through its JavaScript API: run by `make check-wasm`.
// Each case encodes the same samples with the Shine class and with the native
// shineenc, and requires identical streams.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";

const { Shine, StereoMode } = await import(
  pathToFileURL(process.env.SHINE_JS).href
);
await Shine.initialized;

const SAMPLES = 12345; // per channel, not a whole number of passes

// Loud noise over a slow square wave, from a fixed seed.
const generate = (channels) => {
  const data = [];
  let seed = 1;
  for (let channel = 0; channel < channels; channel++) {
    const samples = new Int16Array(SAMPLES);
    for (let i = 0; i < SAMPLES; i++) {
      seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
      samples[i] = ((seed >>> 16) & 0x3fff) - 0x2000 + (i % 200 < 100 ? 12000 : -12000);
    }
    data.push(samples);
  }
  return data;
};

const writeWav = (file, samplerate, data) => {
  const channels = data.length;
  const wav = Buffer.alloc(44 + SAMPLES * channels * 2);
  wav.write("RIFF", 0);
  wav.writeUInt32LE(36 + SAMPLES * channels * 2, 4);
  wav.write("WAVEfmt ", 8);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(channels, 22);
  wav.writeUInt32LE(samplerate, 24);
  wav.writeUInt32LE(samplerate * channels * 2, 28);
  wav.writeUInt16LE(channels * 2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write("data", 36);
  wav.writeUInt32LE(SAMPLES * channels * 2, 40);
  for (let i = 0; i < SAMPLES; i++)
    for (let channel = 0; channel < channels; channel++)
      wav.writeInt16LE(data[channel][i], 44 + 2 * (i * channels + channel));
  fs.writeFileSync(file, wav);
};

// Uneven chunks, to exercise the buffering across encoder passes.
const encodeWithShine = (config, data) => {
  const shine = new Shine(config);
  const chunks = [];
  for (let offset = 0, size = 1; offset < SAMPLES; offset += size, size = (size * 7 + 3) % 3000 + 1)
    chunks.push(shine.encode(data.map((samples) => samples.subarray(offset, offset + size))));
  chunks.push(shine.close());
  return Buffer.concat(chunks);
};

const cases = [
  { samplerate: 44100, bitrate: 128, channels: 2, stereoMode: StereoMode.STEREO, options: [] },
  { samplerate: 48000, bitrate: 320, channels: 2, stereoMode: StereoMode.JOINT_STEREO, options: ["-j"] },
  { samplerate: 32000, bitrate: 64, channels: 2, stereoMode: StereoMode.DUAL_CHANNEL, options: ["-d"] },
  { samplerate: 22050, bitrate: 64, channels: 1, stereoMode: StereoMode.MONO, options: [] },
  { samplerate: 8000, bitrate: 16, channels: 1, stereoMode: StereoMode.MONO, options: [] },
];

const directory = fs.mkdtempSync(path.join(os.tmpdir(), "shine-wasm-"));
let passed = 0;

for (const { options, ...config } of cases) {
  const name = `${config.samplerate} Hz ${config.bitrate} kbps mode ${config.stereoMode}`;
  const data = generate(config.channels);
  const wav = path.join(directory, "input.wav");
  const mp3 = path.join(directory, "native.mp3");

  writeWav(wav, config.samplerate, data);
  execFileSync(process.env.SHINEENC, ["-q", "-b", String(config.bitrate), ...options, wav, mp3]);
  const native = fs.readFileSync(mp3);
  const encoded = encodeWithShine(config, data);

  if (native.length > 0 && encoded.equals(native)) passed++;
  else
    console.log(`FAIL ${name}: ${encoded.length} bytes, native shineenc ${native.length} bytes${
      encoded.length === native.length ? ", contents differ" : ""}`);
}

// Float input goes through the same path once converted.
const float = new Shine({ samplerate: 44100, bitrate: 128, channels: 1 });
const floatEncoded = Buffer.concat([float.encode([new Float32Array(SAMPLES).fill(0.25)]), float.close()]);
const floatOk = floatEncoded.length > 0;
if (!floatOk) console.log("FAIL Float32Array input: no output");

// Channels of different lengths are rejected.
const uneven = new Shine({ samplerate: 44100, bitrate: 128, channels: 2 });
let unevenOk = false;
try {
  uneven.encode([new Int16Array(4000), new Int16Array(3000)]);
} catch {
  unevenOk = true;
}
uneven.close();
if (!unevenOk) console.log("FAIL channels of different lengths: accepted");

fs.rmSync(directory, { recursive: true });

console.log(`${passed}/${cases.length} cases passed`);
process.exit(passed === cases.length && floatOk && unevenOk ? 0 : 1);
