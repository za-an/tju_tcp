/* Deterministic integration tests for the real transport send/ACK/RTO paths.
 * Build with tju_packet.c and tju_congestion.c, NOT a separate tju_tcp.o.
 * No UDP socket, timer thread, sleeps, trace files, or VM changes are needed.
 */
#include "tju_tcp.h"
#include <time.h>
#include <assert.h>
#include <inttypes.h>

static uint64_t test_clock_ms = 100000;

static int transport_clock_gettime(clockid_t clock_id, struct timespec* ts)
{
    (void)clock_id;
    ts->tv_sec = (time_t)(test_clock_ms / 1000);
    ts->tv_nsec = (long)((test_clock_ms % 1000) * 1000000);
    return 0;
}

/* Keep the production timer logic, replacing only its clock source. */
#define clock_gettime transport_clock_gettime
#include "../src/tju_tcp.c"
#undef clock_gettime

#define TEST_SMSS ((uint32_t)MAX_DLEN)
#define CAPTURE_LIMIT 128

typedef struct {
    uint32_t seq;
    uint32_t ack;
    uint16_t length;
    uint16_t window;
    uint8_t flags;
    unsigned char data[MAX_DLEN];
} captured_packet_t;

static captured_packet_t captured[CAPTURE_LIMIT];
static size_t captured_count;
static const char* current_test;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", current_test, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Capture the bytes the real encoder handed to the network boundary. */
void sendToLayer3(char* packet, int packet_len)
{
    captured_packet_t* item;
    uint16_t header_len = get_hlen(packet);
    CHECK(captured_count < CAPTURE_LIMIT);
    CHECK(packet_len == get_plen(packet));
    CHECK(header_len == DEFAULT_HEADER_LEN);
    CHECK(packet_len >= header_len);
    item = &captured[captured_count++];
    item->seq = get_seq(packet);
    item->ack = get_ack(packet);
    item->flags = get_flags(packet);
    item->length = (uint16_t)(packet_len - header_len);
    item->window = get_advertised_window(packet);
    CHECK(item->length <= MAX_DLEN);
    if (item->length)
        memcpy(item->data, packet + header_len, item->length);
}

int cal_hash(uint32_t local_ip, uint16_t local_port,
             uint32_t remote_ip, uint16_t remote_port)
{
    (void)local_ip;
    (void)local_port;
    (void)remote_ip;
    (void)remote_port;
    return 0;
}

static void socket_init(tju_tcp_t* sock, tju_cc_mode_t mode,
                        uint32_t start_seq, uint32_t cwnd, uint16_t rwnd)
{
    memset(sock, 0, sizeof(*sock));
    CHECK(pthread_mutex_init(&sock->send_lock, NULL) == 0);
    CHECK(pthread_mutex_init(&sock->recv_lock, NULL) == 0);
    CHECK(pthread_mutex_init(&sock->state_lock, NULL) == 0);
    CHECK(pthread_cond_init(&sock->wait_cond, NULL) == 0);
    CHECK(pthread_cond_init(&sock->state_cond, NULL) == 0);
    CHECK(pthread_cond_init(&sock->accept_cond, NULL) == 0);
    CHECK(pthread_cond_init(&sock->send_cond, NULL) == 0);
    sock->state = ESTABLISHED;
    sock->iss = start_seq;
    sock->snd_una = sock->snd_nxt = sock->snd_max = start_seq;
    sock->rcv_nxt = 7000;
    sock->peer_rwnd = rwnd;
    sock->cc_enabled = 1;
    sock->recv_capacity = TCP_RECVWN_SIZE;
    sock->rto_ms = TJU_INITIAL_RTO_MS;
    sock->established_local_addr.port = 5678;
    sock->established_remote_addr.port = 1234;
    tju_cc_init(&sock->cc, mode, MAX_DLEN, cwnd, 65535);
    captured_count = 0;
    test_clock_ms = 100000;
}

static void socket_destroy(tju_tcp_t* sock)
{
    while (sock->send_head) {
        tju_send_segment_t* next = sock->send_head->next;
        free_send_segment(sock->send_head);
        sock->send_head = next;
    }
    while (sock->recv_ooo_head) {
        tju_recv_segment_t* next = sock->recv_ooo_head->next;
        free_recv_segment(sock->recv_ooo_head);
        sock->recv_ooo_head = next;
    }
    free(sock->received_buf);
    free(sock->sending_buf);
    CHECK(pthread_cond_destroy(&sock->send_cond) == 0);
    CHECK(pthread_cond_destroy(&sock->accept_cond) == 0);
    CHECK(pthread_cond_destroy(&sock->state_cond) == 0);
    CHECK(pthread_cond_destroy(&sock->wait_cond) == 0);
    CHECK(pthread_mutex_destroy(&sock->state_lock) == 0);
    CHECK(pthread_mutex_destroy(&sock->recv_lock) == 0);
    CHECK(pthread_mutex_destroy(&sock->send_lock) == 0);
}

static void queue_data(tju_tcp_t* sock, unsigned int segments)
{
    unsigned int n;
    unsigned char payload[MAX_DLEN];
    pthread_mutex_lock(&sock->send_lock);
    for (n = 0; n < segments; n++) {
        unsigned int i;
        for (i = 0; i < MAX_DLEN; i++)
            payload[i] = (unsigned char)((n * 31U + i) % 251U);
        enqueue_segment_locked(sock, sock->snd_nxt, 0,
                               (const char*)payload, MAX_DLEN);
        sock->snd_nxt += MAX_DLEN;
    }
    pthread_mutex_unlock(&sock->send_lock);
}

static void queue_control(tju_tcp_t* sock, uint8_t flag)
{
    pthread_mutex_lock(&sock->send_lock);
    enqueue_segment_locked(sock, sock->snd_nxt, flag, NULL, 0);
    sock->snd_nxt++;
    pthread_mutex_unlock(&sock->send_lock);
}

/* Deliver encoded peer packets through the public receive path, including
 * duplicate-ACK qualification, rather than calling the controller directly. */
static void deliver_ack(tju_tcp_t* sock, uint32_t ack, uint16_t window,
                        uint8_t extra_flags, uint16_t payload_len)
{
    char payload[MAX_DLEN];
    char* packet;
    CHECK(payload_len <= MAX_DLEN);
    memset(payload, 0x5a, sizeof(payload));
    packet = create_packet_buf(1234, 5678, sock->rcv_nxt, ack,
                               DEFAULT_HEADER_LEN,
                               DEFAULT_HEADER_LEN + payload_len,
                               ACK_FLAG_MASK | extra_flags, window, 0,
                               payload_len ? payload : NULL, payload_len);
    CHECK(packet != NULL);
    CHECK(tju_handle_packet(sock, packet) == 0);
    free(packet);
}

static void advance_to_timeout(tju_tcp_t* sock, uint64_t at_ms)
{
    test_clock_ms = at_ms;
    pthread_mutex_lock(&sock->send_lock);
    process_timeout_locked(sock, now_ms());
    pthread_mutex_unlock(&sock->send_lock);
}

static void queued_ack_is_rejected(void)
{
    tju_tcp_t sock;
    socket_init(&sock, TJU_CC_BASIC, 1001, TEST_SMSS, 65535);
    queue_data(&sock, 3);
    flush_send_queue(&sock);
    CHECK(captured_count == 1);
    CHECK(sock.snd_max == 1001 + TEST_SMSS);
    CHECK(sock.snd_nxt == 1001 + 3 * TEST_SMSS);
    deliver_ack(&sock, sock.snd_nxt, 12345, 0, 0);
    CHECK(sock.snd_una == 1001);
    CHECK(sock.flight_size == TEST_SMSS);
    CHECK(sock.cc.cwnd == TEST_SMSS);
    CHECK(sock.peer_rwnd == 65535);
    CHECK(captured_count == 1);
    socket_destroy(&sock);
}

static void both_windows_limit_actual_transmission(void)
{
    tju_tcp_t sock;
    socket_init(&sock, TJU_CC_BASIC, 1001, 2 * TEST_SMSS, 65535);
    queue_data(&sock, 4);
    flush_send_queue(&sock);
    CHECK(captured_count == 2);
    CHECK(sock.flight_size == 2 * TEST_SMSS);
    CHECK(sock.send_tail->last_sent_ms == 0.0);
    socket_destroy(&sock);

    socket_init(&sock, TJU_CC_BASIC, 1001, 4 * TEST_SMSS,
                (uint16_t)(TEST_SMSS + 100));
    queue_data(&sock, 4);
    flush_send_queue(&sock);
    CHECK(captured_count == 1);
    CHECK(sock.flight_size == TEST_SMSS);
    deliver_ack(&sock, sock.snd_una, 2 * TEST_SMSS, 0, 0);
    CHECK(captured_count == 2);
    CHECK(sock.flight_size == 2 * TEST_SMSS);
    CHECK(sock.cc.dupacks == 0);
    CHECK(sock.cc.cwnd == 4 * TEST_SMSS);
    deliver_ack(&sock, sock.snd_una, 0, 0, 0);
    flush_send_queue(&sock);
    CHECK(captured_count == 2);
    deliver_ack(&sock, sock.snd_una, 4 * TEST_SMSS, 0, 0);
    CHECK(captured_count == 4);
    CHECK(sock.flight_size == 4 * TEST_SMSS);
    CHECK(sock.cc.dupacks == 0);
    socket_destroy(&sock);

    socket_init(&sock, TJU_CC_BASIC, 1001, TEST_SMSS, 0);
    queue_data(&sock, 1);
    flush_send_queue(&sock);
    CHECK(captured_count == 0);
    CHECK(sock.flight_size == 0);
    deliver_ack(&sock, sock.snd_una, TEST_SMSS, 0, 0);
    CHECK(captured_count == 1);
    CHECK(sock.flight_size == TEST_SMSS);
    CHECK(sock.cc.dupacks == 0);
    socket_destroy(&sock);
}

static void only_qualified_duplicate_acks_count(void)
{
    static const uint8_t flags[] = {0, SYN_FLAG_MASK, FIN_FLAG_MASK};
    static const uint16_t lengths[] = {1, 0, 0};
    tju_tcp_t sock;
    unsigned int n;
    for (n = 0; n < sizeof(flags) / sizeof(flags[0]); n++) {
        socket_init(&sock, TJU_CC_BASIC, 1001, 4 * TEST_SMSS, 65535);
        queue_data(&sock, 4);
        flush_send_queue(&sock);
        deliver_ack(&sock, sock.snd_una, 65534, 0, 0);
        CHECK(sock.cc.dupacks == 0);
        deliver_ack(&sock, sock.snd_una, 65534, flags[n], lengths[n]);
        CHECK(sock.cc.dupacks == 0);
        CHECK(sock.cc.cwnd == 4 * TEST_SMSS);
        CHECK(!sock.send_head->retransmitted);
        deliver_ack(&sock, sock.snd_una, 0, 0, 0);
        deliver_ack(&sock, sock.snd_una, 0, 0, 0);
        CHECK(sock.cc.dupacks == 0);
        socket_destroy(&sock);
    }
}

static void third_duplicate_retransmits_once_in_all_modes(void)
{
    tju_cc_mode_t mode;
    for (mode = TJU_CC_BASIC; mode <= TJU_CC_NEWRENO; mode++) {
        tju_tcp_t sock;
        unsigned int n;
        socket_init(&sock, mode, 1001, 4 * TEST_SMSS, 65535);
        queue_data(&sock, 4);
        flush_send_queue(&sock);
        deliver_ack(&sock, 1001, 65535, 0, 0);
        deliver_ack(&sock, 1001, 65535, 0, 0);
        CHECK(captured_count == 4);
        deliver_ack(&sock, 1001, 65535, 0, 0);
        CHECK(captured_count == 5);
        CHECK(captured[4].seq == captured[0].seq);
        CHECK(captured[4].length == captured[0].length);
        CHECK(memcmp(captured[4].data, captured[0].data, MAX_DLEN) == 0);
        CHECK(sock.flight_size == 4 * TEST_SMSS);
        CHECK(sock.snd_max == 1001 + 4 * TEST_SMSS);
        CHECK(sock.cc.ssthresh == 2 * TEST_SMSS);
        CHECK(sock.cc.cwnd == (mode == TJU_CC_BASIC ? 2 : 5) * TEST_SMSS);
        for (n = 0; n < 3; n++)
            deliver_ack(&sock, 1001, 65535, 0, 0);
        CHECK(captured_count == 5);
        deliver_ack(&sock, sock.snd_max, 65535, 0, 0);
        CHECK(sock.flight_size == 0);
        CHECK(sock.send_head == NULL);
        CHECK(sock.cc.state != TJU_CC_FAST_RECOVERY);
        CHECK(sock.retransmit_deadline_ms == 0.0);
        socket_destroy(&sock);
    }
}

static void reno_duplicate_ack_clock_sends_queued_data(void)
{
    tju_tcp_t sock;
    socket_init(&sock, TJU_CC_RENO, 1001, 4 * TEST_SMSS, 65535);
    queue_data(&sock, 6);
    flush_send_queue(&sock);
    CHECK(captured_count == 4);
    deliver_ack(&sock, 1001, 65535, 0, 0);
    deliver_ack(&sock, 1001, 65535, 0, 0);
    deliver_ack(&sock, 1001, 65535, 0, 0);
    CHECK(captured_count == 6);
    CHECK(captured[4].seq == 1001);
    CHECK(captured[5].seq == 1001 + 4 * TEST_SMSS);
    CHECK(sock.cc.recover == 1001 + 4 * TEST_SMSS);
    CHECK(sock.flight_size == 5 * TEST_SMSS);
    deliver_ack(&sock, 1001, 65535, 0, 0);
    CHECK(captured_count == 7);
    CHECK(captured[6].seq == 1001 + 5 * TEST_SMSS);
    CHECK(sock.flight_size == 6 * TEST_SMSS);
    deliver_ack(&sock, 1001 + TEST_SMSS, 65535, 0, 0);
    CHECK(sock.cc.state != TJU_CC_FAST_RECOVERY);
    CHECK(sock.cc.cwnd == 2 * TEST_SMSS);
    CHECK(sock.flight_size == 5 * TEST_SMSS);
    CHECK(captured_count == 7);
    socket_destroy(&sock);
}

static void newreno_partial_ack_retransmits_next_hole(void)
{
    tju_tcp_t sock;
    uint32_t recover;
    socket_init(&sock, TJU_CC_NEWRENO, 1001, 4 * TEST_SMSS, 65535);
    queue_data(&sock, 4);
    flush_send_queue(&sock);
    recover = sock.snd_max;
    deliver_ack(&sock, 1001, 65535, 0, 0);
    deliver_ack(&sock, 1001, 65535, 0, 0);
    deliver_ack(&sock, 1001, 65535, 0, 0);
    CHECK(captured_count == 5);
    test_clock_ms += 40;
    deliver_ack(&sock, 1001 + TEST_SMSS, 65535, 0, 0);
    CHECK(captured_count == 6);
    CHECK(captured[5].seq == 1001 + TEST_SMSS);
    CHECK(memcmp(captured[5].data, captured[1].data, MAX_DLEN) == 0);
    CHECK(sock.flight_size == 3 * TEST_SMSS);
    CHECK(sock.cc.state == TJU_CC_FAST_RECOVERY);
    CHECK(sock.cc.recover == recover);
    CHECK(sock.cc.ssthresh == 2 * TEST_SMSS);
    CHECK(sock.send_head->retransmitted);
    deliver_ack(&sock, recover, 65535, 0, 0);
    CHECK(captured_count == 6);
    CHECK(sock.flight_size == 0);
    CHECK(sock.send_head == NULL);
    CHECK(sock.cc.state != TJU_CC_FAST_RECOVERY);
    CHECK(sock.retransmit_deadline_ms == 0.0);
    socket_destroy(&sock);
}

static void rto_reduces_window_and_backs_off_once_per_deadline(void)
{
    tju_tcp_t sock;
    uint64_t deadline;
    socket_init(&sock, TJU_CC_BASIC, 1001, 4 * TEST_SMSS, 65535);
    queue_data(&sock, 4);
    flush_send_queue(&sock);
    deadline = (uint64_t)sock.retransmit_deadline_ms;
    CHECK(deadline == test_clock_ms + 1000);
    advance_to_timeout(&sock, deadline - 1);
    CHECK(captured_count == 4);
    CHECK(sock.cc.cwnd == 4 * TEST_SMSS);
    CHECK(sock.rto_ms == 1000.0);
    advance_to_timeout(&sock, deadline);
    CHECK(captured_count == 5);
    CHECK(captured[4].seq == 1001);
    CHECK(sock.cc.cwnd == TEST_SMSS);
    CHECK(sock.cc.ssthresh == 2 * TEST_SMSS);
    CHECK(sock.cc.state == TJU_CC_SLOW_START);
    CHECK(sock.flight_size == 4 * TEST_SMSS);
    CHECK(sock.rto_ms == 2000.0);
    CHECK(sock.retransmit_deadline_ms == deadline + 2000.0);
    advance_to_timeout(&sock, deadline + 1999);
    CHECK(captured_count == 5);
    advance_to_timeout(&sock, deadline + 2000);
    CHECK(captured_count == 6);
    CHECK(sock.rto_ms == 4000.0);
    CHECK(sock.cc.ssthresh == 2 * TEST_SMSS);
    CHECK(sock.retransmit_deadline_ms == deadline + 6000.0);
    socket_destroy(&sock);
}

static void partial_byte_ack_accounts_flight_and_retransmits_suffix(void)
{
    tju_tcp_t sock;
    socket_init(&sock, TJU_CC_BASIC, 1001, 2 * TEST_SMSS, 65535);
    queue_data(&sock, 2);
    flush_send_queue(&sock);
    test_clock_ms += 40;
    deliver_ack(&sock, 1101, 65535, 0, 0);
    CHECK(sock.snd_una == 1101);
    CHECK(sock.flight_size == 2 * TEST_SMSS - 100);
    CHECK(sock.send_head->seq == 1101);
    CHECK(sock.send_head->data_len == TEST_SMSS - 100);
    CHECK(sock.send_head->seq_len == TEST_SMSS - 100);
    CHECK(sock.cc.cwnd == 2 * TEST_SMSS + 100);
    CHECK(captured_count == 2);
    deliver_ack(&sock, 1001, 12345, 0, 0);
    CHECK(sock.peer_rwnd == 65535);
    CHECK(sock.flight_size == 2 * TEST_SMSS - 100);
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(captured_count == 3);
    CHECK(captured[2].seq == 1101);
    CHECK(captured[2].length == TEST_SMSS - 100);
    CHECK(memcmp(captured[2].data, captured[0].data + 100,
                 MAX_DLEN - 100) == 0);
    deliver_ack(&sock, sock.snd_max, 65535, 0, 0);
    CHECK(sock.flight_size == 0);
    CHECK(sock.send_head == NULL);
    socket_destroy(&sock);
}

static void ack_advancement_restarts_oldest_timer(void)
{
    tju_tcp_t sock;
    uint64_t old_deadline;
    socket_init(&sock, TJU_CC_BASIC, 1001, 2 * TEST_SMSS, 65535);
    queue_data(&sock, 2);
    flush_send_queue(&sock);
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(captured_count == 3);
    old_deadline = (uint64_t)sock.retransmit_deadline_ms;
    /* An ACK for retransmitted data has no RTT sample to shorten the RTO. */
    test_clock_ms += 1900;
    deliver_ack(&sock, 1001 + TEST_SMSS, 65535, 0, 0);
    CHECK(sock.retransmit_deadline_ms == test_clock_ms + 2000.0);
    advance_to_timeout(&sock, old_deadline);
    CHECK(captured_count == 3);
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(captured_count == 4);
    CHECK(captured[3].seq == 1001 + TEST_SMSS);
    socket_destroy(&sock);
}

static void sequence_wraparound_preserves_send_and_ack_accounting(void)
{
    tju_tcp_t sock;
    uint32_t initial = UINT32_MAX - 100;
    uint32_t first_end = initial + TEST_SMSS;
    socket_init(&sock, TJU_CC_BASIC, initial, 2 * TEST_SMSS, 65535);
    queue_data(&sock, 2);
    flush_send_queue(&sock);
    CHECK(captured_count == 2);
    CHECK(captured[0].seq == initial);
    CHECK(captured[1].seq == first_end);
    CHECK(sock.snd_max == initial + 2 * TEST_SMSS);
    deliver_ack(&sock, first_end, 65535, 0, 0);
    CHECK(sock.snd_una == first_end);
    CHECK(sock.flight_size == TEST_SMSS);
    CHECK(sock.send_head->seq == first_end);
    deliver_ack(&sock, initial, 12345, 0, 0);
    CHECK(sock.peer_rwnd == 65535);
    CHECK(sock.snd_una == first_end);
    deliver_ack(&sock, sock.snd_max, 65535, 0, 0);
    CHECK(sock.flight_size == 0);
    CHECK(sock.send_head == NULL);
    socket_destroy(&sock);
}

static void syn_and_fin_do_not_count_as_data_or_grow_cwnd(void)
{
    tju_tcp_t sock;
    uint32_t old_cwnd;
    socket_init(&sock, TJU_CC_BASIC, 1000, TEST_SMSS, 65535);
    sock.state = SYN_SENT;
    queue_control(&sock, SYN_FLAG_MASK);
    flush_send_queue(&sock);
    CHECK(captured_count == 1);
    CHECK(captured[0].flags == SYN_FLAG_MASK);
    CHECK(sock.flight_size == 0);
    CHECK(sock.snd_max == 1001);
    test_clock_ms += 40;
    deliver_ack(&sock, 1001, 65535, SYN_FLAG_MASK, 0);
    CHECK(sock.state == ESTABLISHED);
    CHECK(sock.cc.cwnd == TEST_SMSS);
    CHECK(sock.flight_size == 0);
    CHECK(sock.send_head == NULL);
    CHECK(sock.retransmit_deadline_ms == 0.0);
    socket_destroy(&sock);

    socket_init(&sock, TJU_CC_BASIC, 1001, TEST_SMSS, 65535);
    queue_data(&sock, 1);
    sock.fin_seq = sock.snd_nxt;
    queue_control(&sock, FIN_FLAG_MASK);
    sock.state = FIN_WAIT_1;
    flush_send_queue(&sock);
    CHECK(captured_count == 2);
    CHECK(captured[1].flags & FIN_FLAG_MASK);
    CHECK(sock.flight_size == TEST_SMSS);
    deliver_ack(&sock, sock.fin_seq, 65535, 0, 0);
    CHECK(sock.flight_size == 0);
    CHECK(sock.send_head != NULL && sock.send_head->data_len == 0);
    old_cwnd = sock.cc.cwnd;
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(captured_count == 3);
    CHECK(captured[2].flags & FIN_FLAG_MASK);
    CHECK(sock.cc.cwnd == old_cwnd);
    deliver_ack(&sock, sock.fin_seq + 1, 65535, 0, 0);
    CHECK(sock.state == FIN_WAIT_2);
    CHECK(sock.flight_size == 0);
    CHECK(sock.cc.cwnd == old_cwnd);
    CHECK(sock.send_head == NULL);
    CHECK(sock.retransmit_deadline_ms == 0.0);
    socket_destroy(&sock);
}

static void small_window_splits_without_stalling(void)
{
    tju_tcp_t sock;
    uint32_t start = 1001;
    socket_init(&sock, TJU_CC_NEWRENO, start, 4 * TEST_SMSS, 1024);
    queue_data(&sock, 1);
    flush_send_queue(&sock);
    CHECK(captured_count == 1 && captured[0].length == 1024);
    CHECK(sock.flight_size == 1024 && sock.snd_max == start + 1024);
    deliver_ack(&sock, start + 1024, 1024, 0, 0);
    CHECK(captured_count == 2 && captured[1].length == TEST_SMSS - 1024);
    deliver_ack(&sock, start + TEST_SMSS, 1024, 0, 0);
    CHECK(sock.flight_size == 0 && sock.send_head == NULL);
    socket_destroy(&sock);
}

static void out_of_order_ack_preserves_advertised_right_edge(void)
{
    tju_tcp_t sock;
    unsigned int i;
    char payload[MAX_DLEN];
    socket_init(&sock, TJU_CC_NEWRENO, 1001, TEST_SMSS, 65535);
    sock.recv_capacity = 8 * TEST_SMSS;
    memset(payload, 0x5a, sizeof(payload));
    for (i = 1; i <= 4; i++) {
        char* packet = create_packet_buf(1234, 5678, sock.rcv_nxt + i * TEST_SMSS,
            sock.snd_una, DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN + MAX_DLEN,
            ACK_FLAG_MASK, 65535, 0, payload, MAX_DLEN);
        CHECK(tju_handle_packet(&sock, packet) == 0);
        free(packet);
        CHECK(captured_count == i);
        CHECK(captured[i - 1].ack == 7000);
        CHECK(captured[i - 1].window == 8 * TEST_SMSS);
        CHECK(sock.recv_ooo_bytes == i * TEST_SMSS);
    }
    deliver_ack(&sock, sock.snd_una, 65535, 0, MAX_DLEN);
    CHECK(sock.rcv_nxt == 7000 + 5 * TEST_SMSS);
    CHECK(sock.received_len == 5 * MAX_DLEN && sock.recv_ooo_bytes == 0);
    CHECK(captured[captured_count - 1].window == 3 * TEST_SMSS);
    socket_destroy(&sock);
}

static void new_lost_segment_timeout_recalculates_threshold(void)
{
    tju_tcp_t sock;
    socket_init(&sock, TJU_CC_BASIC, 1001, 8 * TEST_SMSS, 65535);
    queue_data(&sock, 8);
    flush_send_queue(&sock);
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(sock.cc.ssthresh == 4 * TEST_SMSS);
    deliver_ack(&sock, 1001 + 2 * TEST_SMSS, 65535, 0, 0);
    CHECK(sock.flight_size == 6 * TEST_SMSS);
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(sock.cc.ssthresh == 3 * TEST_SMSS);
    advance_to_timeout(&sock, (uint64_t)sock.retransmit_deadline_ms);
    CHECK(sock.cc.ssthresh == 3 * TEST_SMSS);
    socket_destroy(&sock);
}

static void overlapping_delivery_discards_stale_reassembly(void)
{
    tju_tcp_t sock;
    char payload[200];
    socket_init(&sock, TJU_CC_BASIC, 1001, TEST_SMSS, 65535);
    memset(payload, 0x5a, sizeof(payload));
    insert_out_of_order_locked(&sock, sock.rcv_nxt + 100, payload, sizeof(payload));
    deliver_ack(&sock, sock.snd_una, 65535, 0, 400);
    CHECK(sock.received_len == 400 && sock.rcv_nxt == 7400);
    CHECK(sock.recv_ooo_head == NULL && sock.recv_ooo_bytes == 0);
    socket_destroy(&sock);
}

#define RUN(test) do { current_test = #test; test(); \
    printf("PASS %s\n", current_test); } while (0)

int main(void)
{
    RUN(queued_ack_is_rejected);
    RUN(both_windows_limit_actual_transmission);
    RUN(only_qualified_duplicate_acks_count);
    RUN(third_duplicate_retransmits_once_in_all_modes);
    RUN(reno_duplicate_ack_clock_sends_queued_data);
    RUN(newreno_partial_ack_retransmits_next_hole);
    RUN(rto_reduces_window_and_backs_off_once_per_deadline);
    RUN(partial_byte_ack_accounts_flight_and_retransmits_suffix);
    RUN(ack_advancement_restarts_oldest_timer);
    RUN(sequence_wraparound_preserves_send_and_ack_accounting);
    RUN(syn_and_fin_do_not_count_as_data_or_grow_cwnd);
    RUN(small_window_splits_without_stalling);
    RUN(out_of_order_ack_preserves_advertised_right_edge);
    RUN(new_lost_segment_timeout_recalculates_threshold);
    RUN(overlapping_delivery_discards_stale_reassembly);
    puts("PASS all transport/congestion integration tests");
    return EXIT_SUCCESS;
}
