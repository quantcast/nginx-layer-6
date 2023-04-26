export class HttpliteServerConfiguration {
    port: number;
    serverName: string;

    constructor(props?: Partial<HttpliteServerConfiguration>) {
        const { port, serverName } = {
            port: 8888,
            serverName: "192.168.1.1",
            ...props,
        };
        this.port = port;
        this.serverName = serverName;
    }
}
