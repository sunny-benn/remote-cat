FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends tftpd-hpa tcpdump \
    && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /var/tftpboot && chown -R nobody:nogroup /var/tftpboot
EXPOSE 69/udp 30000-30010/udp
CMD ["/usr/sbin/in.tftpd", "--listen", "--address", ":69", \
     "--secure", "--verbosity", "5", "--blocksize", "1468", \
     "--port-range", "30000:30010", "/var/tftpboot"]
