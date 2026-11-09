#include "shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"
#include "main.h"

extern UART_HandleTypeDef huart2;

static int32_t (*read_mv)(void);
static char line[64];
static size_t len;

static void putc_raw(char c)
{
  HAL_UART_Transmit(&huart2, (const uint8_t *)&c, 1, HAL_MAX_DELAY);
}

/* syscalls.c sends printf here */
int __io_putchar(int ch)
{
  if (ch == '\n')
    putc_raw('\r');
  putc_raw((char)ch);
  return ch;
}

/* -1 when nothing came in. a zero timeout only looks, and returns before
   HAL would clear an overrun, so clear it here */
static int getc_nb(void)
{
  uint8_t c;

  __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF);
  if (HAL_UART_Receive(&huart2, &c, 1, 0) != HAL_OK)
    return -1;
  return c;
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
         "dump                 the last grab in mV, one per line\n",
         CAPTURE_LEN);
}

/* hysteresis either side of the trigger level */
#define HYST_MV 50U

static size_t grabbed;
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
         (unsigned long)((uint64_t)CAPTURE_LEN * 1000000U / rate),
         CAPTURE_LEN);
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
static void cmd_grab(const char *arg)
{
  size_t n = (size_t)strtoul(arg, NULL, 10);
  uint32_t rate = capture_rate_hz();

  if (n == 0 || n > CAPTURE_LEN)
    n = CAPTURE_LEN;

  struct capture_trig t = {
    .level = to_raw(trig_mv),
    .hyst = to_raw(HYST_MV),
    .rise = trig_rise,
    .pre = (uint32_t)(n * pre_pct / 100U),
  };
  size_t at;
  int got = capture_run(trig_on ? &t : NULL, n, &at);
  if (got < 0) {
    printf("grab failed\n");
    return;
  }
  grabbed = n;

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
}

static void cmd_dump(void)
{
  for (size_t i = 0; i < grabbed; i++)
    printf("%lu\n", (unsigned long)to_mv(capture_buf[i]));
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
}

void shell_init(int32_t (*read)(void))
{
  read_mv = read;
  printf("\nscope shell, type help\n> ");
}

void shell_poll(void)
{
  int c = getc_nb();

  if (c < 0)
    return;

  if (c == '\r' || c == '\n') {
    printf("\n");
    line[len] = '\0';
    run(line);
    len = 0;
    printf("> ");
  } else if ((c == '\b' || c == 0x7F) && len > 0) {
    len--;
    printf("\b \b");
  } else if (c >= ' ' && c < 0x7F && len < sizeof line - 1) {
    line[len++] = (char)c;
    putc_raw((char)c);
  }
}
