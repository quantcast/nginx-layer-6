import { HTTPRequest } from "../models/http-request";
import { HTTPResponse } from "../models/http-response";
import { LoadBalancer } from "./load-balancer";
import { Socket, createConnection } from "net";

export class PipelineRequestClient {
    private readonly loadBalancer: LoadBalancer;

    constructor(loadBalancer: LoadBalancer) {
        this.loadBalancer = loadBalancer;
    }

    async pipelineRequests(requests: HTTPRequest[]) {
        const socket = await this.connect();
        const flushed = socket.write(this.formatRequests(requests));
        if (!flushed) {
            await this.drain(socket);
        }
        const responses = await this.readResponses(socket, requests);
        await this.close(socket);
        return responses;
    }

    private connect() {
        return new Promise<Socket>((resolve, reject) => {
            const socket = createConnection({
                host: "localhost",
                port: this.loadBalancer.port(),
            });
            socket.on("connect", () => resolve(socket));
        });
    }

    private drain(socket: Socket) {
        return new Promise((resolve) => socket.on("drain", resolve));
    }

    private formatRequests(requests: HTTPRequest[]) {
        return requests.reduce<string>((rawRequest, request) => {
            return `${rawRequest}${this.serializeRequest(request)}`;
        }, "");
    }

    private serializeRequest(request: HTTPRequest) {
        const requestLine = `${request.method} ${request.path} HTTP/1.1\r\n`;

        return `${requestLine}${this.serializeHeaders(
            request.headers
        )}\r\n\r\n${request.body}`;
    }

    private serializeHeaders(headers: Record<string, string>) {
        const headerBuilder: string[] = [];
        for (const name in headers) {
            headerBuilder.push(`${name}: ${headers[name]}`);
        }
        return headerBuilder.join("\r\n");
    }

    private readResponses(socket: Socket, requests: HTTPRequest[]) {
        return new Promise<HTTPResponse[]>((resolve, reject) => {
            let responses: HTTPResponse[] = [];
            socket.on("error", (error) => reject(error));
            socket.on("data", (data) => {
                // Assuming chunked encoding
                const messages = data.toString().split("\r\n0\r\n\r\n");
                messages.forEach((message) => {
                    if (message) {
                        responses.push(this.parseResponse(message));
                    }
                });
                if (responses.length === requests.length) {
                    resolve(responses);
                }
            });
        });
    }

    private parseResponse(message: string) {
        const parts = message.split("\r\n");
        const responseLine = parts[0];
        const body = parts[parts.length - 1];
        const status = parseInt(responseLine.split(" ")[1]);
        const headers: Record<string, string> = {};
        for (let i = 1; i < parts.length - 1; i++) {
            const header = parts[i];
            if (!header) {
                break;
            }
            const headerParts = header.split(":");
            headers[headerParts[0]] = headerParts[1];
        }
        return new HTTPResponse({
            status,
            body,
            headers,
        });
    }

    private close(socket: Socket) {
        return new Promise<void>((resolve) => {
            socket.on("end", () => {
                resolve();
            });
            socket.end();
        });
    }
}
