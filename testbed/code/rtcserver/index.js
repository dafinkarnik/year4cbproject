var WebSocketServer = require('ws').Server,
    wss = new WebSocketServer({ port: 8888 }),
    users = {};
var fs = require('fs');

function sendTo(conn, message) {
  conn.send(JSON.stringify(message));
}

wss.on('listening', function () {
    console.log("Server started...");
});

wss.on('connection', function (connection) {
  connection.on('message', function (message) {
    var data;
    var skipDefault = 0;
    
    try {
      data = JSON.parse(message);
    } catch (e) {
      skipDefault = 1;
      fs.writeFile("video.webm", message, function(err) {
            skipDefault = 0;
            if(err) {
                console.log(err);
        }});
      if(skipDefault == 1){
        console.log("Received video sample blob.");
      }
      data = {};
    }

    switch (data.type) {
      case "login":
        console.log("User logged in as", data.name);
        if (users[data.name]) {
          sendTo(connection, {
            type: "login",
            success: false
          });
        } else {
          users[data.name] = connection;
          connection.name = data.name;
          sendTo(connection, {
            type: "login",
            success: true
          });
        }

        break;
      case "offer":
        console.log("Sending offer to", data.name);
        var conn = users[data.name];

        if (conn != null) {
          connection.otherName = data.name;
          sendTo(conn, {
            type: "offer",
            offer: data.offer,
            name: connection.name
          });
        }

        break;
      case "answer":
        console.log("Sending answer to", data.name);
        var conn = users[data.name];

        if (conn != null) {
          connection.otherName = data.name;
          sendTo(conn, {
            type: "answer",
            answer: data.answer
          });
        }

        break;
      case "candidate":
        console.log("Sending candidate to", data.name);
        var conn = users[data.name];

        if (conn != null) {
          sendTo(conn, {
            type: "candidate",
            candidate: data.candidate
          });
        }

        break;
      case "leave":
        console.log("Disconnecting user from", data.name);
        var conn = users[data.name];
        conn.otherName = null;

        if (conn != null) {
          sendTo(conn, {
            type: "leave"
          });
        }

        break;
      default:
        if(skipDefault == 0){
            sendTo(connection, {
                type: "error",
                message: "Unrecognized command: " + data.type
            });
        }

        break;
    }
  });

  connection.on('close', function () {
    if (connection.name) {
      delete users[connection.name];

      if (connection.otherName) {
        console.log("Disconnecting user from", connection.otherName);
        var conn = users[connection.otherName];
        conn.otherName = null;

        if (conn != null) {
          sendTo(conn, {
            type: "leave"
          });
        }
      }
    }
  });
});
