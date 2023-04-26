import { HttpliteServerConfiguration } from "./httplite-server-configuration";
import { HttpliteUpstreamConfiguration } from "./httplite-upstream-configuration";

export class HttpliteConfiguration {
    sever: HttpliteServerConfiguration;
    upstreams: HttpliteUpstreamConfiguration;

    constructor(props?: Partial<HttpliteConfiguration>) {
        const { server, upstreams } = {
            server: new HttpliteServerConfiguration(),
            upstreams: new HttpliteUpstreamConfiguration(),
            ...props,
        };
        this.sever = server;
        this.upstreams = upstreams;
    }
}
