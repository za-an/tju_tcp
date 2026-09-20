#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include "tju_congestion.h"

/* The teaching VMs use .2/.3 locally; the online grader uses .5/.6. Resolve
 * the address from the actual 172.17.0.x interface so the same source works
 * in both environments. TJU_LOCAL_IP/TJU_REMOTE_IP remain available for
 * unusual runner setups. */
static inline uint32_t tju_local_ip(void)
{
    const char* configured = getenv("TJU_LOCAL_IP");
    struct ifaddrs* list = NULL;
    struct ifaddrs* cur;
    uint32_t result = 0;
    if (configured != NULL && configured[0] != '\0')
        return inet_network(configured);
    if (getifaddrs(&list) == 0) {
        for (cur = list; cur != NULL; cur = cur->ifa_next) {
            struct sockaddr_in* address;
            uint32_t host_address;
            if (cur->ifa_addr == NULL || cur->ifa_addr->sa_family != AF_INET)
                continue;
            address = (struct sockaddr_in*)cur->ifa_addr;
            host_address = ntohl(address->sin_addr.s_addr);
            if ((host_address & UINT32_C(0xffffff00)) == UINT32_C(0xac110000)) {
                /* Keep the same host-order representation as inet_network(),
                 * which is used by tju_sock_addr and the hash tables. */
                result = ntohl(address->sin_addr.s_addr);
                break;
            }
        }
        freeifaddrs(list);
    }
    if (result != 0)
        return result;
    {
        char hostname[64] = {0};
        gethostname(hostname, sizeof(hostname) - 1);
        return inet_network(strcmp(hostname, "server") == 0
                                ? "172.17.0.3" : "172.17.0.2");
    }
}

static inline uint32_t tju_peer_ip(uint32_t local_ip)
{
    const char* configured = getenv("TJU_REMOTE_IP");
    uint32_t host_address;
    if (configured != NULL && configured[0] != '\0')
        return inet_network(configured);
    /* local_ip is already in the host-order representation returned by
     * inet_network()/tju_local_ip(). */
    host_address = local_ip;
    switch (host_address & 0xffU) {
    case 2: return inet_network("172.17.0.3");
    case 3: return inet_network("172.17.0.2");
    case 5: return inet_network("172.17.0.6");
    case 6: return inet_network("172.17.0.5");
    default: return 0;
    }
}

#define SIZE32 4
#define SIZE16 2
#define SIZE8  1

#define NO_FLAG 0
#define NO_WAIT 1
#define TIMEOUT 2
#define TRUE 1
#define FALSE 0

#define MAX_LEN 1400
/* v3: 1400-byte packet minus the fixed 20-byte header. Legacy peers can
   explicitly select -DTJU_SMSS=1375 without changing trace byte units. */
#ifndef TJU_SMSS
#define TJU_SMSS 1380
#endif
#if TJU_SMSS < 1 || TJU_SMSS > 1380
#error "TJU_SMSS must fit the 1400-byte packet and 20-byte header"
#endif
#define MAX_DLEN TJU_SMSS

#define CLOSED 0
#define LISTEN 1
#define SYN_SENT 2
#define SYN_RECV 3
#define ESTABLISHED 4
#define FIN_WAIT_1 5
#define FIN_WAIT_2 6
#define CLOSE_WAIT 7
#define CLOSING 8
#define LAST_ACK 9
#define TIME_WAIT 10

#define SLOW_START 0
#define CONGESTION_AVOIDANCE 1
#define FAST_RECOVERY 2

/* 接收缓存按课程要求预留至少 5000 个满载数据段。 */
#define TCP_RECVWN_SIZE (5000 * MAX_DLEN)

/* Conservative RFC 5681 defaults; course-specific settings can override these. */
#ifndef TJU_INITIAL_CWND
#define TJU_INITIAL_CWND MAX_DLEN
#endif
#ifndef TJU_INITIAL_SSTHRESH
#define TJU_INITIAL_SSTHRESH 65535U
#endif

/* RFC 6298 定时参数，单位为毫秒。 */
#define TJU_INITIAL_RTO_MS 1000.0
#define TJU_MIN_RTO_MS 1000.0
#define TJU_MAX_RTO_MS 4000.0
#define TJU_TIMER_GRANULARITY_MS 10.0
#define TJU_MSL_MS 1000.0

typedef struct {
    uint16_t window_size;
} sender_window_t;

typedef struct {
    char received[TCP_RECVWN_SIZE];
} receiver_window_t;

typedef struct {
    sender_window_t* wnd_send;
    receiver_window_t* wnd_recv;
} window_t;

typedef struct {
    uint32_t ip;
    uint16_t port;
} tju_sock_addr;

/* 发送队列中的一个数据段或控制段。 */
typedef struct tju_send_segment {
    uint32_t seq;
    uint32_t seq_len;
    uint16_t data_len;
    uint8_t flags;
    char* data;
    double first_sent_ms;
    double last_sent_ms;
    int retransmitted;
    int rto_retransmitted;
    int rtt_sampled;
    int sack_retransmitted;
    struct tju_send_segment* next;
} tju_send_segment_t;

/* 失序接收队列节点，按序号升序排列。 */
typedef struct tju_recv_segment {
    uint32_t seq;
    uint32_t len;
    char* data;
    struct tju_recv_segment* next;
} tju_recv_segment_t;

typedef struct tju_tcp tju_tcp_t;

/* TJU_TCP 内部状态。公共 API 签名保持不变。 */
struct tju_tcp {
    int state;
    tju_sock_addr bind_addr;
    tju_sock_addr established_local_addr;
    tju_sock_addr established_remote_addr;

    pthread_mutex_t send_lock;
    char* sending_buf;
    int sending_len;
    pthread_mutex_t recv_lock;
    char* received_buf;
    int received_len;
    pthread_cond_t wait_cond;
    window_t window;

    /* 状态锁保护连接状态和 accept 完成队列，避免复制 pthread 对象。 */
    pthread_mutex_t state_lock;
    pthread_cond_t state_cond;
    pthread_cond_t accept_cond;
    pthread_cond_t send_cond;

    uint32_t iss;
    uint32_t irs;
    uint32_t snd_una;
    uint32_t snd_nxt;
    /* snd_nxt reserves queued sequence space; snd_max advances only on wire. */
    uint32_t snd_max;
    uint32_t flight_size;
    tju_cc_t cc;
    int cc_enabled;
    int rack_enabled;
    int checksum_enabled;
    int sack_enabled;
    int tlp_sent;
    double rack_reordering_ms;
    uint32_t sack_blocks[4][2];
    unsigned int sack_block_count;
    uint32_t sack_rx_blocks[4][2];
    unsigned int sack_rx_count;
    uint32_t rcv_nxt;
    uint32_t fin_seq;
    uint16_t peer_rwnd;

    tju_send_segment_t* send_head;
    tju_send_segment_t* send_tail;
    tju_recv_segment_t* recv_ooo_head;
    size_t recv_ooo_bytes;
    size_t recv_capacity;
    size_t recv_head;
    size_t recv_tail;

    double srtt_ms;
    double rttvar_ms;
    double rto_ms;
    int rtt_initialized;
    uint32_t last_ack_seen;
    int duplicate_ack_count;

    pthread_t timer_thread;
    int timer_started;
    int timer_stop;
    double last_probe_ms;
    double retransmit_deadline_ms;
    double persist_deadline_ms;
    double persist_interval_ms;
    double last_peer_response_ms;
    int syn_retransmitted;
    double time_wait_deadline_ms;

    int closing_requested;
    int recv_eof;
    int peer_fin_pending;
    uint32_t peer_fin_seq;

    tju_tcp_t* parent_listener;
    tju_tcp_t* accept_head;
    tju_tcp_t* accept_tail;
    tju_tcp_t* accept_next;
};

#endif
