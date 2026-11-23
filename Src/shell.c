#include "shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"
#include "cycles.h"
#include "main.h"

extern UART_HandleTypeDef huart2;

static int32_t (*read_mv)(void);
static char line[64];
static size_t len;

/* what the shell is doing between passes of main. a grab waits on the dma
   and a dump on the uart, and neither holds main up while it does */
static enum { IDLE, GRABBING, DUMPING } state;

/* bytes on their way out. printf fills tx from main and USART2's interrupt
   sends it, one stretch up to the end of the array at a time. head and
   tail count up forever, the index is that modulo the size */
static uint8_t tx[1024];
static volatile uint32_t tx_head, tx_tail;
static volatile uint32_t tx_sending; /* bytes in the transfer going, 0 idle */

/* start the next stretch. from the interrupt, or with it off */
static void tx_next(void)
{
  uint32_t tail = tx_tail, n = tx_head - tail, at = tail % sizeof tx;

  if (n > sizeof tx - at)
    n = sizeof tx - at;
  tx_sending = n;
  /* refused, the next putc_raw tries again */
  if (n > 0 && HAL_UART_Transmit_IT(&huart2, &tx[at], (uint16_t)n) != HAL_OK)
    tx_sending = 0;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart2)
    return;
  tx_tail += tx_sending;
  tx_next();
}

static uint32_t tx_room(void)
{
  return sizeof tx - (tx_head - tx_tail);
}

/* waits only when tx is full, while the interrupt empties it */
static void putc_raw(char c)
{
  while (tx_room() == 0) {
  }
  tx[tx_head % sizeof tx] = (uint8_t)c;
  tx_head++;
  /* the interrupt could end the last stretch between the test and the
     start, so neither may run without the other */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (tx_sending == 0)
    tx_next();
  __set_PRIMASK(primask);
}

/* syscalls.c sends printf here */
int __io_putchar(int ch)
{
  if (ch == '\n')
    putc_raw('\r');
  putc_raw((char)ch);
  return ch;
}

/* USART2's interrupt drops each byte in rx, getc_nb takes them out */
static uint8_t rx_byte;
static volatile uint8_t rx[64];
static volatile uint32_t rx_in, rx_out;
static volatile bool rx_lost; /* a byte came with rx full */
static volatile bool rx_cancel; /* ctrl-c, kept out of rx */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart2)
    return;
  if (rx_byte == 0x03)
    rx_cancel = true;
  else if (rx_in - rx_out < sizeof rx)
    rx[rx_in++ % sizeof rx] = rx_byte;
  else
    rx_lost = true;
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/* an overrun or a framing error ends the reception, start it again */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart2)
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/* -1 when nothing came in */
static int getc_nb(void)
{
  if (rx_in == rx_out)
    return -1;
  return rx[rx_out++ % sizeof rx];
}

static void cmd_help(void)
{
  printf("help                 this\n"
         "adc                  read A1 in mV\n"
         "rate [hz]            show or set the sample rate\n"
         "trig [off|mV [rise|fall]]\n"
         "                     show or set the trigger level and edge\n"
         "pre [percent]        how much of a grab comes before the trigger\n"
         "grab [n]             take n samples of A1, %u at most\n"
         "dump                 the last grab in mV, one per line\n"
         "ctrl-c               stop a grab or a dump\n",
         CAPTURE_LEN);
}

/* hysteresis either side of the trigger level */
#define HYST_MV 50U

static size_t grabbed;
static bool grab_new; /* for shell_new_grab */

#if SCOPE_GF2
/* gf2's waveform viewer reads float arrays only, capture_buf is raw counts */
float scope_mv[CAPTURE_LEN];
uint32_t scope_len;
#endif

void __attribute__((noinline)) shell_grabbed(void)
{
  __asm volatile("");
}
static bool trig_on;
static uint32_t trig_mv = VDDA_APPLI / 2U;
static bool trig_rise = true;
static uint32_t pre_pct = 50;

static uint32_t to_mv(uint32_t raw)
{
  return raw * VDDA_APPLI / 4095U;
}

static uint16_t to_raw(uint32_t mv)
{
  if (mv > VDDA_APPLI)
    mv = VDDA_APPLI;
  return (uint16_t)(mv * 4095U / VDDA_APPLI);
}

static void cmd_rate(const char *arg)
{
  uint32_t hz = (uint32_t)strtoul(arg, NULL, 10);

  if (hz > 0)
    capture_set_rate(hz);
  uint32_t rate = capture_rate_hz();
  printf("%lu Hz, %lu us for %u samples\n", (unsigned long)rate,
         (unsigned long)((uint64_t)CAPTURE_LEN * 1000000U / rate), CAPTURE_LEN);
}

static void cmd_trig(const char *arg)
{
  while (*arg == ' ')
    arg++;
  if (strcmp(arg, "off") == 0) {
    trig_on = false;
  } else if (*arg != '\0') {
    char *rest;
    trig_mv = (uint32_t)strtoul(arg, &rest, 10);
    if (trig_mv > VDDA_APPLI)
      trig_mv = VDDA_APPLI;
    while (*rest == ' ')
      rest++;
    if (strcmp(rest, "rise") == 0)
      trig_rise = true;
    else if (strcmp(rest, "fall") == 0)
      trig_rise = false;
    trig_on = true;
  }
  if (trig_on)
    printf("trigger %lu mV %s, %u mV hysteresis\n", (unsigned long)trig_mv,
           trig_rise ? "rise" : "fall", HYST_MV);
  else
    printf("trigger off, grabs run free\n");
}

static void cmd_pre(const char *arg)
{
  while (*arg == ' ')
    arg++;
  if (*arg != '\0') {
    pre_pct = (uint32_t)strtoul(arg, NULL, 10);
    if (pre_pct > 100)
      pre_pct = 100;
  }
  printf("%lu%% before the trigger\n", (unsigned long)pre_pct);
}

/* min, max, mean, and the frequency from rising crossings of the middle.
   the 1/8 of the swing either side keeps noise from counting twice */
/* what grab_done needs from cmd_grab */
static size_t grab_n;
static uint32_t grab_rate;

static void cmd_grab(const char *arg)
{
  size_t n = (size_t)strtoul(arg, NULL, 10);

  if (n == 0 || n > CAPTURE_LEN)
    n = CAPTURE_LEN;

  struct capture_trig t = {
    .level = to_raw(trig_mv),
    .hyst = to_raw(HYST_MV),
    .rise = trig_rise,
    .pre = (uint32_t)(n * pre_pct / 100U),
  };
  /* a failed run leaves the ring unturned, nothing to dump */
  grabbed = 0;
  if (capture_start(trig_on ? &t : NULL, n) < 0) {
    printf("grab failed\n");
    return;
  }
  grab_n = n;
  grab_rate = capture_rate_hz();
  state = GRABBING;
}

/* on every pass of main while a grab runs. the report once it is over */
static void grab_done(void)
{
  size_t at;
  struct capture_time tm;
  int got = capture_poll(&at, &tm);

  if (got == CAPTURE_RUNNING)
    return;
  state = IDLE;
  if (got < 0) {
    printf("grab failed\n> ");
    return;
  }
  size_t n = grab_n;
  uint32_t rate = grab_rate;
  grabbed = n;
#if SCOPE_GF2
  uint32_t t0 = cycles_now();
  for (size_t i = 0; i < n; i++)
    scope_mv[i] = (float)capture_buf[i] * VDDA_APPLI / 4095.0f;
  scope_len = (uint32_t)n;
  uint32_t copy = cycles_now() - t0;
#endif
  shell_grabbed();
  grab_new = true;

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
      if (rises++ == 0)
        first = i;
      last = i;
    }
  }
  uint32_t stats = cycles_now() - c;

  printf("%u samples at %lu Hz, %lu us\n", (unsigned)n, (unsigned long)rate,
         (unsigned long)((uint64_t)n * 1000000U / rate));
  if (got > 0)
    printf("triggered at sample %u\n", (unsigned)at);
  else if (trig_on)
    printf("no trigger in %u ms, took it anyway\n", CAPTURE_AUTO_MS);
  printf("min %lu  max %lu  mean %lu mV\n", (unsigned long)to_mv(lo),
         (unsigned long)to_mv(hi), (unsigned long)to_mv(sum / n));
  if (rises > 1 && band > 0)
    printf("about %lu Hz\n",
           (unsigned long)((uint64_t)(rises - 1) * rate / (last - first)));
  else
    printf("no full cycle in the window\n");

  printf("took %lu us, %lu dma %s, rotate %lu us\n",
         (unsigned long)cycles_to_us(tm.total), (unsigned long)tm.loops,
         SCOPE_IRQ ? "interrupts" : "checks",
         (unsigned long)cycles_to_us(tm.rotate));
  if (SCOPE_IRQ)
    printf("in the dma interrupts %lu us\n",
           (unsigned long)cycles_to_us(tm.isr));
  if (tm.searched > 0) {
    uint32_t per = (uint32_t)((uint64_t)tm.search * 100U / tm.searched);
    printf("edge search %lu.%02lu cycles a sample over %lu samples\n",
           (unsigned long)(per / 100U), (unsigned long)(per % 100U),
           (unsigned long)tm.searched);
  }
#if SCOPE_GF2
  printf("float copy %lu us\n", (unsigned long)cycles_to_us(copy));
#endif
  printf("stats %lu us\n> ", (unsigned long)cycles_to_us(stats));
}

/* a whole dump is about 20 KB, near 2 s at 115200. it goes a few lines a
   pass of main, as fast as tx empties */
static size_t dump_at;

static void cmd_dump(void)
{
  dump_at = 0;
  state = DUMPING;
}

static void dump_more(void)
{
  /* "4095\r\n" is the longest line */
  while (dump_at < grabbed && tx_room() >= 6)
    printf("%lu\n", (unsigned long)to_mv(capture_buf[dump_at++]));
  if (dump_at == grabbed) {
    state = IDLE;
    printf("> ");
  }
}

static void run(char *cmd)
{
  /* adc grabs into the same buffer */
  if (strncmp(cmd, "adc", 3) == 0)
    grabbed = 0;

  if (strcmp(cmd, "help") == 0)
    cmd_help();
  else if (strcmp(cmd, "adc") == 0)
    printf("adc %ld mV\n", (long)read_mv());
  else if (strcmp(cmd, "rate") == 0 || strncmp(cmd, "rate ", 5) == 0)
    cmd_rate(cmd + 4);
  else if (strcmp(cmd, "trig") == 0 || strncmp(cmd, "trig ", 5) == 0)
    cmd_trig(cmd + 4);
  else if (strcmp(cmd, "pre") == 0 || strncmp(cmd, "pre ", 4) == 0)
    cmd_pre(cmd + 3);
  else if (strcmp(cmd, "grab") == 0 || strncmp(cmd, "grab ", 5) == 0)
    cmd_grab(cmd + 4);
  else if (strcmp(cmd, "dump") == 0)
    cmd_dump();
  else if (cmd[0] != '\0')
    printf("%s? try help\n", cmd);
  /* a grab or a dump prints the prompt when it is over */
  if (state == IDLE)
    printf("> ");
}

void shell_init(int32_t (*read)(void))
{
  read_mv = read;
  /* newlib holds a line until '\n'. the prompt and the backspace echo have
     none, and putc_raw buffers anyway */
  setvbuf(stdout, NULL, _IONBF, 0);
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
  printf("\nscope shell, type help\n> ");
}

bool shell_busy(void)
{
  return state != IDLE;
}

bool shell_new_grab(void)
{
  bool b = grab_new;
  grab_new = false;
  return b;
}

size_t shell_frame_len(void)
{
  return state == IDLE ? grabbed : 0;
}

/* typed bytes wait in rx while a grab or a dump runs. otherwise it takes
   all that came in since the last pass, so a slow pass loses nothing */
void shell_poll(void)
{
  if (rx_cancel) {
    /* ctrl-c ends a grab, a dump or the line being typed */
    rx_cancel = false;
    if (state == GRABBING) {
      capture_abort();
      grabbed = 0;
    }
    state = IDLE;
    len = 0;
    printf("^C\n> ");
  }

  if (state == GRABBING)
    grab_done();
  else if (state == DUMPING)
    dump_more();

  if (state == IDLE && rx_lost) {
    /* what is in rx is the start of a line with its middle missing */
    rx_lost = false;
    rx_out = rx_in;
    len = 0;
    printf("\ntyped too much while busy, input dropped\n> ");
  }

  int c;
  while (state == IDLE && (c = getc_nb()) >= 0) {
    if (c == '\r' || c == '\n') {
      printf("\n");
      line[len] = '\0';
      len = 0;
      run(line);
    } else if ((c == '\b' || c == 0x7F) && len > 0) {
      len--;
      printf("\b \b");
    } else if (c >= ' ' && c < 0x7F && len < sizeof line - 1) {
      line[len++] = (char)c;
      putc_raw((char)c);
    }
  }
}
