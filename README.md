# remote-cat
A TFTP based client to read and display the contents of a remote file on the local system's STDOUT.

## Introduction
TFTP is a simple file transfer protocol that uses UDP as its underlying protocol and enables a client to fetch a file from a remote server (host where TFTP is installed). It is designed to be small and easy to implement. Therefore, it lacks most of the features of a regular FTP. The only thing it can do is read and write files (or mail) from/to a remote server. Hence, we are going to use this property to our advantage and transfer files from servers and render the data on local STDOUT.

+ A client initiates a transfer by issuing a read request to read a file from a remote server. 
+ The server on accepting the request sends the file in fixed block lengths usually within a single IP packet. 
+ The client on receiving the packet must send an ACK before it can receive another block. 
+ The server sends numbered packets to the client and the client replies with the ACKs. 
+ The final data packet should contain less than full sized block to indicate that it is the final block.

## Architecture

    remcat Client --------------->  TFTP Server (file.txt)
                    RRQ                                           


    remcat Client <---------------  TFTP Server (file.txt)
     [STDOUT]         DAT (TFTP)                                
                       

### TFTP Packets

            2 bytes     string    1 byte     string   1 byte
            ------------------------------------------------
           | Opcode |  Filename  |   0  |    Mode    |   0  |
            --------------------------------------------------
                                RRQ/WRQ packet

                   2 bytes     2 bytes      n bytes
                   ----------------------------------
                  | Opcode |   Block #  |   Data     |
                   ----------------------------------
                                DATA packet

                        2 bytes     2 bytes
                         ---------------------
                        | Opcode |   Block #  |
                         ---------------------
                                ACK packet

                2 bytes     2 bytes      string   1 byte
               -----------------------------------------
              | Opcode |  ErrorCode |   ErrMsg   |   0  |
               -----------------------------------------
                                ERROR packet


## Build

Prerequisites
- CMake 3.20+
- A C++20 compiler (clang or gcc)

Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
I usually like to build in debug mode for easier debugging.
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Since I built and tested this code on the Apple Silicon chip, here is the preset
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build -j
```


Run help
```bash
./build/remcat --help
```

## Usage

CLI syntax
```bash
remcat <URL-or-path> [--bytes N | --head N] [-q|--quiet]
```

Examples
```bash
# Read a local file
./build/remcat ./README.md

# Read via TFTP (port 69 by default)
./build/remcat tftp://127.0.0.1/test.txt

# Read the first 16 bytes only
./build/remcat --head 16 tftp://127.0.0.1/test.txt

# Limit total bytes read
./build/remcat --bytes 1024 tftp://server/path/to/file.bin
```

## TFTP server quickstart (Docker)

This repo includes a simple `tftpd-hpa` image in `docker/Dockerfile` (serves files from `/var/tftpboot`).

Build the image
```bash
docker build -t local/tftpd-hpa -f docker/Dockerfile .
```

Run the server

- Linux (host networking available)
  ```bash
  docker rm -f tftpd 2>/dev/null || true
  docker run -d --name tftpd --network host \
    -v "$PWD/tftp:/var/tftpboot:ro" \
    local/tftpd-hpa
  # Image default CMD is roughly:
  #   in.tftpd --listen --address :69 --secure \
  #     --verbosity 5 --blocksize 1468 --port-range 30000:30010 /var/tftpboot
  ```

- macOS (Docker Desktop; host networking is NOT supported)
  ```bash
  # Publish UDP 69 and a pinned data port range; bind to IPv4 explicitly
  docker rm -f tftpd 2>/dev/null || true
  docker run -d --name tftpd \
    -p 6969:69/udp -p 30000-30010:30000-30010/udp \
    -v "$PWD/tftp:/var/tftpboot:ro" \
    local/tftpd-hpa \
    /usr/sbin/in.tftpd -L -vvv --address 0.0.0.0:69 -s \
      --port-range 30000:30010 /var/tftpboot
  ```

Notes (macOS Docker Desktop)
- `--network host` is not supported. Always publish UDP 69 and a data port range and use your Mac’s LAN IP instead of 127.0.0.1.
- Known issue: host→container TFTP may still time out due to UDP/vpnkit limitations. We have observed RRQs reaching the container and the server replying, but the host not receiving DATA/ERROR. Workarounds:
  - Test from a different machine on your LAN to your Mac’s IP and the published host port.
  - Or test container-to-container on the default bridge network (bypasses host UDP path).
  - Or avoid Docker Desktop for TFTP (use Linux host/VM or run a local Python TFTP server).

Test the server

- From another machine on your LAN (recommended on macOS):
  ```bash
  IP=$(ipconfig getifaddr en0 || ipconfig getifaddr en1)   # run on your Mac to get its LAN IP
  # On the other machine:
  curl -v --tftp-no-options "tftp://$IP:6969/test.txt" -o /tmp/x
  hexdump -C /tmp/x | head
  ```

- From another container (same Docker bridge):
  ```bash
  CID=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' tftpd)
  docker run --rm --network bridge busybox \
    sh -lc "tftp -g -r test.txt -l /tmp/x $CID 69 && hexdump -C /tmp/x | head"
  ```

Test with remcat

- Build remcat (see Build section above), then:
  - Linux (host network):
    ```bash
    ./build/remcat tftp://127.0.0.1/test.txt
    ```
  - macOS (Docker Desktop; may require another client machine):
    ```bash
    IP=$(ipconfig getifaddr en0 || ipconfig getifaddr en1)
    ./build/remcat "tftp://$IP:6969/test.txt"
    ```
    If this times out on macOS, run the same command from another machine on the LAN, or test container-to-container as shown above.

## References
    RFC1350 details: https://www.ietf.org/rfc/rfc1350.txt

## Legacy: xinetd tftpd (Linux only; not recommended)
These steps are kept for historical reference. Prefer the Docker quickstart above.
The development of remote-cat server (src/remcat_server.c) is still in progress. As a temporary workaround, you can download and install the tftpd server from a debian style package manager to test the client. (The existing client (src/remcat.c) works well with the tftpd server). Read the instructions below to download and install the TFTPD server until the (work in progress) server is fully developed.

### TFTPD Server Install and Setup
        1. Install following packages.

        sudo apt-get install xinetd tftpd tftp

        2. Create /etc/xinetd.d/tftp and put this entry

        service tftp
        {
        protocol        = udp
        port            = 69
        socket_type     = dgram
        wait            = yes
        user            = nobody
        server          = /usr/sbin/in.tftpd
        server_args     = /tftpboot
        disable         = no
        }

        3. Create a folder /tftpboot this should match whatever you gave in server_args. mostly it will be tftpboot

        sudo mkdir /tftpboot
        sudo chmod -R 777 /tftpboot
        sudo chown -R nobody /tftpboot

        4. Restart the xinetd service.

        sudo /etc/init.d/xinetd restart

        5. In addition, you can use the lsof command to verify if the server is up and running.

        sudo lsof -i | grep tftp
