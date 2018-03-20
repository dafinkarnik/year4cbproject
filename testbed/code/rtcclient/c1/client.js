var connection = new WebSocket('ws://10.0.0.5:8888');
//var connection = new WebSocket('ws://localhost:8888'); // Uncomment this line and comment the one above when using outside of the testbed.

var name = "";

var myVideo = document.querySelector('#myVideo'),
    remoteVideo = document.querySelector('#remoteVideo'),
    myConnection, connectedUser, stream;
    
var vidata;

connection.onopen = function () {
  console.log("Connected");
  login();
};

function login(){
  name = "c1";

  if (name.length > 0) {
    send({
      type: "login",
      name: name
    });
  }
};

function send(message) {
  if (connectedUser) {
    message.name = connectedUser;
  }

  connection.send(JSON.stringify(message));
};

connection.onmessage = function (message) {
  console.log("Got message", message.data);

  var data = JSON.parse(message.data);

  switch(data.type) {
    case "login":
      onLogin(data.success);
      break;
    case "offer":
      onOffer(data.offer, data.name);
      break;
    case "answer":
      onAnswer(data.answer);
      break;
    case "candidate":
      onCandidate(data.candidate);
      break;
    case "leave":
      onLeave();
      break;
    default:
      break;
  }
};

connection.onerror = function (err) {
  console.log("Got error", err);
};

function onLogin(success) {
  if (success === false) {
    alert("Login unsuccessful, please try a different name.");
  } else {
    startConnection();
  }
};

function startConnection() {
  if (hasUserMedia()) {
    navigator.mediaDevices.getUserMedia({ video: true, audio: true }).then(function (myStream) {
      stream = myStream;
      myVideo.src = window.URL.createObjectURL(stream);
      if (hasRTCPeerConnection()) {
        setupPeerConnection(stream);
      } else {
        alert("Sorry, your browser does not support WebRTC.");
      }
    }).catch(function (error) {
      console.log(error);
    });
  } else {
    alert("Sorry, your browser does not support WebRTC.");
  }
};

function hasUserMedia() {
  return !!navigator.mediaDevices.getUserMedia;
};

function hasRTCPeerConnection() {
  window.RTCPeerConnection = window.RTCPeerConnection || window.webkitRTCPeerConnection || window.mozRTCPeerConnection;
  window.RTCSessionDescription = window.RTCSessionDescription || window.webkitRTCSessionDescription || window.mozRTCSessionDescription;
  window.RTCIceCandidate = window.RTCIceCandidate || window.webkitRTCIceCandidate || window.mozRTCIceCandidate;
  return !!window.RTCPeerConnection;
};

function setupPeerConnection(stream) {
  var configuration = {
    "iceServers": [{ "url": "stun:127.0.0.1:9876" }]
  };
  myConnection = new RTCPeerConnection(configuration);

  // Setup stream listening
  myConnection.addStream(stream);
  myConnection.onaddstream = function (e) {
    remoteVideo.src = window.URL.createObjectURL(e.stream);
    record(remoteVideo.captureStream());            // Comment this line if using the testbed in headless mode and the Media Recorder is causing problems.
  };

  // Setup ice handling
  myConnection.onicecandidate = function (event) {
    if (event.candidate) {
      send({
        type: "candidate",
        candidate: event.candidate
      });
    }
  };
  setTimeout(function() {startPeerConnection("c2")}, 2000);
};

function record(stream){
	var mediaRecorder = new MediaRecorder(stream);
	vidata = [];
	mediaRecorder.stream.onaddtrack = function(){
        if(mediaRecorder.state != "recording"){
            mediaRecorder.start();
            console.log(mediaRecorder.state);
        }
	}
	
	mediaRecorder.ondataavailable = function(event){
		vidata.push(event.data);
	}	
	
	setTimeout(function() {
		mediaRecorder.stop();
		console.log(mediaRecorder.state);
        }, 10000);
	 

	mediaRecorder.onstop = function(e) {
		console.log("on stop");
		var blob = new Blob(vidata, { 'type' : 'video/webm' });
		connection.send(blob);
	}   
}    

function startPeerConnection(user) {
  connectedUser = user;

  // Begin the offer
  myConnection.createOffer(function (offer) {
    send({
      type: "offer",
      offer: offer
    });
    myConnection.setLocalDescription(offer);
  }, function (error) {
    alert("An error has occurred.");
  });
};

function onOffer(offer, name) {
  connectedUser = name;
  myConnection.setRemoteDescription(new RTCSessionDescription(offer));

  myConnection.createAnswer(function (answer) {
    myConnection.setLocalDescription(answer);
    send({
      type: "answer",
      answer: answer
    });
  }, function (error) {
    alert("An error has occurred");
  });
}

function onAnswer(answer) {
  myConnection.setRemoteDescription(new RTCSessionDescription(answer));
};

function onCandidate(candidate) {
  myConnection.addIceCandidate(new RTCIceCandidate(candidate));
};

function onLeave() {
  connectedUser = null;
  myVideo.src = null;
  myConnection.close();
  myConnection.onicecandidate = null;
  myConnection.onaddstream = null;
  setupPeerConnection(stream);
};