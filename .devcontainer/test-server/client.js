const process = require("process");
const http = require("http");

function getPreciseTime() {
    const hrTime = process.hrtime();
    return hrTime[0] * 1000000 + hrTime[1] / 1000;
}

function makeRequest() {
    return new Promise((resolve, reject) => {
        const payload = JSON.stringify({
            timestamp: getPreciseTime(),
        });
        const request = http.request(
            "http://localhost:8888",
            {
                method: "POST",
                headers: {
                    "Content-Type": "application/json",
                    "Content-Length": Buffer.byteLength(payload),
                    Connection: "keep-alive",
                },
            },
            (response) => {
                let body = "";
                response.on("data", (chunk) => {
                    console.log("Chunk received.");
                    body += chunk;
                });
                response.on("end", () => {
                    console.log("raw body:", `"${body}"`);
                    resolve(JSON.parse(body));
                });
                response.on("error", reject);
            }
        );
        request.on("socket", (socket) => {
            console.log("socket");
            socket.on("close", (x) => {
                console.log("Socket closed");
            });
        });
        request.write(payload);
        request.on("error", reject);
    });
}

async function main() {
    let startTime = Date.now();
    let currentTime = Date.now();
    try {
        const deltaSum = 0;
        let requests = 0;
        while (5 * 60 * 1000 > currentTime - startTime) {
            console.log(`--- Request ${requests} ---`);
            const data = await makeRequest();
            const timeComplete = getPreciseTime();
            const delta = timeComplete - data.timestamp;
            requests++;
            currentTime = Date.now();

            console.log(`Delta ${requests}: ${delta}ns`);
        }
        console.log("Total requests:", requests);
        console.log("Average Delta (ns):", deltaSum / requests);
    } catch (error) {
        console.log(error);
        console.log(
            "Failed after: " +
                new Date(currentTime - startTime).getSeconds() +
                " seconds"
        );
    }
}

main();
