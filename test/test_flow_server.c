#include "tju_tcp.h"

#define FLOW_TEST_BYTES (8 * 1024 * 1024)
#define FLOW_TEST_OUTPUT "/vagrant/tju_tcp/test/flow_recv.bin"
#define FLOW_TEST_PAUSE_SECONDS 15

int main(void)
{
    char buffer[8192];
    tju_sock_addr bind_addr;
    tju_tcp_t* listener;
    tju_tcp_t* conn;
    FILE* file;
    int total = 0;

    startSimulation();
    listener = tju_socket();
    if (listener == NULL)
        return EXIT_FAILURE;
    bind_addr.ip = inet_network("172.17.0.3");
    bind_addr.port = 1234;
    if (tju_bind(listener, bind_addr) != 0 || tju_listen(listener) != 0)
        return EXIT_FAILURE;
    conn = tju_accept(listener);
    if (conn == NULL)
        return EXIT_FAILURE;

    printf("flow server: pausing reads for %d seconds\n",
           FLOW_TEST_PAUSE_SECONDS);
    fflush(stdout);
    sleep(FLOW_TEST_PAUSE_SECONDS);

    file = fopen(FLOW_TEST_OUTPUT, "wb");
    if (file == NULL) {
        perror("open flow_recv.bin");
        return EXIT_FAILURE;
    }
    while (total < FLOW_TEST_BYTES) {
        int wanted = FLOW_TEST_BYTES - total;
        int received;
        if (wanted > (int)sizeof(buffer))
            wanted = (int)sizeof(buffer);
        received = tju_recv(conn, buffer, wanted);
        if (received <= 0) {
            fprintf(stderr, "flow server: receive stopped at %d bytes\n", total);
            fclose(file);
            return EXIT_FAILURE;
        }
        if (fwrite(buffer, 1, (size_t)received, file) != (size_t)received) {
            perror("write flow_recv.bin");
            fclose(file);
            return EXIT_FAILURE;
        }
        total += received;
        usleep(2000);
    }
    if (fclose(file) != 0) {
        perror("close flow_recv.bin");
        return EXIT_FAILURE;
    }

    printf("FLOW_SERVER_OK bytes=%d\n", total);
    sleep(2);
    return EXIT_SUCCESS;
}
