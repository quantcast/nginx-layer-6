import { Socket } from "net";

export class ConnectionDetails {
    socket: Socket;
    timeOpened: Date;
    timeClosed: Date | null;

    constructor(props?: Partial<ConnectionDetails>) {
        const { socket, timeClosed, timeOpened } = {
            socket: new Socket(),
            timeOpened: new Date(),
            timeClosed: null,
            ...props,
        };
        this.socket = socket;
        this.timeClosed = timeClosed;
        this.timeOpened = timeOpened;
    }
}
