const http = require("http");

http
  .createServer((req, res) => {
    req.on("data", (chunk) => {
      console.log(`chunk: ${chunk}`);
    });
    req.on("end", () => {
      res.write("Pong.\n");
      res.end();
    });
  })
  .on("connection", () => {
    console.log("Connection received");
  })
  .listen(process.argv[2], () => {
    console.log("Ready to ping pong.");
  });
