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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <getopt.h>
#include <limits.h> // to check for INT_MAX
// semaphore libs
#include <semaphore.h>
#include <sys/sem.h>

#include "config.h"
#include "license.h"

#define PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

// Function definitions
void docommand(char *cline);
int detachandremove(int shmid, void *shmaddr);
char * getTimeFormattedMessage(char *msg);

// Semaphore functions and helpers
int initelement(int semid, int semnum, int semvalue);
void setsembuf(struct sembuf *s, int num, int op, int flg);
void wait_sem(int semid, struct sembuf *sops, size_t nsops);
void signal_sem(int semid, struct sembuf *sops, size_t nsops);
int removesem(int semid);
int r_semop(int semid, struct sembuf *sops, int nsops);


// Variable definitions
int shmid;
int semid; // for semaphore
void *shmaddr;
struct sembuf semsignal[1];
struct sembuf semwait[1];

extern struct License *nlicenses;


static void myhandler(int signum) {
  if(signum == SIGINT) {
    // is ctrl-c interrupt
    perror("\nrunsim: Ctrl-C Interrupt Detected. Shutting down gracefully...\n");
  }
  else if(signum == SIGALRM) {
    // is timer interrupt
    perror("\nrunsim: Info: The time for this program has expired. Shutting down gracefully...\n");
  }
  else {
    perror("\nrunsim: Warning: Only Ctrl-C and Timer signal interrupts are being handled.\n");
    return; // ignore the interrupt, do not exit
  }

  // free memory and exit

  // Print time to logfile before exit
  char *msg = getTimeFormattedMessage(" - Termination");

  logmsg(msg);

  removesem(semid); // will succeed for first execution

  if(detachandremove(shmid, nlicenses) == -1) {
    perror("runsim: Error: Failure to detach and remove memory\n");

    kill(getpid(), SIGKILL); // SIGKILL, SIGTERM, SIGINT

    exit(1);
  }

  pid_t group_id = getpgrp();
  if(group_id < 0)
    perror("runsim: Info: group id not found\n");
  else
    killpg(group_id, signum);


  kill(getpid(), SIGKILL);
	exit(0);
  signal(SIGQUIT, SIG_IGN);
}

// to handle time interrupts
static int timerHandler(int s) {
  int errsave;
  errsave = errno;
  write(STDERR_FILENO, "The time limit was reached\n", 1);
  errno = errsave;
}

static int setupinterrupt(void) {
  struct sigaction act;
  act.sa_handler = myhandler;
  act.sa_flags = 0;
  return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL) || sigaction(SIGALRM, &act, NULL));
}

static int setupitimer(int sleepTime) {
  struct itimerval value;
  value.it_interval.tv_sec = 0;
  value.it_interval.tv_usec = 0;
  value.it_value.tv_sec = sleepTime; // alarm
  value.it_value.tv_usec = 0;
  return (setitimer(ITIMER_PROF, &value, NULL));
}

void wait_sem(int semid, struct sembuf *sops, size_t nsops) {
  while(r_semop(semid, sops, nsops) == -1) {
    perror("runsim: Error: Failed to enter critical section (semop wait)\n");
    exit(1);
  }
}

void signal_sem(int semid, struct sembuf *sops, size_t nsops) {
  while(r_semop(semid, sops, nsops) == -1) {
    perror("runsim: Error: Failed to exit critical section (semop signal)\n");
    exit(1);
  }
}


// Write a runsim program that runs up to n processes at a time. Start the runsim program by typing the following command
int main(int argc, char *argv[]) {
  int option;
  int sleepTime;
  int hasSleepTime = 0; // change to 1 for true
  int nlicensesInput;
  int error;

  // get option for "-t" time, if it exists
  while((option = getopt(argc, argv, ":t:")) != -1) {
    switch(option) {
      case 't':
        // check that sleepTime is integer
        if(!atoi(optarg)) {
          perror("runsim: Error: Sleep time must be an integer\n");
          return 1;
        }
        sleepTime = atoi(optarg);
        hasSleepTime = 1;
        break;
      default:
        perror("runsim: Error: Invalid option. Only -t is allowed.\n");
        return 1;
    }
  }


  if (argc != 2 && argc != 4) {
    printf("argc: %d\n", argc);
    fprintf(stderr, "runsim: Error: Usage: %s [-t sec] <number-of-licenses>\n", argv[0]);
    return -1;
  }
  if(argc == 2) {
    if(!atoi(argv[1])) {
      fprintf(stderr, "runsim: Error: Usage: %s [-t sec] <number-of-licenses>, where n is an integer\n", argv[0]);
      return -1;
    }
    nlicensesInput = atoi(argv[1]);
  }
  else if(argc == 4) {
    if(!atoi(argv[3])) {
      fprintf(stderr, "runsim: Error: Usage: %s [-t sec] <number-of-licenses>, where n is an integer\n", argv[0]);
      return -1;
    }
    nlicensesInput = atoi(argv[3]);
  }

  if(hasSleepTime == 1) {
    if(sleepTime < 0) {
      perror("runsim: Error: Usage: sleep time specified by -t must be greater than 0\n");
      return -1;
    }
    // check if sleepTime overflows integer
    if(sleepTime >= INT_MAX) {
      perror("runsim: Error: Usage: sleep time specified by -t is too large.\n");
      return -1;
    }
  }
  else {
    sleepTime = SLEEP_TIME; // assign to default (100s) if none provided
  }

  if(nlicensesInput < 0) {
    perror("Usage: ./runsim [-t sec] <number-of-licenses>, where n is an integer >= 0\n");
    return -1;
  }
  else if(nlicensesInput > MAX_LICENSES) {
    perror("runsim: Warning: Max Licenses at a time is 20\n");
    nlicensesInput = MAX_LICENSES;
  }

  // Set up timers and interrupt handler
  if (setupinterrupt() == -1) {
    perror("runsim: Error: Could not run setup the interrupt handler.\n");
    return -1;
  }
  if (setupitimer(sleepTime) == -1) {
    perror("runsim: Error: Could not setup the interval timer.\n");
    return -1;
  }

  signal(SIGINT, myhandler);

  // initialize timer
  alarm(sleepTime);

  printf("%d licenses specified\n", nlicensesInput);

  // allocate shared memory
  shmid = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
  printf("shmid: %d\n", shmid);
  if (shmid == -1) {
    perror("runsim: Error: Failed to create shared memory segment\n");
    return -1;
  }

  // attach shared memory
  nlicenses = (struct License *)shmat(shmid, NULL, 0);
  if (nlicenses == (void *) -1) {
    perror("runsim: Error: Failed to attach to shared memory\n");
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
      perror("runsim: Error: Failed to remove memory segment\n");
    return -1;
  }

  // Create semaphore containing a single element
  if((semid = semget(IPC_PRIVATE, 1, PERMS)) == -1) {
    perror("runsim: Error: Failed to create private semaphore\n");
    return 1;
  }

  setsembuf(semwait, 0, -1, 0); // decrement first element of semwait
  setsembuf(semsignal, 0, 1, 0); // increment first element of semsignal

  // initialize semaphore before use
  if(initelement(semid, 0, 1) == -1) {
    perror("runsim: Error: Failed to init semaphore element value to 1\n");
    if(removesem(semid) == -1)
      perror("runsim: Error: Failed to remove failed semaphore\n");
    return 1;
  }

  wait_sem(semid, semwait, 1);

  // critical section
  initlicense(nlicensesInput);

  signal_sem(semid, semsignal, 1);


  printf("\n");

  char cline[MAX_CANON];

  // Main Loop until EOF reached
  while (fgets(cline, MAX_CANON, stdin) != NULL) {
    printf("\n");

    // 1. Fork a child that calls docommand
    pid_t child_pid = fork();
    if (child_pid == -1) {
      perror("runsim: Error: Failed to fork a child process\n");
      if (detachandremove(shmid, nlicenses) == -1)
        perror("runsim: Error: Failed to detach and remove shared memory segment\n");
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
        perror("runsim: Error: Failed to wait for child\n");
        return -1;
      }
      else if(wpid == 0) {
        // child is still running
        // printf("Child is still running\n");
      }
      else {
        wait_sem(semid, semwait, 1);

        int result = returnlicense();

        signal_sem(semid, semsignal, 1);
      }
    }
  }

  // Wait for all children to finish, after the main loop is complete
  while(wait(NULL) > 0) {
    printf("Waiting for all children to finish...\n");
  }

  nlicenses = (struct License *)shmat(shmid, NULL, 0);
  if (nlicenses == (void *) -1) {
    perror("runsim: Error: Failed to attach to shared memory\n");
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
      perror("runsim: Error: Failed to remove memory segment\n");
    if(removesem(semid) == -1) {
      perror("runsim: Error: Failed to remove semaphore\n");
    }
    return -1;
  }

  // get the message to log before termination
  char *msg = getTimeFormattedMessage(" - Termination");

  wait_sem(semid, semwait, 1);

  logmsg(msg);

  signal_sem(semid, semsignal, 1);

  if(removesem(semid) == -1) {
    perror("runsim: Error: Failed to remove semaphore\n");
  }
  if(detachandremove(shmid, nlicenses) == -1) {
    perror("runsim: Error: Failed to detach and remove shared memory segment\n");
    return -1;
  }

  return 0;
}

void docommand(char *cline) {
  int error;
  int result;

  printf("received in docammand: %s\n", cline);

  // check if license available as well
  wait_sem(semid, semwait, 1);
  result = getlicense();
  signal_sem(semid, semsignal, 1);

  while(result == 1) {
    sleep(1);   
    wait_sem(semid, semwait, 1);
    result = getlicense();
    signal_sem(semid, semsignal, 1);
  }

  // Fork to grand-child
  pid_t grandchild_id = fork();

  printf("forked grandchild: %d\n", grandchild_id);

  if (grandchild_id == -1) {
    perror("runsim: Error: Failed to fork grand-child process\n");

    wait_sem(semid, semwait, 1);

    result = returnlicense();

    signal_sem(semid, semsignal, 1);

    exit(1);
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
    perror("runsim: Error: Failed to execl\n");
    exit(1);
  }
  else {
    // in parent
    int grandchild_status;

    waitpid(grandchild_id, &grandchild_status, 0);
    printf("Grand child finished, result: %d\n", WEXITSTATUS(grandchild_status));

    wait_sem(semid, semwait, 1);

    // start critical section
    returnlicense();

    signal_sem(semid, semsignal, 1);

    exit(0);
  }

  exit(0);
}

// Initialize the sempahore element
int initelement(int semid, int semnum, int semvalue) {
  union semun {
    int val;
    struct semids_ds *buf;
    unsigned short *array;
  } arg;

  arg.val = semvalue;

  return semctl(semid, semnum, SETVAL, arg);
}

// initializes the struct sembuf structure members sem_num, sem_op, and sem_flg
void setsembuf(struct sembuf *s, int num, int op, int flg) {
  s->sem_num = (short)num;
  s->sem_op = (short)op;
  s->sem_flg = (short)flg;
  return;
}

// restarts the semop if interrupt received
int r_semop(int semid, struct sembuf *sops, int nsops) {
  while(semop(semid, sops, nsops) == -1) {
    if(errno != EINTR)
      return -1;
  }
  return 0;
}


// removed the semaphore specified by semid
int removesem(int semid) {
  return semctl(semid, 0, IPC_RMID);;
}

// From textbook
int detachandremove(int shmid, void *shmaddr) {
  int error = 0;

  if (shmdt(shmaddr) == -1) {
    fprintf(stderr, "runsim: Error: Can't detach memory\n");
    error = errno;
  }
  
  if ((shmctl(shmid, IPC_RMID, NULL) == -1) && !error) {
    fprintf(stderr, "runsim: Error: Can't remove shared memory\n");
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
