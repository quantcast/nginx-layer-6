import { HttpliteConfiguration } from "./httplite-configuration";
import { NginxEventsConfiguration } from "./nginx-configuration-events";

export class NginxConfiguration {
    daemon: boolean;
    masterProcess: boolean;
    errorLog: string[];
    events: NginxEventsConfiguration;
    httplite: HttpliteConfiguration;

    constructor(props?: Partial<NginxConfiguration>) {
        const { daemon, masterProcess, errorLog, events, httplite } = {
            daemon: false,
            masterProcess: false,
            errorLog: ["logs/error.log", "debug"],
            events: new NginxEventsConfiguration(),
            httplite: new HttpliteConfiguration(),
            ...props,
        };
        this.daemon = daemon;
        this.masterProcess = masterProcess;
        this.errorLog = errorLog;
        this.events = events;
        this.httplite = httplite;
    }
}
