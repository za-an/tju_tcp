#include "tju_congestion.h"

#include <limits.h>
#include <string.h>

#ifdef TJU_CC_EMBEDDED_WEAK
#define TJU_CC_LINKAGE __attribute__((weak))
#else
#define TJU_CC_LINKAGE
#endif

/* TCP sequence ordering is unambiguous for outstanding spans < 2^31. */
static int seq_after(uint32_t a, uint32_t b) {
    return a != b && (uint32_t)(a - b) < UINT32_C(0x80000000);
}

static int seq_at_or_after(uint32_t a, uint32_t b) {
    return a == b || seq_after(a, b);
}

static uint32_t add_sat(uint32_t a, uint32_t b) {
    return UINT32_MAX - a < b ? UINT32_MAX : a + b;
}

static uint32_t maximum(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static uint32_t minimum(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void set_open_state(tju_cc_t *cc) {
    cc->state = cc->cwnd < cc->ssthresh ? TJU_CC_SLOW_START
                                      : TJU_CC_CONGESTION_AVOIDANCE;
}

static uint32_t cubic_target(const tju_cc_t *cc)
{
    int64_t delta = (int64_t)cc->cubic_tick - 8;
    int64_t cube = delta * delta * delta;
    int64_t target = (int64_t)cc->cubic_wmax + cube / 64;
    if (target < (int64_t)cc->smss)
        target = cc->smss;
    if (target > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)target;
}

static uint32_t loss_threshold(const tju_cc_t *cc, uint32_t flight_size) {
    return maximum(flight_size / 2, add_sat(cc->smss, cc->smss));
}

static void begin_loss_epoch(tju_cc_t *cc, uint32_t high_sent_seq) {
    /* This boundary controls NewReno recovery, independently of whether a
     * newly detected loss requires recalculating ssthresh (RFC 5681). */
    cc->recover = high_sent_seq;
    cc->loss_epoch = 1;
    cc->ca_acked_bytes = 0;
}

TJU_CC_LINKAGE void tju_cc_init(tju_cc_t *cc, tju_cc_mode_t mode, uint32_t smss,
                 uint32_t initial_cwnd, uint32_t initial_ssthresh) {
    memset(cc, 0, sizeof(*cc));
    cc->smss = smss ? smss : 1;
    cc->mode = mode >= TJU_CC_BASIC && mode <= TJU_CC_CUBIC
                   ? mode : TJU_CC_BASIC;
    cc->cwnd = maximum(initial_cwnd, cc->smss);
    cc->ssthresh = maximum(initial_ssthresh,
                           add_sat(cc->smss, cc->smss));
    cc->cubic_wmax = cc->cwnd;
    set_open_state(cc);
}

TJU_CC_LINKAGE tju_cc_action_t tju_cc_on_ack(tju_cc_t *cc, uint32_t ack,
                             uint32_t new_data_bytes, uint32_t flight_before,
                             uint32_t flight_after, uint32_t high_sent_seq) {
    uint32_t old_cwnd = cc->cwnd;
    int full_ack;
    (void)high_sent_seq;

    if (cc->have_ack && !seq_after(ack, cc->last_ack)) {
        return TJU_CC_NONE;
    }
    cc->last_ack = ack;
    cc->have_ack = 1;
    cc->dup_ack_seq = ack;
    cc->dupacks = 0;
    /* Defensive clipping keeps malformed accounting from manufacturing cwnd. */
    new_data_bytes = minimum(new_data_bytes, flight_before);
    full_ack = cc->loss_epoch && seq_at_or_after(ack, cc->recover);

    if (cc->state == TJU_CC_FAST_RECOVERY) {
        if (cc->mode == TJU_CC_NEWRENO && !full_ack) {
            if (!new_data_bytes) {
                return TJU_CC_NONE;
            }
            /* RFC 6582 partial ACK: deflate by newly ACKed bytes, add one
             * SMSS only when at least one complete SMSS was acknowledged. */
            cc->cwnd = cc->cwnd > new_data_bytes
                           ? cc->cwnd - new_data_bytes : 0;
            if (new_data_bytes >= cc->smss) {
                cc->cwnd = add_sat(cc->cwnd, cc->smss);
            }
            cc->cwnd = maximum(cc->cwnd, cc->smss);
            return TJU_CC_PARTIAL_RETRANSMIT;
        }
        /* NewReno uses the RFC 6582 conservative exit option, limiting the
         * first post-recovery burst to FlightSize + SMSS. Basic/Reno deflate
         * to ssthresh on the first ACK of new data. */
        cc->cwnd = cc->mode == TJU_CC_NEWRENO
                       ? minimum(cc->ssthresh,
                                 maximum(add_sat(flight_after, cc->smss),
                                         cc->smss))
                       : cc->ssthresh;
        if (!new_data_bytes) {
            cc->cwnd = minimum(cc->cwnd, old_cwnd);
        }
        cc->ca_acked_bytes = 0;
        set_open_state(cc);
        if (full_ack) {
            cc->loss_epoch = 0;
        }
        return TJU_CC_NONE;
    }

    if (full_ack) {
        cc->loss_epoch = 0;
    }
    if (!new_data_bytes) {
        return TJU_CC_NONE;
    }
    if (cc->mode == TJU_CC_CUBIC) {
        cc->cubic_tick++;
        cc->cubic_ack_accum += new_data_bytes;
        if (cc->cubic_ack_accum >= cc->cwnd) {
            uint32_t target = cubic_target(cc);
            cc->cubic_ack_accum = 0;
            if (target > cc->cwnd)
                cc->cwnd = target;
        }
    } else if (cc->cwnd < cc->ssthresh) {
        /* RFC 5681: at most one SMSS per ACK, even with delayed ACKs. */
        cc->cwnd = add_sat(cc->cwnd, minimum(new_data_bytes, cc->smss));
    } else {
        /* Byte counting avoids a zero increment at large cwnd and prevents
         * ACK division from accelerating additive increase. Reset on each
         * increase per RFC 5681, so one ACK cannot cause a multi-SMSS jump. */
        cc->ca_acked_bytes += new_data_bytes;
        if (cc->ca_acked_bytes >= cc->cwnd) {
            cc->ca_acked_bytes = 0;
            cc->cwnd = add_sat(cc->cwnd, cc->smss);
        }
    }
    set_open_state(cc);
    return TJU_CC_NONE;
}

TJU_CC_LINKAGE tju_cc_action_t tju_cc_on_dup_ack(tju_cc_t *cc, uint32_t ack,
                                 uint32_t flight_size, uint32_t high_sent_seq) {
    if (!flight_size) {
        return TJU_CC_NONE;
    }
    if (cc->have_ack && ack != cc->last_ack) {
        return TJU_CC_NONE;
    }
    if (!cc->have_ack) {
        cc->last_ack = ack;
        cc->have_ack = 1;
    }
    if (!cc->dupacks || cc->dup_ack_seq != ack) {
        cc->dup_ack_seq = ack;
        cc->dupacks = 0;
    }
    if (cc->dupacks != UINT32_MAX) {
        ++cc->dupacks;
    }
    if (cc->state == TJU_CC_FAST_RECOVERY) {
        if (cc->mode != TJU_CC_BASIC) {
            cc->cwnd = add_sat(cc->cwnd, cc->smss);
        }
        return TJU_CC_NONE;
    }
    if (cc->dupacks != 3) {
        return TJU_CC_NONE;
    }
    /* After an RTO, NewReno waits until recover is covered before allowing
     * another fast recovery; duplicate ACKs for old data must not undo RTO. */
    if (cc->mode == TJU_CC_NEWRENO && cc->loss_epoch) {
        return TJU_CC_NONE;
    }
    /* Each new BASIC/Reno recovery entry responds to its current FlightSize.
     * NewReno reaches here only after its prior recovery boundary is covered. */
    cc->ssthresh = loss_threshold(cc, flight_size);
    begin_loss_epoch(cc, high_sent_seq);
    cc->cwnd = cc->ssthresh;
    if (cc->mode != TJU_CC_BASIC) {
        cc->cwnd = add_sat(cc->cwnd,
                          add_sat(cc->smss, add_sat(cc->smss, cc->smss)));
    }
    cc->state = TJU_CC_FAST_RECOVERY;
    if (cc->mode == TJU_CC_CUBIC)
        cc->cubic_wmax = cc->cwnd;
    return TJU_CC_FAST_RETRANSMIT;
}

TJU_CC_LINKAGE void tju_cc_on_timeout(tju_cc_t *cc, uint32_t flight_size,
                       uint32_t high_sent_seq, int repeated_timeout) {
    if (!flight_size) {
        return; /* SYN/FIN and persist timers are not congestion signals. */
    }
    /* RFC 5681 section 3.1 preserves ssthresh only on an RTO retry of the
     * same segment, not on the first timeout of another hole in this flight.
     * A prior fast retransmission also does not suppress the first RTO cut. */
    if (!repeated_timeout) {
        if (cc->mode == TJU_CC_CUBIC)
            cc->cubic_wmax = cc->cwnd;
        cc->ssthresh = loss_threshold(cc, flight_size);
    }
    /* RFC 6582 also records the current send edge on timeout, including data
     * sent during fast recovery beyond the original recovery boundary. */
    begin_loss_epoch(cc, high_sent_seq);
    cc->cwnd = cc->smss;
    cc->dupacks = 0;
    cc->state = TJU_CC_SLOW_START;
}
