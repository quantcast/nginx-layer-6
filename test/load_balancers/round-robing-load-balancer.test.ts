import { AxiosResponse } from "axios";
import { pingLoadBalancer } from "../helpers/request";
import { createPingUpstream } from "../helpers/server";

describe("Round Robin Load Balancer Test Suite", () => {
    const serverA = createPingUpstream(8889);
    const serverB = createPingUpstream(8890);
    const serverC = createPingUpstream(8891);

    beforeEach(async () => {
        await serverA.open();
        await serverB.open();
        await serverC.open();
    });

    afterEach(() => {
        serverA.close();
        serverB.close();
        serverC.close();
    });

    test("Shoud load balance 9 requests", async () => {
        const requests: Promise<AxiosResponse>[] = [];
        for (let i = 0; i < 12; i++) {
            requests.push(pingLoadBalancer("http://localhost:8888"));
        }
        await Promise.all(requests);

        expect(serverA.getDetails().requests).toEqual(4);
        expect(serverB.getDetails().requests).toEqual(4);
        expect(serverC.getDetails().requests).toEqual(4);
    });
});
