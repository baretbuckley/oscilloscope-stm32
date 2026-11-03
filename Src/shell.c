#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dac.h"
#include "main.h"

#define BAUD 115200U
#define TX_PIN 2U
#define RX_PIN 3U
#define AF_USART2 7U

#define SWEEP_STEP_MV 300U
#define SWEEP_SETTLE_MS 5U

static int32_t (*read_mv)(void);
static char line[64];
static size_t len;

static void usart2_init(void)
{
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
  RCC->APB1LENR |= RCC_APB1LENR_USART2EN;
  (void)RCC->APB1LENR;

  /* alternate function mode, 0b10 */
  GPIOA->MODER = (GPIOA->MODER & ~(3U << (TX_PIN * 2U) | 3U << (RX_PIN * 2U)))
               | 2U << (TX_PIN * 2U) | 2U << (RX_PIN * 2U);
  GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFU << (TX_PIN * 4U) | 0xFU << (RX_PIN * 4U)))
                | AF_USART2 << (TX_PIN * 4U) | AF_USART2 << (RX_PIN * 4U);

  /* the kernel clock is pclk1 out of reset */
  USART2->BRR = (HAL_RCC_GetPCLK1Freq() + BAUD / 2U) / BAUD;
  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void putc_raw(char c)
{
  while (!(USART2->ISR & USART_ISR_TXE_TXFNF))
    ;
  USART2->TDR = (uint8_t)c;
}

/* syscalls.c sends printf here */
int __io_putchar(int ch)
{
  if (ch == '\n')
    putc_raw('\r');
  putc_raw((char)ch);
  return ch;
}

/* -1 when nothing came in */
static int getc_nb(void)
{
  if (USART2->ISR & USART_ISR_ORE)
    USART2->ICR = USART_ICR_ORECF;
  if (!(USART2->ISR & USART_ISR_RXNE_RXFNE))
    return -1;
  return (int)(USART2->RDR & 0xFFU);
}

static void cmd_help(void)
{
  printf("help        this\n"
         "adc         read A1 - A0 in mV\n"
         "dac <mV>    set D13 to 0..%u mV\n"
         "sweep       step D13 and read it back, jumper D13 to A1, A0 to GND\n",
         VDDA_APPLI);
}

static void cmd_dac(const char *arg)
{
  char *end;
  long mv = strtol(arg, &end, 10);

  if (end == arg || mv < 0 || mv > (long)VDDA_APPLI) {
    printf("dac 0..%u\n", VDDA_APPLI);
    return;
  }
  dac_write_mv((uint32_t)mv);
  printf("dac %ld mV\n", mv);
}

static void cmd_sweep(void)
{
  for (uint32_t mv = 0; mv <= VDDA_APPLI; mv += SWEEP_STEP_MV) {
    dac_write_mv(mv);
    HAL_Delay(SWEEP_SETTLE_MS);
    int32_t got = read_mv();
    printf("dac %4lu mV  adc %4ld mV  off %4ld\n",
           (unsigned long)mv, (long)got, (long)got - (long)mv);
  }
  dac_write_mv(0);
}

static void run(char *cmd)
{
  if (strcmp(cmd, "help") == 0)
    cmd_help();
  else if (strcmp(cmd, "adc") == 0)
    printf("adc %ld mV\n", (long)read_mv());
  else if (strncmp(cmd, "dac ", 4) == 0)
    cmd_dac(cmd + 4);
  else if (strcmp(cmd, "sweep") == 0)
    cmd_sweep();
  else if (cmd[0] != '\0')
    printf("%s? try help\n", cmd);
}

void shell_init(int32_t (*read)(void))
{
  read_mv = read;
  usart2_init();
  dac_init();
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
