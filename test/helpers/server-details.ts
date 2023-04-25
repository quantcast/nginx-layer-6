import { ConnectionDetails } from "./connection-details";

export class ServerDetails {
    requests: number;
    connections: Promise<ConnectionDetails>[];

    constructor(props?: Partial<ServerDetails>) {
        const { requests, connections } = {
            requests: 0,
            connections: [],
            ...props,
        };
        this.requests = requests;
        this.connections = connections;
    }
}
