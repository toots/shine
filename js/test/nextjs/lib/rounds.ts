import { Shine, StereoMode } from "@toots/shine.js"

const channels = 2 // test.wav is stereo
const samplerate = 44100 // ditto

export const runRoundsTest = (log: (_: string) => void) => {
  log("Executing rounds test")

  const nPasses = 50
  const frameSize = 4096
  const data = new Array(channels)
  for (let chan = 0; chan < channels; chan++) data[chan] = new Int16Array(frameSize)

  log("Encoding " + nPasses + " buffers of " + frameSize + " samples")
  const started = new Date()

  const shine = new Shine({
    samplerate: samplerate,
    bitrate: 128,
    channels: channels,
    stereoMode: StereoMode.STEREO,
  })

  for (let i = 0; i < nPasses; i++) shine.encode(data)
  shine.close()

  const ended = new Date()
  const duration = (nPasses * frameSize) / samplerate
  const encodingTime = (ended.getTime() - started.getTime()) / 1000
  log("Done encoding")
  log("Total duration: " + duration.toFixed(2))
  log("Encoding time: " + encodingTime.toFixed(2))
  log("Encoding rate: " + (duration / encodingTime).toFixed(2) + "X")
}
