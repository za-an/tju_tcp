#ifndef TJU_CONGESTION_H
#define TJU_CONGESTION_H

#include <stdint.h>

/* All windows and flight sizes are payload bytes. Sequence numbers point at
 * the next expected byte, including high_sent_seq (the exclusive send edge).
 * Callers serialize access, count only actual transmissions in FlightSize,
 * and feed on_dup_ack only ACKs qualified under RFC 5681 section 2. */
typedef enum {
    TJU_CC_BASIC = 0,
    TJU_CC_RENO = 1,
    TJU_CC_NEWRENO = 2,
    TJU_CC_CUBIC = 3
} tju_cc_mode_t;

typedef enum {
    TJU_CC_SLOW_START = 0,
    TJU_CC_CONGESTION_AVOIDANCE = 1,
    TJU_CC_FAST_RECOVERY = 2
} tju_cc_state_t;

typedef enum {
    TJU_CC_NONE = 0,
    TJU_CC_FAST_RETRANSMIT = 1,
    TJU_CC_PARTIAL_RETRANSMIT = 2
} tju_cc_action_t;

typedef struct {
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t smss;
    uint32_t recover;
    uint32_t dupacks;
    tju_cc_mode_t mode;
    tju_cc_state_t state;
    uint64_t ca_acked_bytes;
    uint32_t last_ack;
    uint32_t dup_ack_seq;
    unsigned char have_ack;
    /* NewReno's recovery guard; not a blanket ban on threshold updates. */
    unsigned char loss_epoch;
    uint32_t cubic_wmax;
    uint32_t cubic_tick;
    uint32_t cubic_ack_accum;
} tju_cc_t;

void tju_cc_init(tju_cc_t *cc, tju_cc_mode_t mode, uint32_t smss,
                 uint32_t initial_cwnd, uint32_t initial_ssthresh);

/* new_data_bytes excludes SYN/FIN and retransmission duplicates. Both flight
 * arguments describe payload only; high_sent_seq excludes queued unsent data.
 * Duplicate and stale ACKs cannot grow cwnd, even if a caller passes bytes. */
tju_cc_action_t tju_cc_on_ack(tju_cc_t *cc, uint32_t ack,
                             uint32_t new_data_bytes, uint32_t flight_before,
                             uint32_t flight_after, uint32_t high_sent_seq);
tju_cc_action_t tju_cc_on_dup_ack(tju_cc_t *cc, uint32_t ack,
                                 uint32_t flight_size, uint32_t high_sent_seq);
/* repeated_timeout is nonzero only if this same segment has previously been
 * retransmitted by the RTO timer. A fast retransmission does not set it. */
void tju_cc_on_timeout(tju_cc_t *cc, uint32_t flight_size,
                       uint32_t high_sent_seq, int repeated_timeout);

#endif
