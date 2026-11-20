// Shine fixed-point MP3 encoder, compiled to WebAssembly.

import wasmBase64 from "./shine_wasm.mjs";

export const StereoMode = Object.freeze({
  STEREO: 0,
  JOINT_STEREO: 1,
  DUAL_CHANNEL: 2,
  MONO: 3,
});

const INT16 = 2;
const POINTER = 4;

let wasm;

const decodeBase64 = text => Uint8Array.from(atob(text), character => character.charCodeAt(0));

const initialized = WebAssembly.instantiate(decodeBase64(wasmBase64), {}).then(({ instance }) => {
  wasm = instance.exports;
});

// malloc can grow the memory, which detaches previous views of it: create
// views at each use.
const memory = () => wasm.memory.buffer;

const allocate = size => {
  const pointer = wasm.malloc(size);
  if (!pointer) throw new Error("Out of memory");
  return pointer;
};

const clip = x => (x > 1 ? 1 : x < -1 ? -1 : x);

const toInt16 = samples => {
  if (samples instanceof Int16Array) return samples;
  const converted = new Int16Array(samples.length);
  for (let i = 0; i < samples.length; i++) converted[i] = Math.trunc(clip(samples[i]) * 32767);
  return converted;
};

const concat = (Type, a, b) => {
  const joined = new Type(a.length + b.length);
  joined.set(a);
  joined.set(b, a.length);
  return joined;
};

export class Shine {
  static initialized = initialized;

  static checkConfig(samplerate, bitrate) {
    return wasm.shine_check_config(samplerate, bitrate) >= 0;
  }

  constructor({ samplerate, bitrate, channels, stereoMode }) {
    if (!wasm) throw new Error("Await Shine.initialized first");
    if (channels !== 1 && channels !== 2) throw new Error("Invalid number of channels");
    if (!Shine.checkConfig(samplerate, bitrate)) throw new Error("Invalid configuration");

    const mode = stereoMode ?? (channels === 1 ? StereoMode.MONO : StereoMode.JOINT_STEREO);

    this._handle = wasm.shine_js_init(channels, samplerate, mode, bitrate);
    if (!this._handle) throw new Error("Cannot initialise the encoder");

    this._channels = channels;
    this._samplesPerPass = wasm.shine_samples_per_pass(this._handle);
    this._written = allocate(4);
    this._planes = allocate(channels * POINTER);
    this._pcm = [];
    this._pending = [];

    for (let channel = 0; channel < channels; channel++) {
      const pcm = allocate(this._samplesPerPass * INT16);
      new DataView(memory()).setUint32(this._planes + channel * POINTER, pcm, true);
      this._pcm.push(pcm);
      this._pending.push(new Int16Array(0));
    }
  }

  _take(pointer) {
    const size = new DataView(memory()).getInt32(this._written, true);
    return new Uint8Array(memory(), pointer, size).slice();
  }

  _encodePass(offset) {
    for (let channel = 0; channel < this._channels; channel++)
      new Int16Array(memory(), this._pcm[channel], this._samplesPerPass).set(
        this._pending[channel].subarray(offset, offset + this._samplesPerPass)
      );
    return this._take(wasm.shine_encode_buffer(this._handle, this._planes, this._written));
  }

  /** Encodes one array of samples per channel, Int16Array or Float32Array. */
  encode(data) {
    if (!this._handle) throw new Error("Closed");
    if (data.length !== this._channels) throw new Error("Invalid data");

    for (let channel = 0; channel < this._channels; channel++)
      this._pending[channel] = concat(Int16Array, this._pending[channel], toInt16(data[channel]));

    let encoded = new Uint8Array(0);
    let offset = 0;
    for (; offset + this._samplesPerPass <= this._pending[0].length; offset += this._samplesPerPass)
      encoded = concat(Uint8Array, encoded, this._encodePass(offset));

    for (let channel = 0; channel < this._channels; channel++)
      this._pending[channel] = this._pending[channel].slice(offset);

    return encoded;
  }

  /** Encodes the remaining samples, flushes the encoder and frees it. */
  close() {
    if (!this._handle) throw new Error("Closed");

    let encoded = new Uint8Array(0);
    if (this._pending[0].length > 0) {
      // Zero padded to a whole pass, as the native encoder expects.
      for (let channel = 0; channel < this._channels; channel++)
        this._pending[channel] = concat(
          Int16Array,
          this._pending[channel],
          new Int16Array(this._samplesPerPass - this._pending[channel].length)
        );
      encoded = this._encodePass(0);
    }

    encoded = concat(Uint8Array, encoded, this._take(wasm.shine_flush(this._handle, this._written)));

    wasm.shine_close(this._handle);
    this._handle = null;
    for (const pcm of this._pcm) wasm.free(pcm);
    wasm.free(this._planes);
    wasm.free(this._written);

    return encoded;
  }
}
