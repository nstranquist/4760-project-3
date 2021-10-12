/**
 * Author: Nico Stranquist
 * Date: September 25, 2021
 * 
 *
 * Implement 'runsim' as follows:
 * 1. Check CLI arguments and output a usage message if argument is not appropriate
 * 2. Allocate shared memory and populate it with the number of licenses from Step 1
 * 3. Execute the main loop until stdin is end-of-file is reached on stdin
 *  a. Read a line from stdin of up to MAX_CANON characters (fgets)
 *  b. Request a license from the License object
 *  c. Fork a child that does 'docommand'. docommand will request a license from the license manager object.
 *      Notice that if the license is not available, the request function will go into wait state.
 *  d. Pass the input string from step (a) to docommand. The docommand function will execl the specified command
 *  e. The parent (runsim) checks to see if any of the children have finished (waitpid with WNOHANG option).
 *      It will `returnlicense` when that happens
 *  f. After encountering EOF on stdin, `wait` for all the remaining children to finish and then exit
    // You are required to use fork, exec (or one of its variants) ,wait, and exit to manage multiple processes.

    // You will need to set up shared memory in this project to allow the processes to communicate with each other.
    // Please check the man pages for shmget, shmctl, shmat, and shmdt to work with shared memory.
 */

#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h> // for optional parameters
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "config.h"
#include "license.h"


// Function definitions
void docommand(char *cline);
int detachandremove(int shmid, void *shmaddr);
char * getTimeFormattedMessage(char *msg);

// Bakery functions and bakery helpers
int max(int *array, int size);
int process_i(const int i, int functionType, ...); // int (*function_ptr)(int first, ...)
int getNextZero(int *array, int size);
int getBakeryPlace();

int shmid;
void *shmaddr;

extern struct License *nlicenses;

static void myhandler(int signum) {
  if(signum == SIGINT) {
    // is ctrl-c interrupt
    printf("\nrunsim: Ctrl-C Interrupt Detected. Shutting down gracefully...\n");
  }
  else if(signum == SIGPROF) {
    // is timer interrupt
    printf("\nrunsim: The time for this program has expired. Shutting down gracefully...\n");
  }
  else {
    printf("\nrunsim: Warning: Only Ctrl-C and Timer signal interrupts are being handled.\n");
    return;
  }

  // free memory and exit
  nlicenses = (struct License *)shmat(shmid, NULL, 0);

  // Print time to logfile before exit
  char *msg = getTimeFormattedMessage(" - Termination");

  void (*function_ptr)(const char*);
  function_ptr = logmsg;
  int place = getBakeryPlace(); // can ignore the return value
  process_i(place, 4, function_ptr, msg);

  int result = detachandremove(shmid, nlicenses);

  pid_t group_id = getpgrp();
  killpg(group_id, signum);

  if(result == -1) {
    fprintf(stderr, "runsim: Error: Failure to detach and remove memory\n");
  }

	exit(1);
}

static int setupinterrupt(void) {
  struct sigaction act;
  act.sa_handler = myhandler;
  act.sa_flags = 0;
  return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void) {
  struct itimerval value;
  value.it_interval.tv_sec = SLEEP_TIME;
  value.it_interval.tv_usec = 0;
  value.it_value = value.it_interval;
  return (setitimer(ITIMER_PROF, &value, NULL));
}

// Write a runsim program that runs up to n processes at a time. Start the runsim program by typing the following command
int main(int argc, char *argv[]) {
  // Validate CLI Arguments
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <number-of-licenses>\n", argv[0]);
    return -1;
  }
  if(!atoi(argv[1])) {
    fprintf(stderr, "Usage: %s <number-of-licenses>, where n is an integer\n", argv[0]);
    return -1;
  }

  // parse argv[1] to get the number of licenses available at the same time
  int nlicensesInput = atoi(argv[1]);

  if(nlicensesInput < 0) {
    fprintf(stderr, "Usage: %s <number-of-licenses>, where n is an integer >= 0\n", argv[0]);
    return -1;
  }
  else if(nlicensesInput > MAX_LICENSES) {
    printf("runsim: Warning: Max Licenses at a time is %d\n", MAX_LICENSES);
    nlicensesInput = MAX_LICENSES;
  }

  // Set up timers and interrupt handler
  int err = setupitimer();
  if (err) {
    perror("runsim: Error: setupitimer");
    return -1;
  }
  err = setupinterrupt();
  if (err) {
    perror("runsim: Error: setupinterrupt");
    return -1;
  }

  signal(SIGINT, myhandler);

  printf("%d licenses specified\n", nlicensesInput);

  // allocate shared memory
  shmid = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
  printf("shmid: %d\n", shmid);
  if (shmid == -1) {
    perror("runsim: Error: Failed to create shared memory segment");
    return -1;
  }

  // attach shared memory
  nlicenses = (struct License *)shmat(shmid, NULL, 0);
  if (nlicenses == (void *) -1) {
    perror("runsim: Error: Failed to attach to shared memory");
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
      perror("runsim: Error: Failed to remove memory segment");
    return -1;
  }

  // initialize n licenses with bakery
  int (*function_ptr_init)(int);
  function_ptr_init = initlicense;

  int place = getBakeryPlace();

  // queue into the bakery
  process_i(place, 2, function_ptr_init, nlicensesInput); // initialize licenses with nLicensesInput

  printf("\n");

  char cline[MAX_CANON];

  // Main Loop until EOF reached
  while (fgets(cline, MAX_CANON, stdin) != NULL) {
    printf("\n");

    // 1. Fork a child that calls docommand
    pid_t child_pid = fork();
 
    if (child_pid == -1) {
      perror("runsim: Error: Failed to fork a child process");
      if (detachandremove(shmid, nlicenses) == -1)
        perror("runsim: Error: Failed to detach and remove shared memory segment");
      return -1;
    }

    // child's code if pid is 0
    if (child_pid == 0) {
      // attach memory again, because you are the child
      nlicenses = (struct License *)shmat(shmid, NULL, 0); 

      // Call docommand child
      docommand(cline);
    }
    // parent's code if child_pid > 0
    else {
      // parent waits inside loop for child to finish
      int status;
      pid_t wpid = waitpid(child_pid, &status, WNOHANG);
      if (wpid == -1) {
        perror("runsim: Error: Failed to wait for child");
        return -1;
      }
      else if(wpid == 0) {
        // child is still running
        // printf("Child is still running\n");
      }
      else {
        // return license with bakery
        int (*function_ptr)();
        function_ptr = returnlicense;

        place = getBakeryPlace();

        // queue into the bakery
        int result = process_i(place, 1, function_ptr);

        if(result == 1) {
          printf("runsim: Error: Failed to return license\n");
        }
      }
    }
  }

  // Wait for all children to finish, after the main loop is complete
  while(wait(NULL) > 0) {
    printf("Waiting for all children to finish\n");
  }

  nlicenses = (struct License *)shmat(shmid, NULL, 0);
  if (nlicenses == (void *) -1) {
    perror("runsim: Error: Failed to attach to shared memory");
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
      perror("runsim: Error: Failed to remove memory segment");
    return -1;
  }

  // log message before final termination
  
  char *msg = getTimeFormattedMessage(" - Termination");

  // bakery
  void (*function_ptr)(const char*);
  function_ptr = logmsg;
  place = getBakeryPlace(); // can ignore the return value
  process_i(place, 4, function_ptr, msg);
  
  if(detachandremove(shmid, nlicenses) == -1) {
    perror("runsim: Error: Failed to detach and remove shared memory segment");
    return -1;
  }

  return 0;
}

void docommand(char *cline) {
  int place;
  printf("received in docammand: %s\n", cline);

  // check if license available as well
  
  // get function pointer for getlicense() to put into the bakery
  int (*function_ptr_get)();
  function_ptr_get = getlicense;
  place = getBakeryPlace();
  int result = process_i(place, 1, function_ptr_get);

  // busy wait for available license
  while(result == 1) {
    sleep(1);
    result = process_i(place, 1, function_ptr_get);
  }

  // actually consume the license for the process using bakery
  void (*function_ptr_remove)(int);
  function_ptr_remove = removelicenses;

  place = getBakeryPlace();

  // queue into the bakery
  process_i(place, 3, function_ptr_remove, 1);

  // Fork to grand-child
  pid_t grandchild_id = fork();

  printf("forked grandchild: %d\n", grandchild_id);

  if (grandchild_id == -1) {
    perror("runsim: Error: Failed to fork grand-child process");
    int (*function_ptr_return)();
    function_ptr_return = returnlicense;
    place = getBakeryPlace(); // can ignore the return value
    int result = process_i(place, 1, function_ptr_return);
    return;
  }
  else if (grandchild_id == 0) {
    // In grand-child:
    // reattach memory
    nlicenses = (struct License *)shmat(shmid, NULL, 0);
    
    // get first word from cline
    char *command = strtok(cline, " ");
    // get the rest of the words in the line
    char *arg2 = strtok(NULL, " ");
    char *arg3 = strtok(NULL, " ");

    // execl the command
    execl(command, command, arg2, arg3, (char *) NULL);
    perror("runsim: Error: Failed to execl");
  }
  else {
    // in parent
    int grandchild_status;

    waitpid(grandchild_id, &grandchild_status, 0);
    printf("Grand child finished, result: %d\n", WEXITSTATUS(grandchild_status));

    int (*function_ptr_return)();
    function_ptr_return = returnlicense;
    place = getBakeryPlace(); // can ignore the return value
    int result = process_i(place, 1, function_ptr_return);

    if(result == 1) {
      printf("licenses: %d\n", nlicenses->nlicenses);
    }
  }

  exit(0);
}

// gets the next place in line for the bakery, executes the bakery, then returns the result
int getBakeryPlace() {
  int place = getNextZero(nlicenses->number, BAKERY_SIZE);
  while(place == -1) {
    sleep(1);
    place = getNextZero(nlicenses->number, BAKERY_SIZE);
  }

  printf("\ngot place: %d\n\n", place);

  return place;
}

// From textbook
int detachandremove(int shmid, void *shmaddr) {
  printf("cleaning up id %d\n", shmid);

  int error = 0;

  if (shmdt(shmaddr) == -1) {
    fprintf(stderr, "runsim: Error: Can't detach memory\n");
    error = errno;
  }
  
  if ((shmctl(shmid, IPC_RMID, NULL) == -1) && !error) {
    fprintf(stderr, "runsim: Error: Can't remove memory\n");
    error = errno;
  }

  if (!error)
    return 0;

  errno = error;

  return -1;
}

char * getTimeFormattedMessage(char *msg) {
  time_t tm = time(NULL);
  time(&tm);
  struct tm *tp = localtime(&tm);

  char time_str [9];
  sprintf(time_str, "%.2d:%.2d:%.2d", tp->tm_hour, tp->tm_min, tp->tm_sec);

  int msg_length = strlen(msg) + strlen(time_str);

  char *formatted_msg = (char*)malloc(msg_length * sizeof(char));

  sprintf(formatted_msg, "%s%s", time_str, msg);

  return formatted_msg;
}

// Bakery Algorithm
// - easier algorithm to understand and implement
// - each process has a unique id (bitmap array)
// - process id is assigned in a completely ordered manner
// - unsigned character has 8 bits, can handle 7 processes
// Can see exercise 5.10, page 251

// optional parameters documentation:
// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/va-arg-va-copy-va-end-va-start

int process_i(const int i, int functionType, ...) {
  do {
    nlicenses->choosing[i] = 1; // 1 is true
    nlicenses->number[i] = 1 + max(nlicenses->number, BAKERY_SIZE); // get highest number within array and add 1, to define a new turn
    nlicenses->choosing[i] = 0; // 0 is false

    for(int j = 0; j < BAKERY_SIZE; j++) {
      while(nlicenses->choosing[j]); // wait while someone else is choosing
      while((nlicenses->number[j]) && (nlicenses->number[j], j) < (nlicenses->number[i], j)); // (a,b) < (c,d) === (a < c) || ((a == c) && (b < d))
    }

    // enter critical section (using function pointer)

    // process optional parameters, with "-1" as a terminator
    int i = functionType;
    va_list marker;
    va_start(marker, functionType);

    // if functionType == 1, assume int function(void);
    // if functionType == 2, assume int function(int);
    // if functionType == 3, assume void function(int);
    // if functionType == 4, assume void function(const char *);

    int result = -1;
    int n;

    // had to declare here to avoid error: "a label can only be part of a statement and a declaration is not a statement"
    int (*function_ptr_1)(void);
    int (*function_ptr_2)(int);
    void (*function_ptr_3)(int);
    void (*function_ptr_4)(const char*);

    switch(functionType) {
      case 1:
        function_ptr_1 = va_arg(marker, int(*)(void));
        result = function_ptr_1();
        break;
      case 2:
        function_ptr_2 = va_arg(marker, int(*)(int));
        // get the optional params for the int
        n = va_arg(marker, int);
        result = function_ptr_2(n);
        break;
      case 3:
        function_ptr_3 = va_arg(marker, void(*)(int));
        // get the optional params for the int
        n = va_arg(marker, int);
        function_ptr_3(n);
        break;
      case 4:
        function_ptr_4 = va_arg(marker, void(*)(const char*));
        // get the optional params for the int
        const char *str = va_arg(marker, const char*);
        function_ptr_4(str);
        break;
      default:
        printf("runsim: Warning: Only options 1-4 are valid for process_i\n");
        break;
    }

    // exit critical section
    nlicenses->number[i] = 0; // resets turn number
    
    return result;
  } while(1);
}

// gets the highest number in an integer array
int max(int *array, int size) {
  int max = array[0];
  for (int i = 1; i < size; i++) {
    if (array[i] > max) {
      max = array[i];
    }
  }
  return max;
}

// get the index of the first element in 'number' with a 0. Return -1 if none found
int getNextZero(int *array, int size) {
  for(int i = 0; i < size; i++) {
    if (array[i] == 0) {
      return i;
    }
  }

  return -1; // error case
}
