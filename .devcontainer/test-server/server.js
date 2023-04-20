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
  .on("connection", () => {
    console.log("Connection received");
  })
  .listen(process.argv[2], () => {
    console.log("Ready to ping pong.");
  });
