/**
 * TestSim Application:
 * - Takes 2 CLI arguments: Sleep time and Repeat factor
 *    --> Repeat factor is the number of times "testsim" iterates a loop
 *   --> Sleep time is the number of seconds between each iteration
 * - In each iteration of the loop, "testsim" sleeps for the specified amount of time, then outputs a message with its `pid` to logfile using logmsg in the format
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include "config.h"
#include "license.h"

int printToFile(char *filename, char *msg);
char *getFormattedTime();

int main(int argc, char** argv) {
  // hardcode name for now
  char *logfile = "testsim.log";

  // Read, validate CLI arguments
  // print args
  // for (int i = 0; i < argc; i++) {
  //   printf("argv[%d]: %s\n", i, argv[i]);
  // }
  printf("\n");

  // validate cli arguments
  if (argc != 3) {
    printf("runsim: Error: Invalid number of arguments.\n");
    fprintf(stderr, "Usage: testsim <sleep time> <repeat factor>\n");
    return 1;
  }

  // convert cli arguments to ints
  int sleepTime = atoi(argv[1]);
  int repeatFactor = atoi(argv[2]);

  // validate cli arguments
  if (sleepTime < 0 || repeatFactor < 0) {
    printf("runsim: Error: Invalid arguments. Must be positive integers\n");
    fprintf(stderr, "Usage: testsim <sleep time> <repeat factor>\n");
    return 1;
  }

  char *message;

  // Enter loop for number of iterations
  for (int i = 0; i < repeatFactor; i++) {
    // sleep for sleepTime seconds
    printf("\nSleeping for %d seconds\n", sleepTime);
    sleep(sleepTime);
    
    // generate char* message of the time, pid, iteration #
    char *time = getFormattedTime();
    int pid = getpid();

    // concatenate message
    char *message = malloc(strlen(time) + sizeof(long) + sizeof(int) + strlen("/") + sizeof(int) + 1);
    sprintf(message, "%s %d %d/%d", time, pid, (i+1), repeatFactor);

    // print time to file
    if (printToFile(logfile, message) == -1) {
      printf("testsim: Error: Could not print to file\n");
    }
    else {
      printf("testsim: Success: Printed message to file: %s\n", message);
    }
  }

  return 0;
}

int printToFile(char *filename, char *msg) {
  FILE *log = fopen(filename, "a");
  if (log == NULL) {
    printf("testsim: Error: Can't open logfile\n");
    return -1;
  }
  fprintf(log, "%s\n", msg);
  fclose(log);
  return 0;
}

char * getFormattedTime() {
  time_t tm = time(NULL);
  time(&tm);
  struct tm *tp = localtime(&tm);

  char time_str [9];
  sprintf(time_str, "%.2d:%.2d:%.2d", tp->tm_hour, tp->tm_min, tp->tm_sec);

  int msg_length = strlen(time_str);

  char *formatted_msg = (char*)malloc(msg_length * sizeof(char));

  sprintf(formatted_msg, "%s", time_str);

  return formatted_msg;
}