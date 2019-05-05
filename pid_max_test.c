#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dirent.h>
#include <ctype.h>

#define NUM_CHILDREN_PER_CYCLE 250
#define NUM_CYCLES 1
#define NUM_CHILDREN (NUM_CHILDREN_PER_CYCLE * NUM_CYCLES)

// Should be able to get a count of all non-hidden processes by reading /proc/[pid]
int getProcessCount() {
	DIR* procDirectory = opendir("/proc");

	int numProcesses = 0;
	struct dirent* currentDirEntry;
	while((currentDirEntry = readdir(procDirectory)) != NULL) {
		int isProcessID = 1;

		int i = 0;
		for(i = 0; currentDirEntry->d_name[i] != '\0'; i++) {
			if(!isdigit(currentDirEntry->d_name[i])) {
				isProcessID = 0;
				break;
			}
		}

		if(isProcessID) {
			numProcesses++;
		}
	}

	return numProcesses;
}

static int* numPausedInCycle;
void handleChildSignal(int signal) {
}

int main() {
	numPausedInCycle = mmap(NULL, sizeof(*numPausedInCycle), PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	pid_t* child_pids = (pid_t*) calloc(NUM_CHILDREN, sizeof(pid_t));
	signal(SIGUSR1, handleChildSignal);

	printf("Num processes: %d\n", getProcessCount());
	exit(0);

	int i;
	for(i = 0; i < NUM_CYCLES; i++) {
		int j;
		for(j = 0; j < NUM_CHILDREN_PER_CYCLE; j++) {
			int childIndex = (i * NUM_CHILDREN_PER_CYCLE + j);
			pid_t child_pid = fork();

			if (child_pid != 0) {
				//printf("Child created with pid: %d\n", child_pid);
				child_pids[childIndex] = child_pid;
			} else {
				*numPausedInCycle += 1;
				pause();
				printf("Child exiting normally with pid: %d\n", getpid());
				exit(0);
			}
		}

		while(*numPausedInCycle < NUM_CHILDREN_PER_CYCLE);
		*numPausedInCycle = 0;
		printf("%d child processes created and paused\n", (i + 1) * NUM_CHILDREN_PER_CYCLE);
	}

	// Set pid_max to be lower than the number of processes on the system
	//system();

	int status;
	for(i = 0; i < NUM_CHILDREN; i++) {
		kill(child_pids[i], SIGUSR1);
		waitpid(child_pids[i], &status, 0);
	}
	
	munmap(numPausedInCycle, sizeof(*numPausedInCycle));
	free(child_pids);
	return 0;
}
