declare module "@toots/shine.js" {
  type Data = Int16Array | Float32Array;

  namespace Shine {
    enum StereoMode {
      STEREO = 0,
      JOINT_STEREO = 1,
      DUAL_CHANNEL = 2,
      MONO = 3,
    }
  }

  class Shine {
    static initialized: Promise<void>;
    static checkConfig: (samplerate: number, bitrate: number) => boolean;
    constructor(args: { samplerate: number; bitrate: number; channels: number; stereoMode?: Shine.StereoMode });
    encode(data: Data[]): void;
    close(): void;
  }

  export default Shine;
}
