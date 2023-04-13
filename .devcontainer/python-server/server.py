# echo-server.py

import socket
import time

HOST = "127.0.0.1"  # Standard loopback interface address (localhost)
PORT = 8889  # Port to listen on (non-privileged ports are > 1023)

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen()
    print('Now listening on the socket!')
    time.sleep(2)
    print('Now accepting connections on the socket')
    conn, addr = s.accept()
    with conn:
        print(f"Connected by {addr}")
        while True:
            data = conn.recv(1024)
            if not data:
                break
            conn.sendall(data)
