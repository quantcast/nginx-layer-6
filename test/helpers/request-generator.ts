import { HTTPRequest } from "../models/http-request";
import { NginxConfiguration } from "../models/nginx-configuration";

export class RequestGenerator {
    generateRequests(
        n: number,
        configuration: NginxConfiguration
    ): HTTPRequest[] {
        const requests: HTTPRequest[] = [];
        for (let i = 0; i < n; i++) {
            const random = Math.random();
            if (random < 0.5) {
                requests.push(this.generateGetRequest(configuration));
            } else {
                requests.push(this.generatePostRequest(i, configuration));
            }
        }
        return requests;
    }

    generatePostRequests(
        n: number,
        configuration: NginxConfiguration
    ): HTTPRequest[] {
        const requests: HTTPRequest[] = [];
        for (let i = 0; i < n; i++) {
            requests.push(this.generatePostRequest(i, configuration));
        }
        return requests;
    }

    private generatePostRequest(i: number, configuration: NginxConfiguration) {
        const payload = `${i + 1}body${i + 1}`;
        return new HTTPRequest({
            method: "POST",
            body: payload,
            headers: {
                Host: `localhost:${configuration.httplite.sever.port}`,
                Connection: "keep-alive",
                "Content-Type": "application/x-www-form-urlencoded",
                "Content-Length": Buffer.byteLength(payload).toString(),
            },
        });
    }

    private generateGetRequest(configuration: NginxConfiguration): HTTPRequest {
        return new HTTPRequest({
            method: "GET",
            headers: {
                Host: `localhost:${configuration.httplite.sever.port}`,
                Connection: "keep-alive",
                "User-Agent": "curl/7.85.0",
                Accept: "*/*",
                "Cache-Control": "*/*",
            },
        });
    }
}
