#include "transfer_common.h"

int main(int argc, char** argv)
{
    transfer_options_t opt;
    tju_sock_addr target;
    tju_tcp_t* sock;
    unsigned char* data;
    unsigned char ready;
    uint64_t offset = 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    double started, elapsed, deadline;
    if (transfer_options(argc, argv, &opt))
        return 2;
    deadline = transfer_now() + opt.timeout;
    data = malloc(opt.chunk);
    if (data == NULL)
        return 1;
    startSimulation();
    sock = tju_socket();
    if (sock == NULL)
        return 1;
    target.ip = inet_network("172.17.0.3");
    target.port = opt.port;
    if (tju_connect(sock, target) || tju_recv(sock, &ready, 1) != 1 || ready != 0xa5) {
        fprintf(stderr, "connection/readiness handshake failed\n");
        return 1;
    }
    started = transfer_now();
    while (offset < opt.bytes) {
        unsigned count = (unsigned)((opt.bytes - offset) < opt.chunk ? opt.bytes - offset : opt.chunk);
        unsigned i;
        if (transfer_wait_queue(sock, 256 * 1024, deadline)) {
            fprintf(stderr, "send queue timeout at byte %" PRIu64 "\n", offset);
            return 1;
        }
        for (i = 0; i < count; i++)
            data[i] = transfer_byte(offset + i);
        hash = transfer_hash(hash, data, count);
        if (tju_send(sock, data, (int)count) != (int)count) {
            fprintf(stderr, "send failed at byte %" PRIu64 "\n", offset);
            return 1;
        }
        offset += count;
    }
    free(data);
    if (transfer_wait_queue(sock, 0, deadline)) {
        fprintf(stderr, "final data ACK timeout\n");
        return 1;
    }
    elapsed = transfer_now() - started;
    if (tju_close(sock) || transfer_wait_closed(sock, deadline))
        return 1;
    transfer_result("client", offset, hash, elapsed);
    return 0;
}
