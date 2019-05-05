#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dirent.h>
#include <ctype.h>

#define NUM_CHILDREN_PER_CYCLE 50
#define NUM_CYCLES 50

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

int main() {
	// Create shared memory so child processes can report to parent
	numPausedInCycle = mmap(NULL, sizeof(*numPausedInCycle), PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	int i;
	int j;
	for(i = 0; i < NUM_CYCLES; i++) {
		for(j = 0; j < NUM_CHILDREN_PER_CYCLE; j++) {
			pid_t child_pid = fork();

			if (child_pid == -1) {
				if (errno == EAGAIN) {
					printf("EAGAIN encountered too early, very suspicious\n");
					break;
				}
				if (errno == ENOMEM) {
					// I have absolutely no idea how to even cause this
					printf("System ran out of memory before fork() could finish\n");
					break;
				} 
			}

			if (child_pid == 0) {
				// This is the child process
				*numPausedInCycle += 1;
				pause();
				exit(0);
			}
		}

		while(*numPausedInCycle < NUM_CHILDREN_PER_CYCLE);
		*numPausedInCycle = 0;
		printf("%d child processes created and paused\n", (i + 1) * NUM_CHILDREN_PER_CYCLE);
	}

	pause();
	munmap(numPausedInCycle, sizeof(*numPausedInCycle));
	return 0;
}
