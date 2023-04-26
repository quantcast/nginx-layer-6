export class HTTPResponse {
    public status: number;
    public headers: Record<string, string>;
    public body: string;

    constructor(props?: Partial<HTTPResponse>) {
        const { status, headers, body } = {
            status: 200,
            headers: {},
            body: "",
            ...props,
        };
        this.status = status;
        this.headers = headers;
        this.body = body;
    }
}
