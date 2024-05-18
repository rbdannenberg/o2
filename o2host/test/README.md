# Test/Demo Software for o2host

**Roger B. Dannenberg**
**May 2024**

## Websockets Support Test/Demo
Run `o2host` in this directory (so that the default web root `www` will enable
serving the web page `www/buttons.htm` relative to this directory. E.g., from
this directory use the command `../Debug/o2host`.

Configure `o2host` as follows:
- Ensemble name: `bdemo`
- Reference Clock: `Y`
- HTTP Port: `8080` (your choice of port, but must be non-empty)
- Root: (leave blank, the default will be `./www`)
- Type `X` in "New MIDI Out from O2: _" to create a MIDI Out service.
- MIDI Out Service: `midiout`
- `to` (select a synthesizer - you must have a MIDI synthesizer to play MIDI!)

As usual, type ESC to start `o2host`. It will be offering a local web service.

In a browser, visit `http://localhost:8080/buttons.htm`. (Note the protocol is not `https:`)

You should be able to play notes by pressing buttons in the web page. The communication path is O2lite running over websocket to `o2host`, which forwards messages to MIDI.


