Shine         = require "../../dist/libshine.js"
runRoundsTest = require "../lib/rounds.js"

exec = () =>
  await Shine.initialized

  console.log ""
  runRoundsTest Shine, (s) ->
    console.log s

exec()
