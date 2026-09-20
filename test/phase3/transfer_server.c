#include "transfer_common.h"

int main(int argc, char** argv)
{
    transfer_options_t opt;
    tju_sock_addr address;
    tju_tcp_t *listener, *conn;
    unsigned char* data;
    unsigned char ready = 0xa5;
    uint64_t offset = 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    double started, elapsed, deadline;
    FILE* output = NULL;
    if (transfer_options(argc, argv, &opt))
        return 2;
    deadline = transfer_now() + opt.timeout;
    data = malloc(opt.chunk);
    if (data == NULL)
        return 1;
    if (opt.output != NULL && (output = fopen(opt.output, "wb")) == NULL) {
        perror("open output");
        return 1;
    }
    startSimulation();
    listener = tju_socket();
    if (listener == NULL)
        return 1;
    address.ip = inet_network("172.17.0.3");
    address.port = opt.port;
    if (tju_bind(listener, address) || tju_listen(listener))
        return 1;
    puts("PHASE3_LISTENING");
    conn = tju_accept(listener);
    if (conn == NULL)
        return 1;
    /* The client waits for this readiness byte before sending any payload. */
    if (opt.recv_capacity) {
        pthread_mutex_lock(&conn->recv_lock);
        conn->recv_capacity = opt.recv_capacity;
        pthread_mutex_unlock(&conn->recv_lock);
    }
    started = transfer_now();
    if (tju_send(conn, &ready, 1) != 1 || transfer_wait_queue(conn, 0, deadline))
        return 1;
    if (opt.start_delay_ms)
        usleep(opt.start_delay_ms * 1000U);
    while (offset < opt.bytes) {
        unsigned wanted = (unsigned)((opt.bytes - offset) < opt.chunk ? opt.bytes - offset : opt.chunk);
        int count = tju_recv(conn, data, (int)wanted);
        int i;
        if (count <= 0) {
            fprintf(stderr, "unexpected receive result %d at byte %" PRIu64 "\n", count, offset);
            return 1;
        }
        for (i = 0; i < count; i++) {
            if (data[i] != transfer_byte(offset + (unsigned)i)) {
                fprintf(stderr, "data mismatch at byte %" PRIu64 ": got %u expected %u\n",
                        offset + (unsigned)i, data[i], transfer_byte(offset + (unsigned)i));
                return 1;
            }
        }
        hash = transfer_hash(hash, data, (size_t)count);
        if (output != NULL && fwrite(data, 1, (size_t)count, output) != (size_t)count) {
            perror("write output");
            return 1;
        }
        offset += (unsigned)count;
        if (opt.read_delay_ms && offset < opt.bytes)
            usleep(opt.read_delay_ms * 1000U);
    }
    elapsed = transfer_now() - started;
    /* EOF proves that the sender completed its half-close without extra bytes. */
    if (tju_recv(conn, data, 1) != 0) {
        fprintf(stderr, "expected EOF after the exact requested byte count\n");
        return 1;
    }
    free(data);
    if (output != NULL && fclose(output)) {
        perror("close output");
        return 1;
    }
    if (tju_close(conn) || transfer_wait_closed(conn, deadline))
        return 1;
    transfer_result("server", offset, hash, elapsed);
    return 0;
}
