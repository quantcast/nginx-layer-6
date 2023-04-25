import http from "http";
import { ServerDetails } from "./server-details";
import { ConnectionDetails } from "./connection-details";
import { Socket } from "net";

type RequestHandler = (
    req: http.IncomingMessage,
    res: http.ServerResponse,
    server: MockServer
) => void;

export class MockServer {
    private server: http.Server | null;
    private port: number;
    private details: ServerDetails;
    private verbose: boolean;
    private requestHandler: RequestHandler;

    constructor(port: number, verbose: boolean = false) {
        this.port = port;
        this.verbose = verbose;
        this.details = new ServerDetails();
        this.requestHandler = this.defaultRequestHandler;
    }

    getDetails() {
        return this.details;
    }

    onRequest(handler: RequestHandler) {
        this.requestHandler = handler;
    }

    private defaultRequestHandler(
        req: http.IncomingMessage,
        res: http.ServerResponse
    ) {
        let msg = "";
        req.on("data", (chunk) => {
            msg += chunk;
        });
        req.on("end", () => {
            res.write(`${msg}\n`);
            res.end();
        });
    }

    open() {
        return new Promise<void>((resolve, reject) => {
            this.server = http
                .createServer((req, res) => {
                    this.details.requests++;
                    this.requestHandler(req, res, this);
                })
                .on("listening", () => resolve())
                .on("error", (error) => this.handleError(reject, error))
                .on("connection", (socket) => this.handleConnection(socket))
                .listen(this.port);
        });
    }

    private handleConnection(socket: Socket) {
        this.details.connections.push(
            this.createConnectionDetails(socket, new Date())
        );
    }

    private createConnectionDetails(socket: Socket, openedAt: Date) {
        return new Promise<ConnectionDetails>((resolve, reject) => {
            const connectionDetails = new ConnectionDetails({
                socket,
                timeOpened: openedAt,
            });
            if (this.verbose) {
                console.log("open", connectionDetails.timeOpened);
            }
            socket.on("close", async () => {
                connectionDetails.timeClosed = new Date();
                if (this.verbose) {
                    console.log("close", connectionDetails.timeClosed);
                }
                resolve(connectionDetails);
            });
            socket.on("error", (error) => {
                reject(error);
            });
        });
    }

    private handleError(reject: (error: any) => void, error: any) {
        if (error.code === "EADDRINUSE") {
            reject(`Port ${this.port} already in use`);
        }
    }

    close() {
        if (this.server) {
            this.server.close();
            this.details = new ServerDetails();
        }
    }
}
