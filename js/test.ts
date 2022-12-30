import Shine from "./dist/libshine";

const runRoundsTest = async () => {
  await Shine.initialized;

  console.log("Executing rounds test");

  const channels = 2;
  const samplerate = 44100;
  const nPasses = 50;
  const frameSize = 4096;
  const data = new Array(channels);
  for (let chan = 0; chan < channels; chan++) data[chan] = new Int16Array(frameSize);

  console.log(`Encoding ${nPasses} buffers of ${frameSize} samples`);
  const started = new Date();

  const shine = new Shine({
    samplerate: samplerate,
    bitrate: 128,
    channels: channels,
    mode: Shine.StereoMode.STEREO,
  });

  for (let i = 0; i < nPasses; i++) shine.encode(data);
  shine.close();

  const ended = new Date();
  const duration = (nPasses * frameSize) / samplerate;
  const encodingTime = (ended.getTime() - started.getTime()) / 1000;
  console.log("Done encoding");
  console.log(`Total duration: ${duration.toFixed(2)}`);
  console.log(`Encoding time: ${encodingTime.toFixed(2)}`);
  console.log(`Encoding rate: ${(duration / encodingTime).toFixed(2)}X`);
};

runRoundsTest();
