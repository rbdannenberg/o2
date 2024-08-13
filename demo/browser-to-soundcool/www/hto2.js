// hto2.js - handtrack to o2
//
// Roger B. Dannenberg
// Jun 2024
//
// based on code from https://victordibia.com/handtrack.js/#/

//--------------------- Handtrack Interface -----------------

const video = document.getElementById("video");
const canvas = document.getElementById("canvas");
const context = canvas.getContext("2d");
let trackButton = document.getElementById("trackbutton");
let updateNote = document.getElementById("updatenote");

let isVideo = false;
let model = null;

const modelParams = {
    flipHorizontal: true,   // flip e.g for video  
    maxNumBoxes: 20,        // maximum number of boxes to detect
    iouThreshold: 0.5,      // ioU threshold for non-max suppression
    scoreThreshold: 0.6,    // confidence threshold for predictions.
}


function startVideo() {
    handTrack.startVideo(video).then(function (status) {
        console.log("video started", status);
        if (status) {
            updateNote.innerText = "Video started. Now tracking";
            isVideo = true;
            runDetection();
        } else {
            updateNote.innerText = "Please enable video";
        }
    });
}


function stopVideo() {
    updateNote.innerText = "Stopping video";
    handTrack.stopVideo(video);
    isVideo = false;
    updateNote.innerText = "Video stopped";
}


function toggleVideo() {
    if (!isVideo) {
        updateNote.innerText = "Starting video";
        startVideo();
    } else {
        stopVideo();
    }
}


function print_predictions(predictions) {
    for (prediction of predictions) {
        console.log(prediction.label, prediction.class, prediction.bbox[0]);
    }
}


function runDetection() {
    model.detect(video).then(predictions => {
        // console.log("Predictions: ", predictions);
        // print_predictions(predictions);
        send_predictions(predictions);
        video.style.height = 240;
        model.renderPredictions(predictions, canvas, context, video);
        if (isVideo) {
            requestAnimationFrame(runDetection);
        }
    });
}


//-------------------- O2lite Interface -----------------
//
// for an initial test, we'll look for pinch gestures in 3 regions
// of the screen: left, middle, right. Left will start/stop the
// drum loop. Middle and right will select at random and play one
// of the remaining sounds.
//
// Messages are:
// Port 8001, corresponds to O2 service "sndcues"
//   /4/push2 1 -- bassnote-edit.wav (send 0 after 1)
//   /4/push3 1 -- slide-edit.wav (send 0 after 1)
//   /4/push4 1 -- stringa-edit.wav (send 0 after 1)
//   /4/push5 1 -- stringpad.wav (send 0 after 1)
//   /4/push6 1 -- stringtone-edit.wav (send 0 after 1)
//   /4/push7 1 -- violine5-edit.wav (send 0 after 1)
// Port 8002, corresponds to O2 service "drumloop"
//   /4/push1 1 -- drumloop-edit.wav (send 0 after 1)
//
// Gesture classes are:
// 1 - open
// 2 - closed
// 3 - pinch
// 4 - point up
// 5 - face
const PINCH = 3;
const POINT = 4;
const FACE = 5;

// "normal" bbox[0] (x) coordinates for left, middle, right:
const LEFT = 20;
const MIDDLE = 220;
const RIGHT = 420;

// how many classes do we need in a row to take action?
const REPEAT = 3;

var middle_cues = ["/sndcues/4/push2", "/sndcues/4/push3", "/sndcues/4/push4"];
var right_cues = ["/sndcues/4/push5", "/sndcues/4/push6", "/sndcues/4/push7"];


function pick_one(a) {
    var n = a.length;
    return a[Math.floor(Math.random() * n)];
}



function o2ws_on_error(msg) {
    console.log(msg);
}


function o2ws_status_msg(msg) {
    updateNote.innerText = msg;
}


function hto2_initialize(ensemble) {
    o2ws_initialize(ensemble);
    o2ws_status_msgs_enable = true;
    // Load the model.
    handTrack.load(modelParams).then(lmodel => {
        // detect objects in the image.
        model = lmodel
        updateNote.innerText = "Loaded Model!"
        trackButton.disabled = false
    });
}


// when can we respond to a gesture?
var min_action_time = 0;
var previous_class = -1;
var class_count = 0;


function send_predictions(predictions) {
    for (p of predictions) {
        if (p.class != FACE) {  // ignore all FACE inputs
            if (previous_class == p.class) {
                class_count += 1;
            } else {
                previous_class = p.class;
                class_count = 0;
            }
        }

        // take action *once* after REPEAT consecutive inputs
        // of the same class; then you have to input a different
        // class.
        if ((p.class == PINCH || p.class == POINT) &&
            class_count == REPEAT &&
            o2ws_local_time() > min_action_time) {
            var address;
            var x = p.bbox[0];
            var y = p.bbox[1];
            if (x < (LEFT + MIDDLE) / 2) {
                // position is left
                address = "/drumloop/4/push1";
            } else if (x < (MIDDLE + RIGHT) / 2) {
                if (p.class == PINCH) {
                    if (y < 180) {
                        address = "/sndcues/4/push2";
                    } else {
                        address = "/sndcues/4/push3";
                    }
                } else {
                    address = "/sndcues/4/push4";
                }
            } else {
                if (p.class == PINCH) {
                    if (y < 180) {
                        address = "/sndcues/4/push5";
                    } else {
                        address = "/sndcues/4/push6";
                    }
                } else {
                    address = "/sndcues/4/push7";
                }
            }
            o2ws_send(address, 0, "f", 1.0);
            setTimeout(() => { o2ws_send(address, 0, "f", 0.0); }, 250);
            console.log(address, p.bbox[0], p.bbox[1]);
            min_action_time = o2ws_local_time() + 1.0;  // 1 sec delay
        }
    }
}
