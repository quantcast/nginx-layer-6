const http = require("http");
const KEEP_ALIVE = process.argv.length >= 4 ? parseInt(process.argv[3]) : 0

const server = http
  .createServer((req, res) => {
    let msg = ""
    req.on("data", (chunk) => {
      console.log(`chunk: ${chunk}`);
      msg = chunk
    });
    req.on("end", () => {
      res.setTimeout(0);
      res.write(`${msg}\n`);
      res.end();
    });
  }).on("connection", (socket) => {
    console.log(`Connection created with keep-alive ${KEEP_ALIVE} at time: ${new Date().toISOString()}`);
    // socket.setTimeout(KEEP_ALIVE);
    socket.on('close', () => {
      console.log(`Connection closing at time: ${new Date().toISOString()}`)
    })
  })
  .listen(process.argv[2], () => {
    console.log("Ready to ping pong.");
  });

server.keepAliveTimeout = 0;
