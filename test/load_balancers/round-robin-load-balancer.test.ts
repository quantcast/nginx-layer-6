import { LoadBalancer } from "../helpers/load-balancer";
import { NginxConfiguration } from "../models/nginx-configuration";

describe("Round Robin Load Balancer Test Suite", () => {
    const loadBalancer = new LoadBalancer();
    const configuration = new NginxConfiguration();
    const upstreams = configuration.httplite.upstreams.servers;

    beforeEach(async () => {
        await loadBalancer.open(configuration);
    });

    afterEach(async () => {
        await loadBalancer.close();
    });

    test("Should load balance requests in parallel using all available connections", async () => {
        await Promise.all(loadBalancer.pingAllConnections(upstreams));

        const details = loadBalancer.getUpstreamDetails();

        for (let i = 0; i < details.length; i++) {
            expect(details[i].requests).toEqual(upstreams[i].connections);
        }
    });

    test("Should fail due to using more than the available connections", async () => {
        const totalConnections = upstreams.reduce<number>(
            (currentConnections, upstream) =>
                currentConnections + upstream.connections,
            0
        );

        await Promise.all(loadBalancer.ping(totalConnections + 1));
        const details = loadBalancer.getUpstreamDetails();

        for (let i = 0; i < details.length; i++) {
            expect(details[i].requests).toBeGreaterThanOrEqual(upstreams[i].connections);
        }
    });

    test("Should load balance bursts of requests in parallel using all available connections", async () => {
        const bursts = Math.floor(Math.random() * 100) + 5;
        for (let j = 0; j < bursts; j++) {
            await Promise.all(loadBalancer.pingAllConnections(upstreams));
        }

        const details = loadBalancer.getUpstreamDetails();

        for (let i = 0; i < details.length; i++) {
            expect(details[i].requests).toEqual(
                upstreams[i].connections * bursts
            );
        }
    });
});
