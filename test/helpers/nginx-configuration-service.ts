import { promises } from "fs";
import { HttpliteUpstreamServer } from "../models/httplite-upstream-server";
import { NginxConfiguration } from "../models/nginx-configuration";
import path, { dirname } from "path";
import { tmpdir } from "os";

export class NginxConfigurationService {
    async deleteConfiguration(path: string) {
        const dirPath = dirname(path);
        await promises.rm(dirPath, {
            recursive: true,
            force: true,
        });
    }

    async writeConfiguration(configuration: NginxConfiguration) {
        const formattedConfiguration = this.formatConfiguration(configuration);
        const tempDirectoryPath = await promises.mkdtemp(
            path.join(tmpdir(), "httplite-")
        );
        const configurationPath = `${tempDirectoryPath}/nginx.conf`;
        await promises.writeFile(
            configurationPath,
            formattedConfiguration,
            "utf-8"
        );
        return configurationPath;
    }

    private formatConfiguration(configuration: NginxConfiguration) {
        return `daemon ${this.formatFlag(configuration.daemon)};
master_process ${this.formatFlag(configuration.masterProcess)};
error_log ${configuration.errorLog.join(" ")};

events {
    worker_connections ${configuration.events.workerConnections};
}

httplite {
    server {
        listen ${configuration.httplite.sever.port};
        server_name ${configuration.httplite.sever.serverName};
    }

    upstreams {
        keep_alive ${
            configuration.httplite.upstreams.keepAliveTimeoutMiliseconds
        };
${this.formatServers(configuration.httplite.upstreams.servers)}
    }
}
`;
    }

    private formatFlag(flag: boolean) {
        return flag ? "on" : "off";
    }

    private formatServers(servers: HttpliteUpstreamServer[]) {
        return servers
            .map(
                (server) =>
                    `        server ${server.ip}:${server.port} connections=${server.connections};`
            )
            .join("\n");
    }
}
