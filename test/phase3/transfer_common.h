#ifndef PHASE3_TRANSFER_COMMON_H
#define PHASE3_TRANSFER_COMMON_H

#include "tju_tcp.h"
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <time.h>

typedef struct {
    uint64_t bytes;
    unsigned timeout;
    unsigned chunk;
    unsigned recv_capacity;
    unsigned read_delay_ms;
    unsigned start_delay_ms;
    uint16_t port;
    const char* output;
} transfer_options_t;

static double transfer_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void transfer_alarm(int signo)
{
    const char message[] = "phase3 transfer: whole-process timeout\n";
    (void)signo;
    ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    _exit(124);
}

static int transfer_options(int argc, char** argv, transfer_options_t* opt)
{
    int i;
    memset(opt, 0, sizeof(*opt));
    opt->bytes = UINT64_C(100) * 1024 * 1024;
    opt->timeout = 600;
    opt->chunk = 64 * 1024;
    opt->port = 1234;
    for (i = 1; i < argc; i++) {
        char* end;
        unsigned long long value;
        const char* key = argv[i];
        if (!strcmp(key, "--help")) {
            printf("%s [--bytes N] [--timeout seconds] [--chunk N] [--port N]\n"
                   "  server: [--recv-capacity N] [--read-delay-ms N]\n"
                   "          [--start-delay-ms N] [--output FILE]\n", argv[0]);
            exit(0);
        }
        if (++i >= argc) {
            fprintf(stderr, "missing value for %s\n", key);
            return -1;
        }
        if (!strcmp(key, "--output")) {
            opt->output = argv[i];
            continue;
        }
        errno = 0;
        value = strtoull(argv[i], &end, 10);
        if (errno || *end || argv[i][0] == '-') {
            fprintf(stderr, "invalid numeric value for %s\n", key);
            return -1;
        }
        if (!strcmp(key, "--bytes") && value > 0 && value <= 1024ULL * 1024 * 1024)
            opt->bytes = (uint64_t)value;
        else if (!strcmp(key, "--timeout") && value > 0 && value <= 86400)
            opt->timeout = (unsigned)value;
        else if (!strcmp(key, "--chunk") && value > 0 && value <= 1024 * 1024)
            opt->chunk = (unsigned)value;
        else if (!strcmp(key, "--port") && value > 0 && value <= 65535)
            opt->port = (uint16_t)value;
        else if (!strcmp(key, "--recv-capacity") && value > 0 && value <= TCP_RECVWN_SIZE)
            opt->recv_capacity = (unsigned)value;
        else if (!strcmp(key, "--read-delay-ms") && value <= 60000)
            opt->read_delay_ms = (unsigned)value;
        else if (!strcmp(key, "--start-delay-ms") && value <= 600000)
            opt->start_delay_ms = (unsigned)value;
        else {
            fprintf(stderr, "unknown or out-of-range option: %s %s\n", key, argv[i]);
            return -1;
        }
    }
    signal(SIGALRM, transfer_alarm);
    alarm(opt->timeout);
    setvbuf(stdout, NULL, _IOLBF, 0);
    return 0;
}

/* Absolute-offset pattern catches missing, duplicated and reordered bytes.
 * FNV-1a below is a reproducible diagnostic, not a cryptographic checksum. */
static unsigned char transfer_byte(uint64_t offset)
{
    uint64_t value = offset + UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (unsigned char)(value ^ (value >> 31));
}

static uint64_t transfer_hash(uint64_t hash, const unsigned char* data, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    return hash;
}

/* snd_nxt - snd_una intentionally includes queued but unsent bytes here:
 * this bounds application memory; it is NOT the congestion FlightSize. */
static int transfer_wait_queue(tju_tcp_t* sock, uint32_t limit, double deadline)
{
    int result = 0;
    pthread_mutex_lock(&sock->send_lock);
    while ((uint32_t)(sock->snd_nxt - sock->snd_una) > limit) {
        struct timespec wake;
        if (transfer_now() >= deadline) {
            result = -1;
            break;
        }
        clock_gettime(CLOCK_REALTIME, &wake);
        wake.tv_sec += 1;
        pthread_cond_timedwait(&sock->send_cond, &sock->send_lock, &wake);
    }
    pthread_mutex_unlock(&sock->send_lock);
    return result;
}

static int transfer_wait_closed(tju_tcp_t* sock, double deadline)
{
    int state;
    do {
        pthread_mutex_lock(&sock->state_lock);
        state = sock->state;
        pthread_mutex_unlock(&sock->state_lock);
        if (state == CLOSED)
            return 0;
        usleep(10000);
    } while (transfer_now() < deadline);
    fprintf(stderr, "close timed out in state %d\n", state);
    return -1;
}

static void transfer_result(const char* role, uint64_t bytes, uint64_t hash, double elapsed)
{
    const char* mode = getenv("TJU_CC");
    if (mode == NULL || (strcmp(mode, "basic") && strcmp(mode, "reno") && strcmp(mode, "newreno")))
        mode = "basic";
    printf("PHASE3_RESULT {\"role\":\"%s\",\"ok\":true,\"bytes\":%" PRIu64
           ",\"fnv1a64\":\"%016" PRIx64 "\",\"seconds\":%.9f,"
           "\"goodput_mbps\":%.9f,\"cc\":\"%s\",\"smss\":%d,\"closed\":true}\n",
           role, bytes, hash, elapsed, elapsed > 0 ? bytes * 8.0 / elapsed / 1e6 : 0,
           mode, MAX_DLEN);
}

#endif
