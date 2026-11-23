#ifndef GRAB_H
#define GRAB_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>



extern bool grabbing;

void startGrab(size_t n);

int pollGrabDone();

size_t shell_frame_len();

#endif