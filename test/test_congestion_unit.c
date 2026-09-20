/* Pure controller tests; no network, clocks, threads, or virtual machines.
 * gcc -std=c99 -Wall -Wextra -Werror -I../inc test_congestion_unit.c \
 *     ../src/tju_congestion.c -o test_congestion_unit
 */
#include "tju_congestion.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#define SMSS UINT32_C(1000)

static void initialize(tju_cc_t *cc, tju_cc_mode_t mode, uint32_t cwnd,
                        uint32_t threshold, uint32_t ack) {
    tju_cc_init(cc, mode, SMSS, cwnd, threshold);
    assert(tju_cc_on_ack(cc, ack, 0, 0, 0, ack) == TJU_CC_NONE);
}

static void invariant(const tju_cc_t *cc) {
    assert(cc->smss > 0);
    assert(cc->cwnd >= cc->smss);
    assert(cc->ssthresh >= 2 * SMSS);
    assert(cc->state >= TJU_CC_SLOW_START);
    assert(cc->state <= TJU_CC_FAST_RECOVERY);
}

static void three_duplicates(tju_cc_t *cc, uint32_t ack,
                               uint32_t flight, uint32_t high) {
    assert(tju_cc_on_dup_ack(cc, ack, flight, high) == TJU_CC_NONE);
    assert(tju_cc_on_dup_ack(cc, ack, flight, high) == TJU_CC_NONE);
    assert(tju_cc_on_dup_ack(cc, ack, flight, high) == TJU_CC_FAST_RETRANSMIT);
    assert(cc->dupacks == 3);
    invariant(cc);
}

static void test_initialization_and_control_ack(void) {
    tju_cc_t cc;
    int mode;
    for (mode = TJU_CC_BASIC; mode <= TJU_CC_NEWRENO; ++mode) {
        tju_cc_init(&cc, (tju_cc_mode_t)mode, SMSS, SMSS, 8000);
        assert(cc.state == TJU_CC_SLOW_START);
        assert(tju_cc_on_ack(&cc, 1, 0, 0, 0, 1) == TJU_CC_NONE);
        assert(cc.cwnd == SMSS);
        assert(tju_cc_on_ack(&cc, 2, 0, 1000, 1000, 1002) == TJU_CC_NONE);
        assert(cc.cwnd == SMSS);
        tju_cc_on_timeout(&cc, 0, 2, 0);
        assert(cc.cwnd == SMSS && cc.ssthresh == 8000);
        assert(tju_cc_on_dup_ack(&cc, 2, 0, 2) == TJU_CC_NONE);
        assert(cc.dupacks == 0);
    }
    tju_cc_init(&cc, (tju_cc_mode_t)42, 0, 0, 0);
    assert(cc.mode == TJU_CC_BASIC && cc.smss == 1);
    assert(cc.cwnd == 1 && cc.ssthresh == 2);
}

static void test_slow_start_and_avoidance(void) {
    tju_cc_t cc;
    uint32_t i;
    initialize(&cc, TJU_CC_BASIC, 1000, 4000, 0);
    tju_cc_on_ack(&cc, 1000, 1000, 1000, 0, 1000);
    assert(cc.cwnd == 2000);
    tju_cc_on_ack(&cc, 3000, 2000, 2000, 0, 3000);
    assert(cc.cwnd == 3000); /* Delayed ACK adds only one SMSS. */
    tju_cc_on_ack(&cc, 3500, 500, 3000, 2500, 6000);
    assert(cc.cwnd == 3500);
    tju_cc_on_ack(&cc, 4000, 500, 2500, 2000, 6000);
    assert(cc.cwnd == 4000 && cc.state == TJU_CC_CONGESTION_AVOIDANCE);
    for (i = 1; i <= 3; ++i) {
        tju_cc_on_ack(&cc, 4000 + i * SMSS, SMSS, 4000, 3000,
                      7000 + i * SMSS);
        assert(cc.cwnd == 4000);
    }
    tju_cc_on_ack(&cc, 8000, SMSS, 4000, 3000, 11000);
    assert(cc.cwnd == 5000 && cc.ca_acked_bytes == 0);
    /* One huge cumulative ACK must not increase cwnd multiple times. */
    tju_cc_on_ack(&cc, 28000, 20000, 20000, 0, 28000);
    assert(cc.cwnd == 6000 && cc.ca_acked_bytes == 0);
    invariant(&cc);
}

static void test_ack_division_and_stale_ack(void) {
    tju_cc_t divided, normal;
    uint32_t i;
    initialize(&divided, TJU_CC_RENO, 10000, 10000, 0);
    initialize(&normal, TJU_CC_RENO, 10000, 10000, 0);
    for (i = 1; i <= 10000; ++i) {
        tju_cc_on_ack(&divided, i, 1, 10000, 9999, i + 9999);
    }
    for (i = 1; i <= 10; ++i) {
        tju_cc_on_ack(&normal, i * SMSS, SMSS, 10000, 9000,
                      i * SMSS + 9000);
    }
    assert(divided.cwnd == 11000 && divided.cwnd == normal.cwnd);
    assert(divided.ca_acked_bytes == normal.ca_acked_bytes);
    tju_cc_on_ack(&normal, 10000, 50000, 50000, 0, 50000);
    tju_cc_on_ack(&normal, 9999, 50000, 50000, 0, 50000);
    assert(normal.cwnd == 11000 && normal.ca_acked_bytes == 0);
    assert(tju_cc_on_dup_ack(&normal, 9999, 10000, 20000) == TJU_CC_NONE);
    assert(tju_cc_on_dup_ack(&normal, 10001, 10000, 20000) == TJU_CC_NONE);
    assert(normal.dupacks == 0);
}

static void test_timeout_and_loss_epoch(void) {
    tju_cc_t cc;
    initialize(&cc, TJU_CC_BASIC, 32000, 16000, 0);
    tju_cc_on_timeout(&cc, 10000, 10000, 0);
    assert(cc.cwnd == SMSS && cc.ssthresh == 5000);
    assert(cc.recover == 10000 && cc.loss_epoch);
    assert(cc.state == TJU_CC_SLOW_START);
    tju_cc_on_timeout(&cc, 4000, 10000, 1);
    assert(cc.ssthresh == 5000); /* RTO retry of the same segment. */
    tju_cc_on_ack(&cc, 1000, 1000, 10000, 9000, 10000);
    assert(cc.cwnd == 2000 && cc.loss_epoch);
    tju_cc_on_timeout(&cc, 9000, 10000, 0);
    assert(cc.cwnd == SMSS && cc.ssthresh == 4500);
    tju_cc_on_ack(&cc, 10000, 9000, 9000, 0, 10000);
    assert(!cc.loss_epoch);
    tju_cc_on_timeout(&cc, 2000, 12000, 0);
    assert(cc.ssthresh == 2000 && cc.recover == 12000);
    invariant(&cc);
}

static void test_basic_fast_retransmit(void) {
    tju_cc_t cc;
    initialize(&cc, TJU_CC_BASIC, 20000, 16000, 100);
    three_duplicates(&cc, 100, 10000, 10100);
    assert(cc.ssthresh == 5000 && cc.cwnd == 5000);
    tju_cc_on_dup_ack(&cc, 100, 10000, 10100);
    assert(cc.cwnd == 5000); /* Basic mode has no artificial inflation. */
    tju_cc_on_ack(&cc, 1100, 1000, 10000, 9000, 10100);
    assert(cc.cwnd == 5000 && cc.state == TJU_CC_CONGESTION_AVOIDANCE);
    assert(cc.loss_epoch);
    three_duplicates(&cc, 1100, 9000, 10100);
    assert(cc.ssthresh == 4500); /* New recovery uses current FlightSize. */
    tju_cc_on_ack(&cc, 10100, 9000, 9000, 0, 10100);
    assert(cc.cwnd == 4500 && !cc.loss_epoch);
}

static void test_reno_fast_recovery(void) {
    tju_cc_t cc;
    initialize(&cc, TJU_CC_RENO, 20000, 16000, 0);
    three_duplicates(&cc, 0, 12000, 12000);
    assert(cc.ssthresh == 6000 && cc.cwnd == 9000);
    assert(cc.state == TJU_CC_FAST_RECOVERY);
    tju_cc_on_dup_ack(&cc, 0, 12000, 12000);
    assert(cc.cwnd == 10000);
    tju_cc_on_dup_ack(&cc, 0, 12000, 12000);
    assert(cc.cwnd == 11000);
    tju_cc_on_ack(&cc, 1000, 1000, 12000, 11000, 12000);
    assert(cc.cwnd == 6000 && cc.state == TJU_CC_CONGESTION_AVOIDANCE);
    assert(cc.dupacks == 0 && cc.ca_acked_bytes == 0);
    /* Reno exits on the first advancing ACK, even if another hole remains. */
    assert(cc.loss_epoch);
    tju_cc_on_timeout(&cc, 11000, 12000, 0);
    assert(cc.cwnd == 1000 && cc.ssthresh == 5500);
    tju_cc_on_ack(&cc, 12000, 11000, 11000, 0, 12000);
    assert(!cc.loss_epoch);
    invariant(&cc);
}

static void test_newreno_multiple_losses(void) {
    tju_cc_t cc;
    initialize(&cc, TJU_CC_NEWRENO, 12000, 24000, 0);
    three_duplicates(&cc, 0, 12000, 12000);
    assert(cc.cwnd == 9000 && cc.ssthresh == 6000);
    /* Recover first hole, then retransmit second hole without waiting RTO. */
    assert(tju_cc_on_ack(&cc, 4000, 4000, 12000, 8000, 12000)
           == TJU_CC_PARTIAL_RETRANSMIT);
    assert(cc.cwnd == 6000 && cc.state == TJU_CC_FAST_RECOVERY);
    assert(cc.recover == 12000 && cc.ssthresh == 6000);
    tju_cc_on_dup_ack(&cc, 4000, 8000, 12000);
    assert(cc.cwnd == 7000);
    /* New sends beyond recover do not move this recovery's boundary. */
    assert(tju_cc_on_ack(&cc, 7000, 3000, 10000, 7000, 14000)
           == TJU_CC_PARTIAL_RETRANSMIT);
    assert(cc.cwnd == 5000 && cc.recover == 12000);
    assert(tju_cc_on_ack(&cc, 12000, 5000, 7000, 2000, 14000)
           == TJU_CC_NONE);
    assert(cc.cwnd == 3000); /* min(ssthresh, FlightSize + SMSS). */
    assert(cc.state == TJU_CC_SLOW_START && !cc.loss_epoch);
    invariant(&cc);
}

static void test_newreno_small_partial_and_full_ack(void) {
    tju_cc_t cc;
    initialize(&cc, TJU_CC_NEWRENO, 12000, 24000, 0);
    three_duplicates(&cc, 0, 12000, 12000);
    assert(tju_cc_on_ack(&cc, 500, 500, 12000, 11500, 12000)
           == TJU_CC_PARTIAL_RETRANSMIT);
    assert(cc.cwnd == 8500); /* A sub-SMSS partial ACK does not add SMSS. */
    assert(tju_cc_on_ack(&cc, 12000, 11500, 17500, 6000, 18000)
           == TJU_CC_NONE);
    assert(cc.cwnd == 6000 && cc.state == TJU_CC_CONGESTION_AVOIDANCE);
    assert(!cc.loss_epoch);
    /* Recovery whose entire flight was ACKed resumes with only one SMSS. */
    three_duplicates(&cc, 12000, 6000, 18000);
    tju_cc_on_ack(&cc, 18000, 6000, 6000, 0, 18000);
    assert(cc.cwnd == SMSS && cc.ssthresh == 3000);
}

static void test_newreno_timeout_suppresses_old_duplicates(void) {
    tju_cc_t cc;
    uint32_t i;
    initialize(&cc, TJU_CC_NEWRENO, 12000, 24000, 0);
    three_duplicates(&cc, 0, 12000, 12000);
    tju_cc_on_timeout(&cc, 12000, 12000, 0);
    assert(cc.cwnd == SMSS && cc.ssthresh == 6000);
    for (i = 0; i < 10; ++i) {
        assert(tju_cc_on_dup_ack(&cc, 0, 12000, 12000) == TJU_CC_NONE);
    }
    assert(cc.cwnd == SMSS && cc.ssthresh == 6000);
    tju_cc_on_ack(&cc, 1000, 1000, 12000, 11000, 12000);
    for (i = 0; i < 3; ++i) {
        assert(tju_cc_on_dup_ack(&cc, 1000, 11000, 12000) == TJU_CC_NONE);
    }
    assert(cc.ssthresh == 6000 && cc.state == TJU_CC_SLOW_START);
    tju_cc_on_ack(&cc, 12000, 11000, 11000, 0, 12000);
    assert(!cc.loss_epoch);
    three_duplicates(&cc, 12000, 8000, 20000);
    assert(cc.ssthresh == 4000 && cc.recover == 20000);
    /* An RTO must cover new data sent beyond the first recovery boundary. */
    tju_cc_on_timeout(&cc, 10000, 22000, 0);
    assert(cc.recover == 22000 && cc.ssthresh == 5000);
    tju_cc_on_ack(&cc, 20000, 8000, 10000, 2000, 22000);
    assert(cc.loss_epoch);
    for (i = 0; i < 3; ++i) {
        assert(tju_cc_on_dup_ack(&cc, 20000, 2000, 22000) == TJU_CC_NONE);
    }
    assert(cc.state == TJU_CC_SLOW_START && cc.ssthresh == 5000);
    tju_cc_on_ack(&cc, 22000, 2000, 2000, 0, 22000);
    assert(!cc.loss_epoch);
}

static void test_timeout_segment_identity(void) {
    tju_cc_t cc;
    int mode;
    for (mode = TJU_CC_BASIC; mode <= TJU_CC_NEWRENO; ++mode) {
        initialize(&cc, (tju_cc_mode_t)mode, 12000, 24000, 0);
        tju_cc_on_timeout(&cc, 12000, 12000, 0);
        assert(cc.ssthresh == 6000 && cc.cwnd == SMSS);
        tju_cc_on_timeout(&cc, 12000, 12000, 1);
        assert(cc.ssthresh == 6000);
        /* The next missing segment has never been retransmitted by RTO,
         * although the ACK has not covered the old recovery boundary. */
        tju_cc_on_ack(&cc, 4000, 4000, 12000, 8000, 12000);
        assert(cc.loss_epoch);
        tju_cc_on_timeout(&cc, 8000, 12000, 0);
        assert(cc.ssthresh == 4000 && cc.cwnd == SMSS);
        /* A partial ACK may trim the already retransmitted segment. Its
         * timer retry must preserve ssthresh despite the advancing ACK. */
        tju_cc_on_ack(&cc, 4500, 500, 8000, 7500, 12000);
        tju_cc_on_timeout(&cc, 7500, 12000, 1);
        assert(cc.ssthresh == 4000 && cc.cwnd == SMSS);
        /* Once that segment is fully ACKed, a different segment's first
         * timer retransmission recalculates the threshold again. */
        tju_cc_on_ack(&cc, 5000, 500, 7500, 7000, 12000);
        tju_cc_on_timeout(&cc, 7000, 12000, 0);
        assert(cc.ssthresh == 3500 && cc.recover == 12000);
        tju_cc_on_ack(&cc, 12000, 7000, 7000, 0, 12000);
        assert(!cc.loss_epoch);
        invariant(&cc);
    }
}

static void test_first_timeout_after_fast_retransmit(void) {
    tju_cc_t cc;
    int mode;
    for (mode = TJU_CC_BASIC; mode <= TJU_CC_NEWRENO; ++mode) {
        initialize(&cc, (tju_cc_mode_t)mode, 12000, 24000, 0);
        three_duplicates(&cc, 0, 12000, 12000);
        assert(cc.ssthresh == 6000);
        tju_cc_on_ack(&cc, 4000, 4000, 12000, 8000, 12000);
        /* In NewReno this hole was retransmitted on the partial ACK, but
         * that fast retransmission must not mark it as a timer retry. */
        tju_cc_on_timeout(&cc, 8000, 12000, 0);
        assert(cc.ssthresh == 4000 && cc.cwnd == SMSS);
        assert(cc.state == TJU_CC_SLOW_START && cc.loss_epoch);
        tju_cc_on_timeout(&cc, 8000, 12000, 1);
        assert(cc.ssthresh == 4000 && cc.cwnd == SMSS);
        invariant(&cc);
    }
}

static void test_new_fast_recovery_recalculates_threshold(void) {
    tju_cc_t cc;
    int mode;
    for (mode = TJU_CC_BASIC; mode <= TJU_CC_RENO; ++mode) {
        initialize(&cc, (tju_cc_mode_t)mode, 12000, 24000, 0);
        three_duplicates(&cc, 0, 12000, 12000);
        tju_cc_on_ack(&cc, 4000, 4000, 12000, 8000, 12000);
        assert(cc.cwnd == 6000 && cc.loss_epoch);
        /* Basic/Reno exited recovery on that advancing ACK. Another hole
         * starts a new recovery, unlike NewReno's continuing partial ACKs. */
        three_duplicates(&cc, 4000, 8000, 12000);
        assert(cc.ssthresh == 4000);
        assert(cc.cwnd == (mode == TJU_CC_BASIC ? 4000U : 7000U));
        tju_cc_on_dup_ack(&cc, 4000, 8000, 12000);
        assert(cc.ssthresh == 4000);
        tju_cc_on_ack(&cc, 7000, 3000, 8000, 5000, 12000);
        three_duplicates(&cc, 7000, 5000, 12000);
        assert(cc.ssthresh == 2500 && cc.recover == 12000);
        tju_cc_on_ack(&cc, 12000, 5000, 5000, 0, 12000);
        assert(cc.cwnd == 2500 && !cc.loss_epoch);
        invariant(&cc);
    }
}

static void test_sequence_wrap(void) {
    tju_cc_t cc;
    uint32_t base = UINT32_MAX - UINT32_C(2999);
    uint32_t high = base + UINT32_C(6000);
    initialize(&cc, TJU_CC_NEWRENO, 6000, 12000, base);
    three_duplicates(&cc, base, 6000, high);
    assert(cc.recover == 3000);
    assert(tju_cc_on_ack(&cc, base + 1000, 1000, 6000, 5000, high)
           == TJU_CC_PARTIAL_RETRANSMIT);
    assert(tju_cc_on_ack(&cc, 1000, 3000, 5000, 2000, high)
           == TJU_CC_PARTIAL_RETRANSMIT);
    assert(cc.loss_epoch);
    assert(tju_cc_on_ack(&cc, high, 2000, 2000, 0, high) == TJU_CC_NONE);
    assert(!cc.loss_epoch && cc.cwnd == SMSS);
    tju_cc_on_ack(&cc, base, 6000, 6000, 0, high); /* Old pre-wrap ACK. */
    assert(cc.last_ack == high && cc.cwnd == SMSS);
}

static void test_arithmetic_saturation(void) {
    tju_cc_t cc;
    initialize(&cc, TJU_CC_BASIC, UINT32_MAX - 500, UINT32_MAX, 0);
    tju_cc_on_ack(&cc, 1000, 1000, 1000, 0, 1000);
    assert(cc.cwnd == UINT32_MAX);
    tju_cc_on_ack(&cc, 2000, 1000, 1000, 0, 2000);
    assert(cc.cwnd == UINT32_MAX);
    tju_cc_init(&cc, TJU_CC_RENO, UINT32_MAX, 0, 0);
    assert(cc.cwnd == UINT32_MAX && cc.ssthresh == UINT32_MAX);
    three_duplicates(&cc, 0, UINT32_MAX, 1000);
    assert(cc.cwnd == UINT32_MAX && cc.ssthresh == UINT32_MAX);
    cc.dupacks = UINT32_MAX;
    tju_cc_on_dup_ack(&cc, 0, UINT32_MAX, 1000);
    assert(cc.dupacks == UINT32_MAX && cc.cwnd == UINT32_MAX);
    tju_cc_on_timeout(&cc, UINT32_MAX, 1000, 0);
    assert(cc.cwnd == UINT32_MAX && cc.ssthresh == UINT32_MAX);
}

static void test_mode_invariants(void) {
    tju_cc_t cc;
    uint32_t ack, i;
    int mode;
    for (mode = TJU_CC_BASIC; mode <= TJU_CC_NEWRENO; ++mode) {
        initialize(&cc, (tju_cc_mode_t)mode, SMSS, 64000, 0);
        ack = 0;
        for (i = 0; i < 500; ++i) {
            uint32_t flight = 16000;
            if (i % 37 == 0) {
                tju_cc_on_timeout(&cc, flight, ack + flight, 0);
            } else if (i % 11 == 0) {
                uint32_t j;
                for (j = 0; j < 5; ++j) {
                    tju_cc_on_dup_ack(&cc, ack, flight, ack + flight);
                    invariant(&cc);
                }
            }
            ack += SMSS;
            tju_cc_on_ack(&cc, ack, SMSS, flight, flight - SMSS,
                          ack + flight - SMSS);
            invariant(&cc);
        }
    }
}

static void test_cubic_growth_and_loss(void) {
    tju_cc_t cc;
    uint32_t before;
    initialize(&cc, TJU_CC_CUBIC, 1000, 4000, 0);
    before = cc.cwnd;
    tju_cc_on_ack(&cc, 1000, 1000, 4000, 3000, 4000);
    tju_cc_on_ack(&cc, 2000, 1000, 4000, 3000, 5000);
    assert(cc.cwnd >= before);
    three_duplicates(&cc, 2000, 6000, 8000);
    assert(cc.ssthresh >= 2000 && cc.cwnd >= cc.smss);
    tju_cc_on_timeout(&cc, 6000, 8000, 0);
    assert(cc.cwnd == cc.smss && cc.ssthresh >= 2 * cc.smss);
    invariant(&cc);
}

int main(void) {
    test_initialization_and_control_ack();
    test_slow_start_and_avoidance();
    test_ack_division_and_stale_ack();
    test_timeout_and_loss_epoch();
    test_basic_fast_retransmit();
    test_reno_fast_recovery();
    test_newreno_multiple_losses();
    test_newreno_small_partial_and_full_ack();
    test_newreno_timeout_suppresses_old_duplicates();
    test_timeout_segment_identity();
    test_first_timeout_after_fast_retransmit();
    test_new_fast_recovery_recalculates_threshold();
    test_sequence_wrap();
    test_arithmetic_saturation();
    test_mode_invariants();
    test_cubic_growth_and_loss();
    puts("congestion controller: 16 test groups passed (basic/Reno/NewReno/CUBIC)");
    return 0;
}
