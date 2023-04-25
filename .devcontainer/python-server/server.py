# echo-server.py

import argparse
import socket
import time

parser = argparse.ArgumentParser()
parser.add_argument('-p', '--port', type=int, default=8889)
parser.add_argument('-s', '--sleep', type=int, default=5)

args = parser.parse_args()

HOST = "127.0.0.1"  # Standard loopback interface address (localhost)
PORT = int(args.port)  # Port to listen on (non-privileged ports are > 1023)
SLEEP = args.sleep

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen()
    print('Now listening on the socket!')
    time.sleep(SLEEP)
    print('Now accepting connections on the socket')
    conn, addr = s.accept()
    with conn:
        print(f"Connected by {addr}")
        while True:
            data = conn.recv(1024)
            if not data:
                break
            conn.sendall(data)
