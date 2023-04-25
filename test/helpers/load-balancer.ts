import axios, { AxiosResponse } from "axios";
import { LoadBalancerProcess } from "./load-balancer-process";
import { NginxConfiguration } from "../models/nginx-configuration";
import { MockServer } from "./mock-server";
import { HttpliteUpstreamServer } from "../models/httplite-upstream-server";
import { NginxConfigurationService } from "./nginx-configuration-service";

export class LoadBalancer {
    private loadBalancerProcess: LoadBalancerProcess;
    private servers: MockServer[];
    private nginxConfigurationService: NginxConfigurationService;
    private configurationPath: string | null;
    private verbose: boolean;

    constructor(verbose: boolean = false) {
        this.verbose = verbose;
        this.nginxConfigurationService = new NginxConfigurationService();
        this.loadBalancerProcess = new LoadBalancerProcess(verbose);
        this.servers = [];
        this.configurationPath = null;
    }

    async open(configuration: NginxConfiguration) {
        this.servers = this.createServers(
            configuration.httplite.upstreams.servers
        );
        this.configurationPath =
            await this.nginxConfigurationService.writeConfiguration(
                configuration
            );
        await this.loadBalancerProcess.spawn(this.configurationPath);
        await Promise.all(this.servers.map((server) => server.open()));
    }

    private createServers(servers: HttpliteUpstreamServer[]) {
        return servers.map((server) => new MockServer(server.port, this.verbose));
    }

    getUpstreamDetails() {
        return this.servers.map((server) => server.getDetails());
    }

    ping(n: number = 1) {
        const requestPromises: Promise<AxiosResponse>[] = [];
        for (let i = 0; i < n; i++) {
            requestPromises.push(
                axios.post("http://localhost:8888", "ping", {
                    headers: {
                        Connection: "keep-alive",
                    },
                })
            );
        }
        return requestPromises;
    }

    pingAllConnections(upstreams: HttpliteUpstreamServer[]) {
        const totalConnections = upstreams.reduce<number>(
            (currentConnections, upstream) =>
                currentConnections + upstream.connections,
            0
        );
        return this.ping(totalConnections);
    }

    async close() {
        await this.loadBalancerProcess.kill();
        this.servers.forEach((server) => server.close());
        if (this.configurationPath) {
            // remove temp file
        }
    }
}
