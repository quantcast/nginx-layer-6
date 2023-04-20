const http = require("http");

http.createServer((req, res) => {
    let msg = "";
    req.on("data", (chunk) => {
        console.log(`chunk: ${chunk}`);
        msg += chunk;
    });
    req.on("close", () => {
        res.write(`${msg}`);
        console.log("Wrote:", msg);
        res.end();
    });
}).on("connection", (socket) => {
    console.log("Connection received");
    socket.on('close', () => {
        console.log("Connection closing...")
    })
}).listen(process.argv[2], () => {
    console.log("Ready to ping pong.");
});
