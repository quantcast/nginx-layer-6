const http = require("http");

http
  .createServer((req, res) => {
    let msg = ""
    req.on("data", (chunk) => {
      console.log(`chunk: ${chunk}`);
      msg = chunk
    });
    req.on("end", () => {
      res.write(`${msg}\n`);
      res.end();
    });
  })
  .on("connection", (socket) => {
    console.log("Connection received");
    socket.on('close', () => {
      console.log('Connection closed...')
    })
  })
  .listen(process.argv[2], () => {
    console.log("Ready to ping pong.");
  });
