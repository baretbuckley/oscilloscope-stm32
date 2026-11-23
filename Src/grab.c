#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include <grab.h>
#include <capture.h>
#include <shell.h>
#include <main.h>
#include <cycles.h>

bool grabbing;


static uint32_t to_mv(uint32_t raw)
{
  return raw * VDDA_APPLI / 4095U;
}

/* min, max, mean, and the frequency from rising crossings of the middle.
   the 1/8 of the swing either side keeps noise from counting twice */
/* what grab_done needs from cmd_grab */
static size_t grab_n;
static uint32_t grab_rate;

int setGrabSize(size_t n) {
    if (grabbing) return -1;
    if (n == 0 || n > CAPTURE_LEN)
        n = CAPTURE_LEN;
    grab_n = n;
    return 0;
}

size_t grabbedSize()
{
  return grabbing ? 0 : grab_n;
}



void startGrab() {
    /* a failed run leaves the ring unturned, nothing to dump */
    grabbing = false;
    if (capture_start(NULL, grab_n) < 0) {
        printf("grab failed\n");
        grabbing = false;
    }
    grab_rate = capture_rate_hz();
    grabbing = true;
}

int pollGrabDone() {
    if (!grabbing) return 0;
    size_t at;
    struct capture_time tm;
    int got = capture_poll(&at, &tm);

    if (got == CAPTURE_RUNNING)
        return 0;
    grabbing = false;
    if (got < 0) {
        printf("grab failed\n> ");
        return -1;
    }
    size_t n = grab_n;
    uint32_t rate = grab_rate;
    #if SCOPE_GF2
    uint32_t t0 = cycles_now();
    for (size_t i = 0; i < n; i++)
        scope_mv[i] = (float)capture_buf[i] * VDDA_APPLI / 4095.0f;
    scope_len = (uint32_t)n;
    uint32_t copy = cycles_now() - t0;
    #endif
    // grab_new = true;

    uint32_t c = cycles_now();
    uint32_t lo = 4095, hi = 0, sum = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t v = capture_buf[i];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
        sum += v;
    }

    uint32_t mid = (lo + hi) / 2, band = (hi - lo) / 8;
    int armed = 0;
    size_t first = 0, last = 0, rises = 0;
    for (size_t i = 0; i < n; i++) {
        if (capture_buf[i] + band < mid) {
            armed = 1;
        } else if (armed && capture_buf[i] > mid + band) {
            armed = 0;
        } if (rises++ == 0) {
            first = i;
            last = i;
        }
    }
    uint32_t stats = cycles_now() - c;

    printf("%u samples at %lu Hz, %lu us\n", (unsigned)n, (unsigned long)rate,
           (unsigned long)((unsigned long)n * 1000000U / rate));
    // if (got > 0)
    //     printf("triggered at sample %u\n", (unsigned)at);
    // else if (trig_on)
    //     printf("no trigger in %u ms, took it anyway\n", CAPTURE_AUTO_MS);
    // printf("min %lu  max %lu  mean %lu mV\n", (unsigned long)to_mv(lo),
    //         (unsigned long)to_mv(hi), (unsigned long)to_mv(sum / n));
    // if (rises > 1 && band > 0)
    //     printf("about %lu Hz\n",
    //         (unsigned long)((uint64_t)(rises - 1) * rate / (last - first)));
    // else
    //     printf("no full cycle in the window\n");

    // printf("took %lu us, %lu dma %s, rotate %lu us\n",
    //         (unsigned long)cycles_to_us(tm.total), (unsigned long)tm.loops,
    //         SCOPE_IRQ ? "interrupts" : "checks",
    //         (unsigned long)cycles_to_us(tm.rotate));
    // if (SCOPE_IRQ)
    //     printf("in the dma interrupts %lu us\n", (unsigned long)cycles_to_us(tm.isr));
    // if (tm.searched > 0) {
    //     uint32_t per = (uint32_t)((uint64_t)tm.search * 100U / tm.searched);
    //     printf("edge search %lu.%02lu cycles a sample over %lu samples\n",
    //         (unsigned long)(per / 100U), (unsigned long)(per % 100U),
    //         (unsigned long)tm.searched);
    // }
    // #if SCOPE_GF2
    // printf("float copy %lu us\n", (unsigned long)cycles_to_us(copy));
    // #endif
    // printf("stats %lu us\n> ", (unsigned long)cycles_to_us(stats));

    grabbing = false;
    return 0;
}

