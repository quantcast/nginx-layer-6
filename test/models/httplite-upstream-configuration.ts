import { HttpliteUpstreamServer } from "./httplite-upstream-server";

export class HttpliteUpstreamConfiguration {
    keepAliveTimeoutMiliseconds: number;
    servers: HttpliteUpstreamServer[];

    constructor(props?: Partial<HttpliteUpstreamConfiguration>) {
        const { keepAliveTimeoutMiliseconds, servers } = {
            keepAliveTimeoutMiliseconds: 1000,
            servers: [
                new HttpliteUpstreamServer({
                    ip: "127.0.0.1",
                    connections: 5,
                    port: 8889,
                }),
                new HttpliteUpstreamServer({
                    ip: "127.0.0.1",
                    connections: 5,
                    port: 8890,
                }),
                new HttpliteUpstreamServer({
                    ip: "127.0.0.1",
                    connections: 5,
                    port: 8891,
                }),
            ],
            ...props,
        };
        this.keepAliveTimeoutMiliseconds = keepAliveTimeoutMiliseconds;
        this.servers = servers;
    }
}
