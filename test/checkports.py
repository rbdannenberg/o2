import socket
import sys

UDP_IP = "127.0.0.1"
UDP_PORTS = [ 64541, 60238, 57143, 55764, 56975, 62711,
              57571, 53472, 51779, 63714, 53304, 61696,
              50665, 49404, 64828, 54859 ]

def countports(verbose):
    free_count = 0
    count = 1
    for port in UDP_PORTS:
        sock = socket.socket(socket.AF_INET, # Internet
                            socket.SOCK_DGRAM) # UDP
        try:
            result = sock.bind((UDP_IP, port))
            if verbose:
                print(count, port)
            if result:
                print("Bind returned", result)
            sock.close()
            free_count += 1
        except socket.error:
            if verbose:
                print(count, port, "could not bind")
        count = count + 1
    if verbose:
        print("->", free_count, "ports are free now")
    return free_count


def checkports(start, verbose):
    expected_count = None
    if not start:
        with open("port_count.dat", "r") as inf:
            expected_count = int(inf.readline())
    free_count = countports(verbose)
    countmsg = "(" + str(free_count) + " ports)"
    if expected_count and expected_count != free_count:
        if verbose:
            print("ERROR: Expected", expected_count, \
                  "free ports, but found", free_count)
        return False, countmsg
    elif verbose and not start:
        print("OK: found expected", free_count, "ports")
    with open("port_count.dat", "w") as outf:
        outf.write(str(free_count) + "\n")
        return True, countmsg


def main():
    verbose = False
    if len(sys.argv) > 1:
        start = sys.argv[1] == "start"
        if len(sys.argv) > 2 and sys.argv[2] == "v":
            verbose = True
    else:
        verbose = True
        start = True
    if len(sys.argv) > 1 and "h" in sys.argv[1]:
        print("Usage: python checkports [<start> [<verbose>]]")
        print("   where <start> is start to set port_count.dat or no")
        print("   and <verbose> is v to print details.")
        print("   Both are optional and positional.")
    checkports(start, verbose)


if __name__ == '__main__':
    main()
