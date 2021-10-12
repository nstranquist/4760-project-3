#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BAKERY_SIZE 99 // Each child may request multiple critical_section() actions

#define SLEEP_TIME 100

#define MAX_LICENSES 20

#define MAX_CANON 150

struct License {
  int nlicenses; // used the same as before
  int nlicenses_max; // defaults to MAX_LICENSES but can be lowered to 'n' supplied in CLI argument

  // bakery information
  int choosing[BAKERY_SIZE];
  int number[BAKERY_SIZE];
};
