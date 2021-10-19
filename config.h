#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SLEEP_TIME 100

#define MAX_LICENSES 20

// define MAX_CANON only if it is not already defined in <limits.h>
#ifndef MAX_CANON
#define MAX_CANON 150
#endif

struct License {
  int nlicenses; // used the same as before
  int nlicenses_max; // defaults to MAX_LICENSES but can be lowered to 'n' supplied in CLI argument
};
