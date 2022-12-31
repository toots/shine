// libshine function wrappers

var StereoMode = {
  '0': 'STEREO',
  '1': 'JOINT_STEREO',
  '2': 'DUAL_CHANNEL',
  '3': 'MONO',
  STEREO: 0,
  JOINT_STEREO: 1,
  DUAL_CHANNEL: 2,
  MONO: 3
};

var ShineModule;
var int16Len;
var ptrLen;

function shineInit() {
  int16Len = ShineModule._shine_js_int16_len();
  ptrLen   = ShineModule._shine_js_ptr_len();
};

function Shine(args) {
  if (ShineModule._shine_check_config(args.samplerate, args.bitrate) < 0)
    throw "Invalid configuration";

  var stereoMode;
  if (!args.stereoMode) {
    if (args.channels === 1) {
      stereoMode = Shine.MONO;
    } else {
      stereoMode = Shine.JOINT_STEREO;
    }
  } else {
    stereoMode = args.stereoMode;
  }

  this._handle = ShineModule._shine_js_init(args.channels, args.samplerate, stereoMode, args.bitrate);

  this._channels = args.channels;
  this._samples_per_pass = ShineModule._shine_samples_per_pass(this._handle);

  this._buffer = ShineModule._malloc(this._channels * ptrLen);
  this._pcm = new Array(this._channels);
  this._rem = new Array(this._channels);
  this._written = ShineModule._malloc(int16Len);

  var _tmp, chan;
  for (chan=0; chan<this._channels; chan++) {
    this._rem[chan] = new Int16Array;
    _tmp = ShineModule._malloc(this._samples_per_pass * int16Len);
    ShineModule.setValue(this._buffer + chan*ptrLen, _tmp, "*")
    this._pcm[chan] = ShineModule.HEAP16.subarray(_tmp/int16Len, _tmp/int16Len+this._samples_per_pass) 
  }

  return this; 
};

Shine.checkConfig = function (samplerate, bitrate) {
  return ShineModule._shine_check_config(samplerate, bitrate) >= 0;
};

Shine.prototype._encodePass = function (data) {
  if (!this._handle)
    throw "Closed";

  var chan;
  for (chan=0;chan<this._channels;chan++)
    this._pcm[chan].set(data[chan]);

  var _buf = ShineModule._shine_encode_buffer(this._handle, this._buffer, this._written);

  var written = ShineModule.getValue(this._written, "i16"); 

  return ShineModule.HEAPU8.subarray(_buf, _buf+written);
};

function concat(ctr, a, b) {
  if (typeof b === "undefined") {
    return a;
  }
  var ret = new ctr(a.length+b.length);
  ret.set(a);
  ret.subarray(a.length).set(b);
  return ret;
}

function clip(x) {
  return (x > 1 ? 1 : (x < -1 ? -1 : x));
}

function convertFloat32(buf) {
  var ret = new Array(buf.length);
  var samples = buf[0].length;
  var chan, i;

  for (chan=0;chan<buf.length;chan++) {
    ret[chan] = new Int16Array(samples);
    for (i=0;i<samples;i++) {
      ret[chan][i] = parseInt(clip(buf[chan][i]) * 32767);
    }
  }
  return ret;
}

Shine.prototype.encode = function (data) {
  if (data.length != this._channels)
    throw "Invalid data";

  var encoded = new Uint8Array;  
  var tmp = new Array(this._channels);

  if (data[0] instanceof Float32Array) {
    data = convertFloat32(data);
  }

  var chan;
  for (chan=0;chan<this._channels; chan++) {
    tmp[chan] = new Float32Array;
    this._rem[chan] = concat(Int16Array, this._rem[chan], data[chan]);
  }

  var i, enc;
  for (i=0;i<this._rem[0].length;i+=this._samples_per_pass) {
    for (chan=0; chan<this._channels; chan++) {
      tmp[chan] = this._rem[chan].subarray(i, i+this._samples_per_pass);
    }

    if (tmp[0].length < this._samples_per_pass) {
      break;
    } else {
      enc = this._encodePass(tmp);
      if (enc.length > 0) {
        encoded = concat(Uint8Array, encoded, enc);   
      }
    }
  }

  if (tmp[0].length < this._samples_per_pass) {
    this._rem = tmp;
  } else {
    for (chan=0; chan<this._channels; chan++) {
      this._rem[chan] = new Int16Array;
    }
  }

  return encoded;
};

Shine.prototype.close = function () {
  if (!this._handle) {
    throw "Closed";
  }

  var _buf = ShineModule._shine_flush(this._handle, this._written);

  var written = ShineModule.getValue(this._written, "i16");
  var encoded = new Uint8Array(written);

  encoded.set(ShineModule.HEAPU8.subarray(_buf, _buf + written));

  ShineModule._free(this._written);
  ShineModule._shine_close(this._handle);
  this._handle = null;

  var chan;
  for (chan=0; chan<this._channels; chan++) {
    ShineModule._free(ShineModule.getValue(this._buffer + chan*ptrLen, "*"));
  }
  ShineModule._free(this._buffer);

  return encoded;
};
