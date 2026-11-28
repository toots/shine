export type Data = Int16Array | Float32Array;
export declare enum StereoMode {
  STEREO = 0,
  JOINT_STEREO = 1,
  DUAL_CHANNEL = 2,
  MONO = 3,
}
export class Shine {
  static initialized: Promise<void>;
  static checkConfig(samplerate: number, bitrate: number): boolean;
  /** quality: 0 (default) runs no psychoacoustic model, 1 runs it. */
  constructor(args: {
    samplerate: number;
    bitrate: number;
    channels: number;
    stereoMode?: StereoMode;
    quality?: number;
  });
  encode(data: Data[]): Uint8Array;
  close(): Uint8Array;
}
