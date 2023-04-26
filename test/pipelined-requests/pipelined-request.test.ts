import { LoadBalancer } from "../helpers/load-balancer";
import { PipelineRequestClient } from "../helpers/pipeline-request-client";
import { RequestGenerator } from "../helpers/request-generator";
import { NginxConfiguration } from "../models/nginx-configuration";

describe("Pipelined Requests Test Suite", () => {
    const configuration = new NginxConfiguration();
    const loadBalancer = new LoadBalancer(configuration);
    const requestGenerator = new RequestGenerator();

    beforeEach(async () => {
        await loadBalancer.open();
    });

    afterEach(async () => {
        await loadBalancer.close();
    });

    test("Should distribute pipelined requests in a round robin fashion", async () => {
        const pipelineRequestClient = new PipelineRequestClient(loadBalancer);
        const numberOfRequests = 100;
        const requests = requestGenerator.generateRequests(
            numberOfRequests,
            configuration
        );
        const totalUpstreams = configuration.httplite.upstreams.servers.length;
        const minRequestsPerUpstream = Math.floor(
            numberOfRequests / totalUpstreams
        );
        const maxRequestsPerUpstream = Math.ceil(
            numberOfRequests / totalUpstreams
        );

        const responses = await pipelineRequestClient.pipelineRequests(
            requests
        );
        const details = loadBalancer.getUpstreamDetails();

        responses.forEach((response) => {
            expect(response.status).toEqual(200);
        });
        for (let i = 0; i < details.length; i++) {
            expect(details[i].requests).toBeGreaterThanOrEqual(
                minRequestsPerUpstream
            );
            expect(details[i].requests).toBeLessThanOrEqual(
                maxRequestsPerUpstream
            );
        }
    });
});
