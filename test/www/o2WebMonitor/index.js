// Getting elements in html for manipulation and values later on.
const connectedMessage = document.getElementById("connected");
const servicesListDisplay = document.getElementById("serviceslist");
const newServiceName = document.getElementById("newName");
const messageSend = document.getElementById("sendMessage");
const typespecInfo = document.getElementById("typespecInput");
const ensembleName = document.getElementById("ensembleName");
const hostName = document.getElementById("hostname");
const toast = document.getElementById("toast");
var messageType;
const statuses = [
  "O2_LOCAL_NOTIME",
  "O2_REMOTE_NOTIME",
  "O2_BRIDGE_NOTIME",
  "O2_TO_OSC_NOTIME",
  "O2_LOCAL",
  "O2_REMOTE",
  "O2_BRIDGE",
  "O2_TO_OSC",
  "O2_TAP",
];
hostName.value = document.location.host;
var data = { services: [] };
var serviceNameMsgs;
let selectedService = ""; // global variable to track selected service for showing received messages.
var tappeeService = ""; // global variable to track service being tapped for showing messages.

// Initializing the o2host and connecting to it.
function o2wsInit() {
  o2ws_initialize(ensembleName.value, hostName.value);
  if (o2ws_websocket) {
    // console.log("connection successful");
    document.getElementById("mainBody").hidden = false;
    connectedMessage.hidden = false;
    document.getElementById("ensembleGetter").hidden = true;
    o2ws_method_new("/_o2/ls", "siss", false, handle_list_services, null);
    handlebarsHelpers();
  } else {
    bootstrap.Toast.getOrCreateInstance(toast).show();
    // console.log("connection failed, try again");
  }
}

// Registering helpers for Handlebars scripts.
function handlebarsHelpers() {
  Handlebars.registerHelper("ifCond", function (v1, v2, options) {
    if (v1 === v2) {
      return true;
    }
    return false;
  });
}

function o2ws_status_msg(msg) {
  console.log("status message received: " + msg);
}

function o2ws_on_error(msg) {
  console.log("error message received: " + msg);
}

// Handles displaying list of services connected to this o2host.
function handle_list_services(address, typespec, info) {
  // console.log("handler called: " + address);
  var service_name = o2ws_get_string();
  // console.log(service_name);
  var status = o2ws_get_int();
  // console.log(status);
  x = o2ws_get_string();
  // console.log(x);
  x = o2ws_get_string();
  // console.log(x);
  // console.log(address);
  // console.log(info);
  displayService(service_name, info, status);
}

// Handler for message receiving to a service.
function handle_message(timestamp, address, typespec, info) {
  // in the format /sensor/data@10.760 "iffs" 15 3.1415 2.0 "hello world"
  const messageData = {
    timestamp: timestamp,
    address: address,
    message: "",
  };
  let a = "";
  // console.log("typespec is " + typespec);
  for (let i = 0; i < typespec.length; i++) {
    if (typespec[i] === "i") {
      a += o2ws_get_int() + " ";
    } else if (typespec[i] === "s") {
      a += ' "' + o2ws_get_string() + '" ';
    } else if (typespec[i] === "f") {
      a += o2ws_get_float() + "f ";
    } else if (typespec[i] === "d") {
      a += o2ws_get_double() + "d ";
    }
    
  }

  var msg = "/" + address + "@" + timestamp + ' "' + typespec + '" ' + a;
  messageData.message = msg;
  for (let i = 0; i < data.services.length; i++) {
    if (address.startsWith(data.services[i].name)) {
      let l = data.services[i].messages.length;
      data.services[i].messages.push(messageData);
    }
  }
  // console.log(data);
}

// Displays service information.
function displayService(serviceName, serviceTypespec, serviceStatus) {
  // console.log(data);
  for (let i = 0; i < data.services.length; i++) {
    if (data.services[i].name === serviceName) {
      return;
    }
  }

  if (serviceName === "_o2" || serviceName === "_cs" || serviceName === "") {
    return;
  } else {
    data.services.push({
      name: serviceName,
      // typespecString: serviceTypespec,
      status: serviceStatus,
      statusName: statuses[serviceStatus],
      selected: false,
      messages: [],
      localService: false,
    });
  }

  generateOutput();
  // console.log("displaying " + data);
}

// Discover and display all services.
function viewServices() {
  // console.log("starting discovery");
  var time = 0;

  if (o2ws_clock_synchronized) {
    time = o2ws_time_get();
    // console.log("clock synced");
  }

  o2ws_send("/_o2/ws/ls", time, "");
}

// Create a new service.
function createService() {
  newservice = "";
  if (newServiceName.value != "") {
    newservice = newServiceName.value;
  }
  typespec = null;
  // console.log(
//     "starting creating service: " + newservice + " typespec = " + typespec
//   );
  o2ws_method_new("/" + newservice, typespec, false, handle_message, null);

  data.services.push({
    name: newservice,
    status: 6,
    statusName: statuses[6],
    selected: false,
    messages: [],
    localService: true,
  });
  // console.log(data);

  o2ws_set_services(newservice);

  var time = 0;

  if (o2ws_clock_synchronized) {
    time = o2ws_time_get();
    // console.log("clock synced");
  }

  o2ws_send("/_o2/ws/ls", time, "");
  // console.log(newservice);
  generateOutput();
}

function setMessageType() {
  messageType = document.getElementById("messageType").value;
}

// Send message to service.
function sendMsg(e) {
  serviceName = e.id;
  const messageGet = document.getElementById("messageGet-" + serviceName);

  var time = 0;

  if (o2ws_clock_synchronized) {
    time = o2ws_time_get();
    // console.log("clock synced");
  }

  // TODO: it shows me clock synchronized and then gives this error when sending message

  const messageAddress = document.getElementById(
    "addressPostfix-" + serviceName
  );
  const messageType = document.getElementById("messageType-" + serviceName);
  let messageArr = messageGet.value.split(",");
  var typespec = messageType.value;
  let arr = ["/" + serviceName + "/" + messageAddress.value, time, typespec];

  for (let i = 0; i < typespec.length; i++) {
    if (typespec[i] === "i") {
      arr.push(parseInt(messageArr[i]));
    } else if (typespec[i] === "s") {
      arr.push(messageArr[i]);
    } else if (typespec[i] === "f") {
      arr.push(parseFloat(messageArr[i]));
    } else if (typespec[i] === "d") {
      arr.push(parseFloat(messageArr[i]));
    }
   //  console.log("message is " + messageGet.value);
  }
//   console.log(arr);
  o2ws_send.call(this, ...arr);
//   console.log("msg is " + messageGet.value);
  messageGet.value = "";
  messageType.value = "";
  messageAddress.value = "";
//   console.log("message sent to " + serviceName);
}

function tapModal(e) {
  tapServiceName = document.getElementById("tapServiceName");
  tapServiceName.value = "tap_" + e.id;
  tappeeService = e.id;
}

// Tap a service to have all messages sent to that service forwarded to this one.
function tap() {
  const sel = document.getElementById("taptype");
  const tapType = sel.options[sel.selectedIndex].value;

  var tapper = document.getElementById("tapServiceName").value;
  // assign default val in text as tapper but take input
  o2ws_method_new("/" + tapper, null, false, handle_message, null);

  o2ws_set_services(tapper);
  data.services.push({
    name: tapper,
    status: 6, // TODO: understand status/ take input?
    selected: false,
    messages: [],
    localService: true,
  });


  var tappingString = "" + tappeeService + ":" + tapper + ":" + tapType; // "K" for TAP_KEEP, "R" for TAP_RELIABLE, or "B" for TAP_BEST_EFFORT
  o2ws_tap(tappingString);
  generateOutput();
  // console.log(tappingString);
}

// Shows messages sent to a specific service.
function showMessages(e) {
  serviceNameMsgs = e.innerText.toString();
  selectedService = serviceNameMsgs.trim();
  for (let i = 0; i < data.services.length; i++) {
    if (data.services[i].name === selectedService) {
      data.services[i].selected = true;
    } else {
      data.services[i].selected = false;
    }
  }
  // console.log(data);
  genModalOutput();
}

function generateOutput() {
  var template = Handlebars.compile(
    document.getElementById("hbsBody").innerHTML
  );
  htmlData = template(data);
  document.getElementById("output").innerHTML = htmlData;
}

document.onload = generateOutput();

function genModalOutput() {
  var template = Handlebars.compile(
    document.getElementById("hbsModal").innerHTML
  );
  htmlData = template(data);
  document.getElementById("modalOutput").innerHTML = htmlData;
}
