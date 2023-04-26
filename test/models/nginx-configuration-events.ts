export class NginxEventsConfiguration {
    workerConnections: number;

    constructor(props?: Partial<NginxEventsConfiguration>) {
        const { workerConnections } = {
            workerConnections: 512,
            ...props,
        };
        this.workerConnections = workerConnections;
    }
}
