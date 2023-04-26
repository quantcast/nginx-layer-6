import { LoadBalancer } from "../helpers/load-balancer";
import { NginxConfiguration } from "../models/nginx-configuration";

describe("Round Robin Load Balancer Test Suite", () => {
    const configuration = new NginxConfiguration();
    const loadBalancer = new LoadBalancer(configuration);
    const upstreams = configuration.httplite.upstreams.servers;

    beforeEach(async () => {
        await loadBalancer.open();
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

    test("Should handle using more than the available connections", async () => {
        const totalConnections = upstreams.reduce<number>(
            (currentConnections, upstream) =>
                currentConnections + upstream.connections,
            0
        );
        const pingFactor = 3;
        const serverPings = pingFactor * totalConnections + 1;

        await Promise.all(loadBalancer.ping(serverPings));
        const details = loadBalancer.getUpstreamDetails();

        for (let i = 0; i < details.length; i++) {
            expect(details[i].requests).toBeGreaterThanOrEqual(
                pingFactor * upstreams[i].connections
            );
            expect(details[i].requests).toBeLessThanOrEqual(
                pingFactor * upstreams[i].connections + 1
            );
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
