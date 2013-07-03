// libshine function wrappers

var int16Len = Module.HEAP16.BYTES_PER_ELEMENT;
var ptrLen = Module.HEAP32.BYTES_PER_ELEMENT;
var floatLen = Module.HEAPF32.BYTES_PER_ELEMENT;

var check_config     = Module.cwrap("shine_check_config", "number", ["number", "number"]);
var init             = Module.cwrap("shine_js_init", "number", ["number", "number", "number", "number"]);
var samples_per_pass = Module.cwrap("shine_samples_per_pass", "number", ["number"]);
var encode_float     = Module.cwrap("shine_js_encode_float_buffer", "number", ["number", "number", "number"]);
var flush            = Module.cwrap("shine_flush", "number", ["number", "number"]);
var close            = Module.cwrap("shine_close", "number", ["number"]);

function Shine(args) {
  if (check_config(args.samplerate, args.bitrate) < 0)
    throw "Invalid configuration";

  this._handle = init(args.channels, args.samplerate, args.mode, args.bitrate);

  this._channels = args.channels;
  this._samples_per_pass = samples_per_pass(this._handle);

  this._buffer = _malloc(this._channels * ptrLen);
  this._pcm = new Array(this._channels);
  this._rem = new Array(this._channels);
  this._written = _malloc(int16Len);

  var _tmp, chan;
  for (chan=0; chan<this._channels; chan++) {
    this._rem[chan] = new Float32Array;
    _tmp = _malloc(this._samples_per_pass * floatLen);
    setValue(this._buffer + chan*ptrLen, _tmp, "*")
    this._pcm[chan] = Module.HEAPF32.subarray(_tmp/floatLen, _tmp/floatLen+this._samples_per_pass) 
  }

  return this; 
};

Shine.STEREO = 0;
Shine.JOINT_STEREO = 1;
Shine.DUAL_CHANNEL = 2;
Shine.MONO = 3;

Shine.prototype._encodePass = function (data) {
  if (!this._handle)
    throw "Closed";

  var chan;
  for (chan=0;chan<this._channels;chan++)
    this._pcm[chan].set(data[chan]);

  var _buf = encode_float(this._handle, this._buffer, this._written);

  var written = getValue(this._written, "i16"); 

  return Module.HEAPU8.subarray(_buf, _buf+written);
};

function concat(ctr, a, b) {
  var ret = new ctr(a.length+b.length);
  ret.set(a);
  ret.subarray(a.length).set(b);
  return ret;
}

Shine.prototype.encode = function (data) {
  var encoded = new Uint8Array;  
  var tmp = new Array(this._channels);

  var chan;
  for (chan=0;chan<this._channels; chan++)
    this._rem[chan] = concat(Float32Array, this._rem[chan], data[chan]);

  var i, enc;
  for (i=0;i<this._rem[0].length;i+=this._samples_per_pass) {
    for (chan=0; chan<this._channels; chan++)
      tmp[chan] = this._rem[chan].subarray(i, i+this._samples_per_pass);

    if (tmp[0].length < this._samples_per_pass) {
      break;
    } else {
      enc = this._encodePass(tmp);
      if (enc.length > 0) encoded = concat(Uint8Array, encoded, enc);   
    }
  }

  if (tmp[0].length < this._samples_per_pass)
    this._rem = tmp;
  else
    for (chan=0; chan<this._channels; chan++)
      this._rem[chan] = new Float32Array;

  return encoded;
};

Shine.prototype.close = function () {
  if (!this._handle)
    throw "Closed";

  var _buf = flush(this._handle, this._written);

  var written = getValue(this._written, "i16");
  var encoded = new Uint8Array(written);

  encoded.set(Module.HEAPU8.subarray(_buf, _buf + written));

  _free(this._written);
  close(this._handle);
  this._handle = null;

  var chan;
  for (chan=0; chan<this._channels; chan++)
    _free(getValue(this._buffer + chan*ptrLen, "*"));
  _free(this._buffer);

  return encoded;
};

return Shine;

}).call(context)})();

