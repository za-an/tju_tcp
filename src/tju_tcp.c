#include "tju_tcp.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

/* 课程协议没有校验和字段；这里仅做长度和序号范围检查。 */

/* Trace 文件记录说明 v2：每条记录独占一行，写入采用进程级互斥锁。 */
static FILE* trace_fp;
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t trace_timestamp_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void trace_write(const char* event, const char* fmt, ...)
{
    va_list ap;
    if (trace_fp == NULL)
        return;
    pthread_mutex_lock(&trace_lock);
    fprintf(trace_fp, "[%" PRIu64 "] [%s] [", trace_timestamp_us(), event);
    va_start(ap, fmt);
    vfprintf(trace_fp, fmt, ap);
    va_end(ap);
    fprintf(trace_fp, "]\n");
    fflush(trace_fp);
    pthread_mutex_unlock(&trace_lock);
}

static void trace_init(void)
{
    char hostname[64] = {0};
    const char* dir;
    char path[512];
    if (trace_fp != NULL)
        return;
    gethostname(hostname, sizeof(hostname) - 1);
    dir = getenv("TJU_TRACE_DIR");
    if (dir != NULL && dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/%s.event.trace", dir,
                 strcmp(hostname, "server") == 0 ? "server" : "client");
        trace_fp = fopen(path, "w");
    }
    if (trace_fp == NULL) {
        snprintf(path, sizeof(path), "/vagrant/tju_tcp/test/%s.event.trace",
                 strcmp(hostname, "server") == 0 ? "server" : "client");
        /* 每次启动覆盖旧文件，符合课程目录约定。 */
        trace_fp = fopen(path, "w");
    }
    if (trace_fp == NULL) {
        snprintf(path, sizeof(path), "%s.event.trace",
                 strcmp(hostname, "server") == 0 ? "server" : "client");
        trace_fp = fopen(path, "w");
    }
}

static void trace_send(uint32_t seq, uint32_t ack, uint8_t flags,
                       uint16_t payload_len)
{
    trace_write("SEND", "seq:%" PRIu32 " ack:%" PRIu32 " flag:%u length:%u",
                seq, ack, flags, payload_len);
}

static void trace_recv(uint32_t seq, uint32_t ack, uint8_t flags,
                       uint16_t payload_len)
{
    trace_write("RECV", "seq:%" PRIu32 " ack:%" PRIu32 " flag:%u length:%u",
                seq, ack, flags, payload_len);
}

static void trace_cwnd(int type, uint32_t size)
{
    trace_write("CWND", "type:%d size:%" PRIu32, type, size);
}

static void trace_rwnd(uint32_t size)
{
    trace_write("RWND", "size:%" PRIu32, size);
}

static void trace_swnd(uint32_t size)
{
    trace_write("SWND", "size:%" PRIu32, size);
}

static void trace_rtts(double sample, double estimated, double deviation,
                       double timeout)
{
    trace_write("RTTS", "SampleRTT:%.6f EstimatedRTT:%.6f "
                "DeviationRTT:%.6f TimeoutInterval:%.6f",
                sample, estimated, deviation, timeout);
}

static void trace_delv(uint32_t seq, uint32_t size)
{
    trace_write("DELV", "seq:%" PRIu32 " size:%" PRIu32, seq, size);
}

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static uint32_t initial_seq(void)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static uint32_t next = 1000;
    uint32_t value;
    pthread_mutex_lock(&lock);
    value = next++;
    pthread_mutex_unlock(&lock);
    return value;
}

static uint32_t local_ip_address(void)
{
    char hostname[64] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    if (strcmp(hostname, "server") == 0)
        return inet_network("172.17.0.6");
    return inet_network("172.17.0.5");
}

static uint16_t advertised_window_locked(tju_tcp_t* sock)
{
    size_t used = (sock->received_len > 0 ? (size_t)sock->received_len : 0) + sock->recv_ooo_bytes;
    size_t free_bytes = used < sock->recv_capacity ? sock->recv_capacity - used : 0;
    return (uint16_t)(free_bytes > 65535 ? 65535 : free_bytes);
}

static void free_send_segment(tju_send_segment_t* seg)
{
    if (seg != NULL) {
        free(seg->data);
        free(seg);
    }
}

static void free_recv_segment(tju_recv_segment_t* seg)
{
    if (seg != NULL) {
        free(seg->data);
        free(seg);
    }
}

static void update_rtt_locked(tju_tcp_t* sock, double sample_ms)
{
    double rto;
    if (sample_ms <= 0.0)
        return;
    if (!sock->rtt_initialized) {
        sock->srtt_ms = sample_ms;
        sock->rttvar_ms = sample_ms / 2.0;
        sock->rtt_initialized = 1;
    } else {
        /* RFC 6298：先更新 RTTVAR，再更新 SRTT。 */
        sock->rttvar_ms = 0.75 * sock->rttvar_ms + 0.25 * fabs(sock->srtt_ms - sample_ms);
        sock->srtt_ms = 0.875 * sock->srtt_ms + 0.125 * sample_ms;
    }
    rto = sock->srtt_ms + 4.0 * sock->rttvar_ms;
    if (rto < TJU_MIN_RTO_MS)
        rto = TJU_MIN_RTO_MS;
    if (rto > TJU_MAX_RTO_MS)
        rto = TJU_MAX_RTO_MS;
    sock->rto_ms = rto;
    trace_rtts(sample_ms, sock->srtt_ms, sock->rttvar_ms, sock->rto_ms);
}

static void send_raw(tju_tcp_t* sock, tju_send_segment_t* seg)
{
    uint16_t plen = DEFAULT_HEADER_LEN + seg->data_len;
    char* packet;
    uint16_t window;
    uint8_t wire_flags;

    pthread_mutex_lock(&sock->recv_lock);
    window = advertised_window_locked(sock);
    pthread_mutex_unlock(&sock->recv_lock);

    wire_flags = (uint8_t)((seg->flags & SYN_FLAG_MASK) &&
                            sock->state == SYN_SENT && seg->seq == sock->iss
                                ? seg->flags : (seg->flags | ACK_FLAG_MASK));
    packet = create_packet_buf(sock->established_local_addr.port,
                               sock->established_remote_addr.port,
                               seg->seq,
                               sock->rcv_nxt,
                               DEFAULT_HEADER_LEN,
                               plen,
                               wire_flags,
                               window, 0, seg->data, seg->data_len);
    trace_send(seg->seq, sock->rcv_nxt, wire_flags, seg->data_len);
    trace_swnd(sock->peer_rwnd);
    sendToLayer3(packet, plen);
    free(packet);
}

static void send_ack(tju_tcp_t* sock)
{
    tju_send_segment_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.seq = sock->snd_nxt;
    ack.flags = ACK_FLAG_MASK;
    send_raw(sock, &ack);
}

static void enqueue_segment_locked(tju_tcp_t* sock, uint32_t seq, uint8_t flags,
                                    const char* data, uint16_t data_len)
{
    tju_send_segment_t* seg = calloc(1, sizeof(*seg));
    if (seg == NULL)
        return;
    seg->seq = seq;
    seg->data_len = data_len;
    seg->seq_len = data_len + ((flags & (SYN_FLAG_MASK | FIN_FLAG_MASK)) ? 1U : 0U);
    seg->flags = flags;
    if (data_len > 0) {
        seg->data = malloc(data_len);
        if (seg->data == NULL) {
            free(seg);
            return;
        }
        memcpy(seg->data, data, data_len);
    }
    if (sock->send_tail != NULL)
        sock->send_tail->next = seg;
    else
        sock->send_head = seg;
    sock->send_tail = seg;
}

static void flush_send_queue(tju_tcp_t* sock)
{
    tju_send_segment_t* seg;
    uint32_t right_edge;
    pthread_mutex_lock(&sock->send_lock);
    right_edge = sock->snd_una + sock->peer_rwnd;
    for (seg = sock->send_head; seg != NULL; seg = seg->next) {
        int allowed;
        if (seg->last_sent_ms > 0.0)
            continue;
        allowed = (seg->data_len == 0) ||
                  (sock->peer_rwnd > 0 && seg->seq + seg->seq_len <= right_edge);
        if (!allowed)
            break;
        if (seg->data_len > 0 && sock->peer_rwnd == 0)
            break;
        send_raw(sock, seg);
        seg->first_sent_ms = seg->last_sent_ms = now_ms();
    }
    pthread_mutex_unlock(&sock->send_lock);
}

static void retransmit_segment(tju_tcp_t* sock, tju_send_segment_t* seg)
{
    send_raw(sock, seg);
    seg->last_sent_ms = now_ms();
    seg->retransmitted = 1; /* Karn：此段后续 ACK 不参与 RTT 采样。 */
}

static void send_window_probe(tju_tcp_t* sock)
{
    char byte = 0;
    tju_send_segment_t probe;
    memset(&probe, 0, sizeof(probe));
    /* 使用 snd_una 前一个已发送字节作为探测，接收端会返回当前 ACK/rwnd，
       但不会把该重复字节再次交付给应用。 */
    probe.seq = sock->snd_una > 0 ? sock->snd_una - 1 : 0;
    probe.data = &byte;
    probe.data_len = 1;
    probe.flags = ACK_FLAG_MASK;
    send_raw(sock, &probe);
}

static void* timer_main(void* arg)
{
    tju_tcp_t* sock = (tju_tcp_t*)arg;
    while (1) {
        tju_send_segment_t* seg;
        double now = now_ms();
        int stop;
        usleep((useconds_t)(TJU_TIMER_GRANULARITY_MS * 1000.0));

        pthread_mutex_lock(&sock->state_lock);
        stop = sock->timer_stop;
        if (sock->state == TIME_WAIT && sock->time_wait_deadline_ms > 0.0 &&
            now >= sock->time_wait_deadline_ms) {
            sock->state = CLOSED;
            pthread_cond_broadcast(&sock->state_cond);
        }
        pthread_mutex_unlock(&sock->state_lock);
        if (stop)
            break;

        pthread_mutex_lock(&sock->send_lock);
        seg = sock->send_head;
        while (seg != NULL && seg->last_sent_ms == 0.0)
            seg = seg->next;
        if (seg != NULL && now - seg->last_sent_ms >= sock->rto_ms) {
            retransmit_segment(sock, seg);
            sock->rto_ms *= 2.0;
            if (sock->rto_ms > TJU_MAX_RTO_MS)
                sock->rto_ms = TJU_MAX_RTO_MS;
            /* 超时退避改变 TimeoutInterval 时同步记录 RTTS 事件。 */
            trace_rtts(0.0, sock->srtt_ms, sock->rttvar_ms, sock->rto_ms);
        }
        pthread_mutex_unlock(&sock->send_lock);

        /* 对端通告零窗口时，用 ACK 探测窗口恢复。 */
        if (sock->peer_rwnd == 0 && now - sock->last_probe_ms >= sock->rto_ms) {
            send_window_probe(sock);
            sock->last_probe_ms = now;
        }
        flush_send_queue(sock);
    }
    return NULL;
}

static void notify_accept(tju_tcp_t* child)
{
    tju_tcp_t* listener = child->parent_listener;
    if (listener == NULL)
        return;
    pthread_mutex_lock(&listener->state_lock);
    child->accept_next = NULL;
    if (listener->accept_tail != NULL)
        listener->accept_tail->accept_next = child;
    else
        listener->accept_head = child;
    listener->accept_tail = child;
    pthread_cond_signal(&listener->accept_cond);
    pthread_mutex_unlock(&listener->state_lock);
}

static size_t append_received_locked(tju_tcp_t* sock, const char* data, size_t len)
{
    size_t available;
    char* new_buf;
    if (len == 0)
        return 0;
    available = sock->recv_capacity > (size_t)sock->received_len ?
                sock->recv_capacity - (size_t)sock->received_len : 0;
    if (len > available)
        len = available;
    if (len == 0)
        return 0;
    new_buf = realloc(sock->received_buf, (size_t)sock->received_len + len);
    if (new_buf == NULL)
        return 0;
    sock->received_buf = new_buf;
    memcpy(sock->received_buf + sock->received_len, data, len);
    sock->received_len += (int)len;
    trace_rwnd((uint32_t)advertised_window_locked(sock));
    pthread_cond_broadcast(&sock->wait_cond);
    return len;
}

static void insert_out_of_order_locked(tju_tcp_t* sock, uint32_t seq,
                                        const char* data, uint32_t len);

static void drain_ordered_locked(tju_tcp_t* sock)
{
    while (sock->recv_ooo_head != NULL && sock->recv_ooo_head->seq == sock->rcv_nxt) {
        tju_recv_segment_t* seg = sock->recv_ooo_head;
        size_t accepted;
        uint32_t seg_len = seg->len;
        uint32_t old_seq = seg->seq;
        sock->recv_ooo_head = seg->next;
        sock->recv_ooo_bytes -= seg->len;
        accepted = append_received_locked(sock, seg->data, seg_len);
        sock->rcv_nxt += (uint32_t)accepted;
        if (accepted > 0)
            trace_delv(old_seq, (uint32_t)accepted);
        if (accepted < seg_len) {
            /* 缓存已满时保留未交付尾部，窗口打开后继续装入。 */
            insert_out_of_order_locked(sock, old_seq + (uint32_t)accepted,
                                       seg->data + accepted,
                                       seg_len - (uint32_t)accepted);
        }
        free_recv_segment(seg);
        if (accepted < seg_len)
            break;
    }
}

static void insert_out_of_order_locked(tju_tcp_t* sock, uint32_t seq,
                                        const char* data, uint32_t len)
{
    tju_recv_segment_t* cur;
    tju_recv_segment_t* prev = NULL;
    tju_recv_segment_t* node;
    uint32_t end = seq + len;
    if (len == 0)
        return;
    if (seq < sock->rcv_nxt) {
        uint32_t trim = sock->rcv_nxt - seq;
        if (trim >= len)
            return;
        seq += trim;
        data += trim;
        len -= trim;
    }
    cur = sock->recv_ooo_head;
    while (cur != NULL && cur->seq < seq) {
        prev = cur;
        cur = cur->next;
    }
    if (prev != NULL && prev->seq + prev->len > seq) {
        uint32_t overlap = prev->seq + prev->len - seq;
        if (overlap >= len)
            return;
        seq += overlap;
        data += overlap;
        len -= overlap;
    }
    end = seq + len;
    if (cur != NULL && end > cur->seq)
        len = cur->seq - seq;
    if (len == 0)
        return;
    node = calloc(1, sizeof(*node));
    if (node == NULL)
        return;
    node->data = malloc(len);
    if (node->data == NULL) {
        free(node);
        return;
    }
    node->seq = seq;
    node->len = len;
    memcpy(node->data, data, len);
    node->next = cur;
    if (prev != NULL)
        prev->next = node;
    else
        sock->recv_ooo_head = node;
    sock->recv_ooo_bytes += len;
}

static void process_ack(tju_tcp_t* sock, uint32_t ack)
{
    tju_send_segment_t* seg;
    tju_send_segment_t* next;
    double sample = 0.0;
    int advanced = 0;
    pthread_mutex_lock(&sock->send_lock);
    if (ack < sock->snd_una || ack > sock->snd_nxt) {
        pthread_mutex_unlock(&sock->send_lock);
        return;
    }
    if (ack == sock->snd_una) {
        if (sock->last_ack_seen == ack)
            sock->duplicate_ack_count++;
        else
            sock->duplicate_ack_count = 1;
        sock->last_ack_seen = ack;
        if (sock->duplicate_ack_count >= 3) {
            for (seg = sock->send_head; seg != NULL; seg = seg->next) {
                if (seg->last_sent_ms > 0.0 && seg->seq + seg->seq_len > ack) {
                    retransmit_segment(sock, seg);
                    break;
                }
            }
            sock->duplicate_ack_count = 0;
        }
        pthread_mutex_unlock(&sock->send_lock);
        return;
    }

    for (seg = sock->send_head; seg != NULL && seg->seq + seg->seq_len <= ack; seg = next) {
        next = seg->next;
        if (!seg->retransmitted && seg->first_sent_ms > 0.0 && sample == 0.0)
            sample = now_ms() - seg->first_sent_ms;
        sock->send_head = next;
        if (sock->send_tail == seg)
            sock->send_tail = NULL;
        free_send_segment(seg);
    }
    sock->snd_una = ack;
    sock->last_ack_seen = ack;
    sock->duplicate_ack_count = 0;
    advanced = 1;
    if (sample > 0.0)
        update_rtt_locked(sock, sample);
    else if (sock->rto_ms < TJU_INITIAL_RTO_MS)
        sock->rto_ms = TJU_INITIAL_RTO_MS;
    pthread_cond_broadcast(&sock->send_cond);
    pthread_mutex_unlock(&sock->send_lock);

    if (advanced) {
        pthread_mutex_lock(&sock->state_lock);
        if (sock->state == SYN_SENT && ack >= sock->iss + 1) {
            sock->state = ESTABLISHED;
            pthread_cond_broadcast(&sock->state_cond);
        } else if (sock->state == SYN_RECV && ack >= sock->iss + 1) {
            sock->state = ESTABLISHED;
            pthread_cond_broadcast(&sock->state_cond);
            notify_accept(sock);
        } else if (sock->state == FIN_WAIT_1 && ack >= sock->fin_seq + 1) {
            sock->state = FIN_WAIT_2;
            pthread_cond_broadcast(&sock->state_cond);
        } else if (sock->state == LAST_ACK && ack >= sock->fin_seq + 1) {
            sock->state = CLOSED;
            pthread_cond_broadcast(&sock->state_cond);
        } else if (sock->state == CLOSING && ack >= sock->fin_seq + 1) {
            sock->state = TIME_WAIT;
            sock->time_wait_deadline_ms = now_ms() + 2.0 * TJU_MSL_MS;
        }
        pthread_mutex_unlock(&sock->state_lock);
        flush_send_queue(sock);
    }
}

static void handle_fin(tju_tcp_t* sock, uint32_t fin_seq)
{
    int accepted = 0;
    pthread_mutex_lock(&sock->state_lock);
    if (fin_seq == sock->rcv_nxt) {
        sock->rcv_nxt++;
        accepted = 1;
        if (sock->state == ESTABLISHED)
            sock->state = CLOSE_WAIT;
        else if (sock->state == FIN_WAIT_1)
            sock->state = CLOSING;
        else if (sock->state == FIN_WAIT_2)
            sock->state = TIME_WAIT;
        if (sock->state == TIME_WAIT)
            sock->time_wait_deadline_ms = now_ms() + 2.0 * TJU_MSL_MS;
        pthread_cond_broadcast(&sock->state_cond);
    } else if (fin_seq > sock->rcv_nxt) {
        sock->peer_fin_pending = 1;
        sock->peer_fin_seq = fin_seq;
    }
    pthread_mutex_unlock(&sock->state_lock);
    if (accepted) {
        pthread_mutex_lock(&sock->recv_lock);
        sock->recv_eof = 1;
        pthread_cond_broadcast(&sock->wait_cond);
        pthread_mutex_unlock(&sock->recv_lock);
    }
    send_ack(sock);
}

static void handle_listener_syn(tju_tcp_t* listener, char* pkt)
{
    tju_tcp_t* child;
    uint32_t local_ip = listener->bind_addr.ip;
    uint32_t remote_ip = (local_ip == inet_network("172.17.0.3"))
                             ? inet_network("172.17.0.5")
                             : inet_network("172.17.0.6");
    uint16_t remote_port = get_src(pkt);
    uint16_t local_port = get_dst(pkt);
    uint32_t hashval;

    child = tju_socket();
    child->bind_addr = listener->bind_addr;
    child->established_local_addr.ip = local_ip;
    child->established_local_addr.port = local_port;
    child->established_remote_addr.ip = remote_ip;
    child->established_remote_addr.port = remote_port;
    child->parent_listener = listener;
    child->state = SYN_RECV;
    child->irs = get_seq(pkt);
    child->rcv_nxt = child->irs + 1;
    child->iss = initial_seq();
    child->snd_una = child->iss;
    child->snd_nxt = child->iss + 1;
    child->peer_rwnd = get_advertised_window(pkt);
    if (child->peer_rwnd == 0)
        child->peer_rwnd = 65535;
    hashval = cal_hash(local_ip, local_port, remote_ip, remote_port);
    established_socks[hashval] = child;

    pthread_mutex_lock(&child->send_lock);
    enqueue_segment_locked(child, child->iss, SYN_FLAG_MASK, NULL, 0);
    pthread_mutex_unlock(&child->send_lock);
    flush_send_queue(child);
}

tju_tcp_t* tju_socket(void)
{
    tju_tcp_t* sock = calloc(1, sizeof(*sock));
    if (sock == NULL)
        return NULL;
    /* 允许调用方在 startSimulation 前创建 socket 时也能生成 trace。 */
    trace_init();
    sock->state = CLOSED;
    pthread_mutex_init(&sock->send_lock, NULL);
    pthread_mutex_init(&sock->recv_lock, NULL);
    pthread_mutex_init(&sock->state_lock, NULL);
    pthread_cond_init(&sock->wait_cond, NULL);
    pthread_cond_init(&sock->state_cond, NULL);
    pthread_cond_init(&sock->accept_cond, NULL);
    pthread_cond_init(&sock->send_cond, NULL);
    sock->recv_capacity = TCP_RECVWN_SIZE;
    sock->peer_rwnd = 65535;
    sock->rto_ms = TJU_INITIAL_RTO_MS;
    trace_rwnd((uint32_t)advertised_window_locked(sock));
    trace_rtts(0.0, 0.0, 0.0, sock->rto_ms);
    if (pthread_create(&sock->timer_thread, NULL, timer_main, sock) == 0)
        sock->timer_started = 1;
    return sock;
}

int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr)
{
    if (sock == NULL)
        return -1;
    sock->bind_addr = bind_addr;
    return 0;
}

int tju_listen(tju_tcp_t* sock)
{
    int hashval;
    if (sock == NULL)
        return -1;
    pthread_mutex_lock(&sock->state_lock);
    sock->state = LISTEN;
    pthread_mutex_unlock(&sock->state_lock);
    hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    listen_socks[hashval] = sock;
    return 0;
}

tju_tcp_t* tju_accept(tju_tcp_t* listen_sock)
{
    tju_tcp_t* conn;
    if (listen_sock == NULL)
        return NULL;
    pthread_mutex_lock(&listen_sock->state_lock);
    while (listen_sock->accept_head == NULL)
        pthread_cond_wait(&listen_sock->accept_cond, &listen_sock->state_lock);
    conn = listen_sock->accept_head;
    listen_sock->accept_head = conn->accept_next;
    if (listen_sock->accept_head == NULL)
        listen_sock->accept_tail = NULL;
    conn->accept_next = NULL;
    pthread_mutex_unlock(&listen_sock->state_lock);
    return conn;
}

int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr)
{
    struct timespec deadline;
    uint32_t hashval;
    if (sock == NULL)
        return -1;
    sock->established_remote_addr = target_addr;
    sock->established_local_addr.ip = local_ip_address();
    sock->established_local_addr.port = 5678;
    sock->iss = initial_seq();
    sock->snd_una = sock->iss;
    sock->snd_nxt = sock->iss + 1;
    sock->rcv_nxt = 0;
    sock->peer_rwnd = 65535;
    pthread_mutex_lock(&sock->state_lock);
    sock->state = SYN_SENT;
    pthread_mutex_unlock(&sock->state_lock);
    hashval = cal_hash(sock->established_local_addr.ip, sock->established_local_addr.port,
                       target_addr.ip, target_addr.port);
    established_socks[hashval] = sock;
    pthread_mutex_lock(&sock->send_lock);
    enqueue_segment_locked(sock, sock->iss, SYN_FLAG_MASK, NULL, 0);
    pthread_mutex_unlock(&sock->send_lock);
    flush_send_queue(sock);

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 30;
    pthread_mutex_lock(&sock->state_lock);
    while (sock->state == SYN_SENT) {
        if (pthread_cond_timedwait(&sock->state_cond, &sock->state_lock, &deadline) == ETIMEDOUT)
            break;
    }
    if (sock->state != ESTABLISHED) {
        pthread_mutex_unlock(&sock->state_lock);
        return -1;
    }
    pthread_mutex_unlock(&sock->state_lock);
    return 0;
}

int tju_send(tju_tcp_t* sock, const void* buffer, int len)
{
    const char* data = (const char*)buffer;
    int offset = 0;
    if (sock == NULL || buffer == NULL || len < 0)
        return -1;
    pthread_mutex_lock(&sock->state_lock);
    if (sock->state != ESTABLISHED && sock->state != CLOSE_WAIT) {
        pthread_mutex_unlock(&sock->state_lock);
        return -1;
    }
    pthread_mutex_unlock(&sock->state_lock);
    while (offset < len) {
        uint16_t part = (uint16_t)((len - offset) > MAX_DLEN ? MAX_DLEN : (len - offset));
        pthread_mutex_lock(&sock->send_lock);
        enqueue_segment_locked(sock, sock->snd_nxt, 0, data + offset, part);
        sock->snd_nxt += part;
        pthread_mutex_unlock(&sock->send_lock);
        offset += part;
    }
    flush_send_queue(sock);
    return len;
}

int tju_recv(tju_tcp_t* sock, void* buffer, int len)
{
    int read_len;
    if (sock == NULL || buffer == NULL || len <= 0)
        return -1;
    pthread_mutex_lock(&sock->recv_lock);
    while (sock->received_len == 0 && !sock->recv_eof)
        pthread_cond_wait(&sock->wait_cond, &sock->recv_lock);
    if (sock->received_len == 0 && sock->recv_eof) {
        pthread_mutex_unlock(&sock->recv_lock);
        return 0;
    }
    read_len = sock->received_len < len ? sock->received_len : len;
    memcpy(buffer, sock->received_buf, read_len);
    if (read_len == sock->received_len) {
        free(sock->received_buf);
        sock->received_buf = NULL;
        sock->received_len = 0;
    } else {
        memmove(sock->received_buf, sock->received_buf + read_len, sock->received_len - read_len);
        sock->received_len -= read_len;
    }
    /* 应用释放空间后，立即尝试把已缓存的失序段接到按序缓冲区。 */
    drain_ordered_locked(sock);
    trace_rwnd((uint32_t)advertised_window_locked(sock));
    pthread_mutex_unlock(&sock->recv_lock);
    /* 窗口扩大后立即发送 ACK，让发送端继续滑动。 */
    send_ack(sock);
    return read_len;
}

int tju_handle_packet(tju_tcp_t* sock, char* pkt)
{
    uint16_t hlen, plen, flags, adv_window;
    uint32_t seq, ack, data_len, end;
    int was_syn_sent;
    if (sock == NULL || pkt == NULL)
        return -1;
    hlen = get_hlen(pkt);
    plen = get_plen(pkt);
    if (hlen < DEFAULT_HEADER_LEN || plen < hlen || plen > MAX_LEN)
        return -1;
    flags = get_flags(pkt);
    seq = get_seq(pkt);
    ack = get_ack(pkt);
    adv_window = get_advertised_window(pkt);
    trace_recv(seq, ack, (uint8_t)flags, (uint16_t)(plen - hlen));

    if (sock->state == LISTEN) {
        if (flags & SYN_FLAG_MASK)
            handle_listener_syn(sock, pkt);
        return 0;
    }
    was_syn_sent = sock->state == SYN_SENT;
    if ((flags & SYN_FLAG_MASK) && was_syn_sent) {
        sock->irs = seq;
        sock->rcv_nxt = seq + 1;
    }
    sock->peer_rwnd = adv_window;
    if (flags & ACK_FLAG_MASK)
        process_ack(sock, ack);

    if ((flags & SYN_FLAG_MASK) && sock->state == SYN_RECV) {
        flush_send_queue(sock);
        return 0;
    }
    if ((flags & SYN_FLAG_MASK) && (was_syn_sent || sock->state == ESTABLISHED)) {
        /* 正常或重复 SYN-ACK 到达时，都补发最终 ACK。 */
        send_ack(sock);
    }

    data_len = plen - hlen;
    if (data_len > 0) {
        pthread_mutex_lock(&sock->recv_lock);
        end = seq + data_len;
        if (seq <= sock->rcv_nxt && end > sock->rcv_nxt) {
            uint32_t trim = sock->rcv_nxt - seq;
            size_t remaining = data_len - trim;
            size_t accepted = append_received_locked(sock, pkt + hlen + trim, remaining);
            if (accepted > 0)
                trace_delv(sock->rcv_nxt, (uint32_t)accepted);
            sock->rcv_nxt += (uint32_t)accepted;
            if (accepted < remaining) {
                insert_out_of_order_locked(sock, sock->rcv_nxt,
                                           pkt + hlen + trim + accepted,
                                           (uint32_t)(remaining - accepted));
            }
            drain_ordered_locked(sock);
        } else if (seq > sock->rcv_nxt) {
            insert_out_of_order_locked(sock, seq, pkt + hlen, data_len);
        } /* seq < rcv_nxt 的完全重复数据直接丢弃。 */
        pthread_mutex_unlock(&sock->recv_lock);
        send_ack(sock);
        if (sock->peer_fin_pending && sock->peer_fin_seq == sock->rcv_nxt) {
            sock->peer_fin_pending = 0;
            handle_fin(sock, sock->peer_fin_seq);
        }
    }
    if (flags & FIN_FLAG_MASK)
        handle_fin(sock, seq + data_len);
    return 0;
}

int tju_close(tju_tcp_t* sock)
{
    int state;
    double wait_until;
    if (sock == NULL)
        return -1;
    pthread_mutex_lock(&sock->state_lock);
    state = sock->state;
    if (state == CLOSED || state == TIME_WAIT) {
        pthread_mutex_unlock(&sock->state_lock);
        return 0;
    }
    if (state != ESTABLISHED && state != CLOSE_WAIT) {
        pthread_mutex_unlock(&sock->state_lock);
        return 0;
    }
    sock->closing_requested = 1;
    pthread_mutex_unlock(&sock->state_lock);

    /* 先等待应用数据全部离开发送队列，再把 FIN 放到序号空间末端。 */
    wait_until = now_ms() + 30000.0;
    pthread_mutex_lock(&sock->send_lock);
    while (sock->send_head != NULL && now_ms() < wait_until) {
        struct timespec ts;
        tju_send_segment_t* seg;
        int has_payload = 0;
        for (seg = sock->send_head; seg != NULL; seg = seg->next) {
            if (seg->data_len > 0) {
                has_payload = 1;
                break;
            }
        }
        if (!has_payload)
            break;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&sock->send_cond, &sock->send_lock, &ts);
    }
    pthread_mutex_unlock(&sock->send_lock);

    pthread_mutex_lock(&sock->state_lock);
    if (sock->state == ESTABLISHED)
        sock->state = FIN_WAIT_1;
    else if (sock->state == CLOSE_WAIT)
        sock->state = LAST_ACK;
    else {
        pthread_mutex_unlock(&sock->state_lock);
        return 0;
    }
    pthread_mutex_unlock(&sock->state_lock);

    pthread_mutex_lock(&sock->send_lock);
    sock->fin_seq = sock->snd_nxt;
    enqueue_segment_locked(sock, sock->fin_seq, FIN_FLAG_MASK, NULL, 0);
    sock->snd_nxt++;
    pthread_mutex_unlock(&sock->send_lock);
    flush_send_queue(sock);
    return 0;
}
