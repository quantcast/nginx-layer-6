const http = require("http");

http.createServer((req, res) => {
    let msg = "";
    req.on("data", (chunk) => {
        msg += chunk;
    });
    req.on("end", () => {
        res.write(`${msg}\n`);
        res.end();
    });
})
    .on("connection", (socket) => {
        console.log("Connection received");
        console.log("open", new Date());
        socket.on("close", () => {
            console.log("closed socket");
            console.log("close", new Date());
        });
    })
    .listen(process.argv[2], () => {
        console.log("Ready to ping pong.");
    });
