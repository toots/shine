import { Fragment, useEffect, useState, useCallback } from "react"
import { runEncodeTest } from "@shine/lib/encode"
import { runRoundsTest } from "@shine/lib/rounds"

const logEntries: string[] = []
let encoderStarted = false

export default function Home() {
  const [logs, setLogs] = useState(logEntries)
  const [encodedBlobUrl, setEncodedBlobUrl] = useState<string | undefined>()

  const log = useCallback(
    (entry: string) => {
      logEntries.push(entry)
      setLogs(logEntries)
    },
    [setLogs]
  )

  useEffect(() => {
    if (encoderStarted) return

    runEncodeTest(log, blob => {
      if (!encodedBlobUrl) setEncodedBlobUrl(URL.createObjectURL(blob))

      log("")
      runRoundsTest(log)
    })

    encoderStarted = true
  }, [log, encodedBlobUrl, setEncodedBlobUrl])

  return (
    <>
      {encodedBlobUrl && (
        <a href={encodedBlobUrl} download='encoded.mp3'>
          Download encoded file
        </a>
      )}
      <div>
        {logs.map(entry => (
          <Fragment key={entry}>
            {entry}
            <br />
          </Fragment>
        ))}
      </div>
    </>
  )
}
