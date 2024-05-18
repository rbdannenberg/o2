# Buttons Demo Software for O2 and Serpent

**Roger B. Dannenberg**
**November 2022**

To run this demo, you need
[wxSerpent](https://www.cs.cmu.edu/~music/serpent/doc/serpent.htm)
with O2 functions enabled.

## Basic Button Demo
The basic demo is a (software) panel of 4 buttons. Pressing a button
sends an O2 command to a server that sends MIDI to play a note. In
this directory:
- Create the buttons: `wxserpent64 buttons`. You should see a small
window with 4 buttons.
- Create the server: `wxserpent64 synthctrl`. You should see a window
that says "O2 Demonstration: synth service"
- Create a MIDI player. I use SimpleSynth, but anything, including a
hardware MIDI interface and synthesizer will do. 

After opening the synthesizer and `syntctrl` program, you may have to
select a MIDI device: Click on the `synthctrl` window. In the `File`
menu, select the desired MIDI output device (e.g., "SimpleSynth virtual input").
If your synthesizer lets you choose an input device, be sure to choose
the same device that `synthctrl` is using (with SimpleSynth, you probably
have a choice of "SimpleSynth virtual input" or an IAC device named
something like "soft-thru Bus").

**Click on a number** in `buttons` and you should hear a sound!

## O2Lite over WebSockets Demo
Browsers can communicate over O2 using the O2Lite protocol. This
protocol runs over various lower-level protocols including WebSockets.

For a simple example/demonstration, the buttons interface has been
reproduced using Javascript and HTML. To demonstrate a browser
interface communicating over O2, follow these steps, starting in
this directory:
- Create the server: `wxserpent64 synthctrl` (exactly as in the Basic
  Button Demo described above).
- Create an O2 host with the web server and web sockets feature
  enabled: `../../Debug/websockhost - bdemo` (just as described below
  under "start websockhost").
- Open `http://localhost:8080/buttons.htm` in a browser.
- You should get a page with 4 buttons. They will send messages over
  a websocket to `websockhost` which forwards the messages to
  `synthctrl.srp` which will send MIDI and play notes.
  

## O2 Spy Demo
There is an experimental O2 debugger/monitor named `o2spy`. After
starting the "Basic Button Demo," you can spy on messages:
- Create O2 Spy:
  - cd to this directory (o2/demo/buttons) (important because it will
    use the subdirectory www so serve the o2spy.html page)
  - start websockhost:
    - websockhost gets compiled as part of O2 provided you retained
      the default CMake option `TESTS_BUILD` enabled. The location of
      the executable depends on your configuration, e.g. if you build
      the debug version of O2 on MacOS with Xcode, it will be in
      `o2/Debug/websockhost`.
    - You *must* tell websockhost to connect to the `bdemo` ensemble
      with a command like this: `../../Debug/websockhost -
      bdemo`. Note the `-` means "no flags" (but you can specify
      debugging flags if you wish) and the *second* argument is the
      ensemble name.
  - open `http://localhost:8080/o2spy.html
- To spy on the `synth` service:
  - enter `bdemo` as the ensemble
  - enter `synth` as the service
  - click on `Start O2` to initialize O2 over websocket to `websockhost`
  - probably uncheck `Show O2 Status Messages`
  - click on `Install Tap` to tap the `synth` service
- Now, when you click on a button or move the slider, you should see
  messages displayed in the browser window.

Credit/Attribution: Spy image and icon files are from
[http://www.roundicons.com](http://www.roundicons.com).
