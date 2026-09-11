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

#define SIZE32 4
#define SIZE16 2
#define SIZE8  1

#define NO_FLAG 0
#define NO_WAIT 1
#define TIMEOUT 2
#define TRUE 1
#define FALSE 0

/* 课程框架的报文限制，第二阶段沿用原有 1375 字节 MSS。 */
#define MAX_DLEN 1375
#define MAX_LEN 1400

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

/* RFC 6298 定时参数，单位为毫秒。 */
#define TJU_INITIAL_RTO_MS 1000.0
#define TJU_MIN_RTO_MS 200.0
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
