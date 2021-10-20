#include "license.h"
#include "config.h"

struct License *nlicenses;

// Note: All of these functions are critical sections

// Blocks until a license is available
int getlicense(void) {
  sleep(1);
  if(nlicenses->nlicenses <= 0)
    return 1;

  printf("licenses: %d\n", nlicenses->nlicenses);

  return 0;
}

// Increments the # of licenses available
int returnlicense(void) {
  sleep(1);

  if(nlicenses->nlicenses >= nlicenses->nlicenses_max || nlicenses->nlicenses >= MAX_LICENSES) {
    return 1;
  }

  nlicenses->nlicenses = nlicenses->nlicenses + 1;

  return 0;
}

// Performs any needed initialization of the license object
int initlicense(int max) {
  sleep(1);

  // set both to max initially, but only nlicenses will change up and down
  nlicenses->nlicenses = max;
  nlicenses->nlicenses_max = max;
  return 0;
}

// Adds n licenses to the number available
void addtolicenses(int n) {
  sleep(1);

  if(nlicenses->nlicenses + n > nlicenses->nlicenses_max) {
    return;
  }
  if(n < 0) {
    return;
  }

  nlicenses->nlicenses = nlicenses->nlicenses + n;
}

// Decrements the number of licenses by n
void removelicenses(int n) {
  sleep(1);

  if(nlicenses->nlicenses - n < 0) {
    nlicenses->nlicenses = 0;
    return;
  }
  if(n < 0) {
    return;
  }

  nlicenses->nlicenses = nlicenses->nlicenses - n;
}

/***
 * Write the specified message to the log file.
 * There's only 1 log file.
 * This function will treat the log file as a critical resource.
 * It will open the file to append the message, and close the file after appending the message.
 */
void logmsg(const char * msg) {
  char *filename = "runsim.log";

  // Open the log file
  FILE * fp = fopen(filename, "a");

  if(fp == NULL) {
    perror("runsim: Error: Could not open log file %s for writing.\n");
    return;
  }

  // Write the message to the log file
  fprintf(fp, "%s\n", msg);

  // Close the log file
  fclose(fp);
}