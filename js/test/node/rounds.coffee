Shine         = require "../../dist/libshine.js"
runRoundsTest = require "../lib/rounds.js"

console.log ""
console.log "Executing rounds test"
runRoundsTest Shine, (s) ->
  console.log s
