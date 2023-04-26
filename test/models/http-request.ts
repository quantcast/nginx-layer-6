export class HTTPRequest {
    method: string;
    path: string;
    headers: Record<string, string>;
    body: string;

    constructor(props?: Partial<HTTPRequest>) {
        const { path, method, headers, body } = {
            method: "GET",
            path: "/",
            headers: {},
            body: "",
            ...props,
        };
        this.path = path;
        this.method = method;
        this.body = body;
        this.headers = headers;
    }
}
