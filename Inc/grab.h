#ifndef GRAB_H
#define GRAB_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>


int setGrabSize(size_t n);

size_t grabbedSize();

extern bool grabbing;

void startGrab();

int pollGrabDone();

size_t shell_frame_len();

#endif