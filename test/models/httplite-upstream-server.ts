export class HttpliteUpstreamServer {
    ip: string;
    port: number;
    connections: number;

    constructor(props?: Partial<HttpliteUpstreamServer>) {
        const { ip, port, connections } = {
            ip: "0.0.0.0",
            port: 0,
            connections: 5,
            ...props,
        };
        this.ip = ip;
        this.port = port;
        this.connections = connections;
    }
}
