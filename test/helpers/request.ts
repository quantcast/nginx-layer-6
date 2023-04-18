import axios from "axios";

export function pingLoadBalancer(url: string) {
    return axios.post(url, "ping");
}
