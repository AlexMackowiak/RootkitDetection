#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>

#define NUM_CHILDREN_PER_CYCLE 100
#define NUM_CYCLES 100
#define NUM_CHILDREN (NUM_CHILDREN_PER_CYCLE * NUM_CYCLES)

static int* numPausedInCycle;

int main() {
	numPausedInCycle = mmap(NULL, sizeof(*numPausedInCycle), PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	//pid_t* child_pids = (pid_t*) calloc(NUM_CHILDREN, sizeof(pid_t));
	
	int i;
	for(i = 0; i < NUM_CYCLES; i++) {
		int j;
		for(j = 0; j < NUM_CHILDREN_PER_CYCLE; j++) {
			//int childIndex = (i * NUM_CHILDREN_PER_CYCLE + j);
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
				//child_pids[childIndex] = child_pid;
			} else {
				//if(j == (NUM_CHILDREN_PER_CYCLE - 1)) {
					// Last child in this cycle
					//printf("%d child processes created\n", childIndex);
				//}
				//printf("CHILD PAUSING\n");
				*numPausedInCycle += 1;
				pause();
				//printf("Child exiting with pid: %d\n", getpid());
				exit(0);
			}
		}

		while(*numPausedInCycle < NUM_CHILDREN_PER_CYCLE);
		*numPausedInCycle = 0;
		printf("%d child processes created and paused\n", (i + 1) * NUM_CHILDREN_PER_CYCLE);
		//printf("Next cycle\n");
	}

	/*int status;
	for(i = 0; i < NUM_CHILDREN; i++) {	
		waitpid(child_pids[i], &status, 0);
	}*/
	pause();
	munmap(numPausedInCycle, sizeof(*numPausedInCycle));
	//free(child_pids);

	return 0;
}
