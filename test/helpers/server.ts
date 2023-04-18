import http from "http";

class PingServer {
    private server: http.Server | null;
    private port: number;

    private requests: number;
    private connections: number;

    constructor(port: number) {
        this.port = port;
        this.requests = 0;
        this.connections = 0;
    }

    getDetails() {
        return {
            requests: this.requests,
            connections: this.connections,
        };
    }

    open() {
        return new Promise<void>((resolve) => {
            this.server = http
                .createServer((req, res) => {
                    this.requests++;
                    req.on("data", () => {
                    });
                    req.on("end", () => {
                        res.write("Pong.\n");
                        res.end();
                    });
                })
                .on("connection", () => {
                    this.connections++;
                })
                .listen(this.port, () => {
                    resolve();
                });
        });
    }

    close() {
        if (this.server) {
            this.server.close();
        }
    }
}

export function createPingUpstream(port: number) {
    return new PingServer(port);
}
