export type Data = Int16Array | Float32Array;
export declare enum StereoMode {
    STEREO = 0,
    JOINT_STEREO = 1,
    DUAL_CHANNEL = 2,
    MONO = 3
}
export class Shine {
    static initialized: Promise<void>;
    static checkConfig(samplerate: any, bitrate: any): boolean;
    private _handle;
    private _channels;
    private _samples_per_pass;
    private _buffer;
    private _pcm;
    private _rem;
    private _written;
    constructor(args: {
        samplerate: number;
        bitrate: number;
        channels: number;
        stereoMode?: StereoMode;
    });
    private _encodePass;
    encode(data: Data[]): Uint8Array;
    close(): Uint8Array;
}
