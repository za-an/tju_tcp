#include "tju_tcp.h"

#include <errno.h>
#include <time.h>

#define FLOW_TEST_BYTES (8 * 1024 * 1024)
#define FLOW_TEST_TIMEOUT_SECONDS 60
#define FLOW_TEST_OUTPUT "/vagrant/tju_tcp/test/flow_send.bin"

static int write_test_data(const unsigned char* data, size_t len)
{
    FILE* file = fopen(FLOW_TEST_OUTPUT, "wb");
    if (file == NULL)
        return -1;
    if (fwrite(data, 1, len, file) != len) {
        fclose(file);
        return -1;
    }
    return fclose(file);
}

static int wait_until_acked(tju_tcp_t* sock)
{
    struct timespec deadline;
    int complete;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += FLOW_TEST_TIMEOUT_SECONDS;

    pthread_mutex_lock(&sock->send_lock);
    while (sock->send_head != NULL) {
        int result = pthread_cond_timedwait(&sock->send_cond,
                                            &sock->send_lock, &deadline);
        if (result == ETIMEDOUT)
            break;
    }
    complete = sock->send_head == NULL;
    pthread_mutex_unlock(&sock->send_lock);
    return complete ? 0 : -1;
}

int main(void)
{
    unsigned char* data;
    tju_sock_addr target_addr;
    tju_tcp_t* sock;
    int index;
    int sent;

    data = malloc(FLOW_TEST_BYTES);
    if (data == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    for (index = 0; index < FLOW_TEST_BYTES; index++)
        data[index] = (unsigned char)(((unsigned int)index * 31U + 7U) % 251U);
    if (write_test_data(data, FLOW_TEST_BYTES) != 0) {
        perror("write flow_send.bin");
        free(data);
        return EXIT_FAILURE;
    }

    startSimulation();
    sock = tju_socket();
    if (sock == NULL) {
        free(data);
        return EXIT_FAILURE;
    }
    target_addr.ip = inet_network("172.17.0.3");
    target_addr.port = 1234;
    if (tju_connect(sock, target_addr) != 0) {
        fprintf(stderr, "flow client: connect failed\n");
        free(data);
        return EXIT_FAILURE;
    }

    /* Give the server time to enter its deliberate no-read interval. */
    sleep(1);
    sent = tju_send(sock, data, FLOW_TEST_BYTES);
    free(data);
    if (sent != FLOW_TEST_BYTES) {
        fprintf(stderr, "flow client: tju_send returned %d, expected %d\n",
                sent, FLOW_TEST_BYTES);
        return EXIT_FAILURE;
    }
    if (wait_until_acked(sock) != 0) {
        fprintf(stderr, "flow client: timed out waiting for acknowledgements\n");
        return EXIT_FAILURE;
    }

    printf("FLOW_CLIENT_OK bytes=%d\n", FLOW_TEST_BYTES);
    sleep(2);
    return EXIT_SUCCESS;
}
