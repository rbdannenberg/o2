import socket

UDP_IP = "127.0.0.1"
UDP_PORTS = [ 64541, 60238, 57143, 55764, 56975, 62711,
              57571, 53472, 51779, 63714, 53304, 61696,
              50665, 49404, 64828, 54859 ]

count = 1
for port in UDP_PORTS:
    sock = socket.socket(socket.AF_INET, # Internet
                         socket.SOCK_DGRAM) # UDP
    try:
        print count, port, sock.bind((UDP_IP, port))
        sock.close()
    except socket.error, e:
        print count, port, "could not bind"
    count = count + 1
