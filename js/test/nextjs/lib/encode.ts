import { Shine } from "@toots/shine.js"

const convertInterleavedBuffer = (buf: Int16Array, channels: number, samples: number) => {
  const ret = new Array(channels)
  for (let chan = 0; chan < channels; chan++) {
    ret[chan] = new Int16Array(samples)
    for (let i = 0; i < samples; i++) {
      ret[chan][i] = buf[i * channels + chan]
    }
  }
  return ret
}

export const runEncodeTest = (log: (_: string) => void, callback: (_: Blob) => void) => {
  log("Executing encoding test")

  const samplerate = 44100
  const channels = 2
  const bitrate = 128
  //const stereoMode = StereoMode.STEREO

  const shine = new Shine({
    samplerate: samplerate,
    channels: channels,
    bitrate: bitrate,
    //    stereoMode: stereoMode,
  })

  const started = new Date()
  let duration = 0.0
  const encoded: Uint8Array[] = []

  const xhr = new XMLHttpRequest()
  xhr.open("GET", "encode.wav", true)
  xhr.responseType = "arraybuffer"
  xhr.onload = () => {
    const samples = xhr.response.byteLength / (2 * channels)
    const data = new Int16Array(xhr.response)
    const buf = convertInterleavedBuffer(data, channels, samples)
    duration += samples / samplerate
    encoded.push(shine.encode(buf))
  }

  log("Got WAV file.")
  log("Encoding..")
  xhr.addEventListener("load", () => {
    encoded.push(shine.close())

    const ended = new Date()
    const encodingTime = (ended.getTime() - started.getTime()) / 1000
    log("Done encoding.")
    log("File duration: " + duration.toFixed(2) + " seconds")
    log("Encoding time: " + encodingTime.toFixed(2) + " seconds")
    log("Encoding rate: " + (duration / encodingTime).toFixed(2) + "X")
    callback(new Blob(encoded))
  })

  xhr.send()
}
