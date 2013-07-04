Shine = require "../../dist/libshine.js"
run   = require "../lib/rounds.js"

console.log ""
console.log "Executing rounds test"
run Shine, (s) ->
  console.log s
