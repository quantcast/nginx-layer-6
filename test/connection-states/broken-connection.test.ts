import { IncomingMessage, ServerResponse } from "http";
import { LoadBalancer } from "../helpers/load-balancer";
import { MockServer } from "../helpers/mock-server";
import { NginxConfiguration } from "../models/nginx-configuration";

describe("Broken Connection Test Suite", () => {
    const configuration = new NginxConfiguration();
    const loadBalancer = new LoadBalancer(configuration);

    beforeEach(async () => {
        await loadBalancer.open();
    });

    afterEach(async () => {
        await loadBalancer.close();
    });

    test("Should not crash the load balancer when the client closes mid request", async () => {
        try {
            const abortController = new AbortController();

            const pingPromise = loadBalancer.ping(1, abortController);
            abortController.abort();
            await Promise.all(pingPromise);
            expect(true).toBeFalsy();
        } catch (error) {
            // verify load balancer hasn't crashed
            await Promise.all(loadBalancer.ping());
        }
    });

    test("Should return a 503 if an upstream crashes mid request", async () => {
        try {
            const requestHandler = (
                req: IncomingMessage,
                res: ServerResponse,
                server: MockServer
            ) => {
                req.on("data", () => {
                    req.destroy();
                    server.close();
                });
            };
            const servers = loadBalancer.getServers();
            servers.forEach((server) => {
                server.onRequest(requestHandler);
            });

            await Promise.all(loadBalancer.ping());
            expect(true).toBeFalsy();
        } catch (error) {
            if (!error.response || !error.response.status) {
                console.log(error);
                expect(true).toBeFalsy();
            }
            expect(error.response.status).toEqual(503);
        }
    });
});
