import { ConnectionDetails } from "../helpers/connection-details";
import { LoadBalancer } from "../helpers/load-balancer";
import { NginxConfiguration } from "../models/nginx-configuration";

describe("Keep Alive Test Suite", () => {
    const loadBalancer = new LoadBalancer();
    const configuration = new NginxConfiguration();
    const keepAliveTimeout =
        configuration.httplite.upstreams.keepAliveTimeoutMiliseconds;
    const upstreams = configuration.httplite.upstreams.servers;
    const TimeoutGraceMiliseconds = 5;

    beforeEach(async () => {
        await loadBalancer.open(configuration);
    });

    afterEach(async () => {
        await loadBalancer.close();
    });

    test("Should keep a connection alive for 1 second", async () => {
        await Promise.all(loadBalancer.ping(upstreams.length));
        const connectionDetails = await waitForConnectionsToClose();

        for (const connectionDetail of connectionDetails) {
            if (!connectionDetail.timeClosed) {
                throw new Error("Connection never closed");
            }
            expect(
                connectionDetail.timeClosed.getTime() -
                    connectionDetail.timeOpened.getTime()
            ).toBeGreaterThanOrEqual(
                keepAliveTimeout - TimeoutGraceMiliseconds
            );
        }
    });

    async function waitForConnectionsToClose() {
        const connectionDetails: Promise<ConnectionDetails>[] = [];
        for (const detail of loadBalancer.getUpstreamDetails()) {
            connectionDetails.push(...detail.connections);
        }
        return await Promise.all(connectionDetails);
    }

    test("Should reuse a connection for multiple requests", async () => {
        await Promise.all(loadBalancer.ping(upstreams.length));
        await Promise.all(loadBalancer.ping(upstreams.length));

        await waitForConnectionsToClose();
        const details = loadBalancer.getUpstreamDetails();

        for (const upstreamDetail of details) {
            expect(upstreamDetail.connections).toHaveLength(1);
        }
    });

    test("Should create a new connection for requests that are sent after the keep alive expires", async () => {
        await Promise.all(loadBalancer.ping(upstreams.length));
        await new Promise((resolve) => setTimeout(resolve, 1100));
        await Promise.all(loadBalancer.ping(upstreams.length));

        await waitForConnectionsToClose();
        const details = loadBalancer.getUpstreamDetails();

        for (const upstreamDetail of details) {
            expect(upstreamDetail.connections).toHaveLength(2);
        }
    });
});
