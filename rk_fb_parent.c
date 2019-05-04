#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>

#define NUM_CHILDREN 10000

volatile sig_atomic_t stopChildren;
void handleChildSignal(int signal) {
	stopChildren = 1;
}

int main() {
	signal(SIGUSR1, handleChildSignal);
	pid_t* child_pids = (pid_t*) calloc(NUM_CHILDREN, sizeof(pid_t));
	int is_parent = 0;
	stopChildren = 0;

	int i;
	for(i = 0; i < NUM_CHILDREN; i++) {
		pid_t child_pid = fork();

		if (child_pid == -1) {
			// Shit went down
			if (errno == EAGAIN) {
				// Some other error
				printf("EAGAIN\n");
				break;
			}
			if (errno == ENOMEM) {
				// That's the money
				printf("ENOMEM\n");
				break;
			} 
		}

		if (child_pid != 0) {
			//printf("Child created with pid: %d\n", child_pid);
			child_pids[i] = child_pid;
			is_parent = 1;
		} else {
			if(i % 100 == 0) {
				printf("%d child processes created\n", i);
			}
			if(!stopChildren) {
				pause();
			}
			//printf("Child exiting with pid: %d\n", getpid());
			break;
		}
	}

	if(is_parent) {
		int status;
		for(i = 0; i < NUM_CHILDREN; i++) {	
			//kill(child_pids[i], SIGUSR1);
			//kill(child_pids[i], SIGINT);
			waitpid(child_pids[i], &status, 0);
		}
		free(child_pids);
	}

	return 0;
}
