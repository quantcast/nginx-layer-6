import { ChildProcess, exec, spawn } from "child_process";

export class LoadBalancerProcess {
    private process: ChildProcess | null;
    private verbose: boolean;

    constructor(verbose: boolean = false) {
        this.verbose = verbose;
    }

    spawn(configurationPath: string) {
        return new Promise<void>(async (resolve, reject) => {
            this.process = spawn(
                "./scripts/run",
                ["--nginx-config", configurationPath],
                {
                    detached: true,
                }
            );
            this.process.on("error", (error) => reject(error));
            this.process.stderr?.on("data", (data) => {
                if (this.verbose) {
                    console.log(data.toString());
                }
            });
            this.process.stdout?.on("data", (data) => {
                if (this.verbose) {
                    console.log(data.toString());
                }
                if (data.toString().includes("Starting nginx")) {
                    resolve();
                }
            });
        });
    }

    kill() {
        return new Promise<void>(async (resolve, reject) => {
            if (this.process) {
                this.process.kill();
                exec("pkill nginx", (error) => {
                    if (error) {
                        return reject(error);
                    }
                    return resolve();
                });
            } else {
                resolve();
            }
        });
    }
}
