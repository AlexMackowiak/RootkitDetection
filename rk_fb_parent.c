#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dirent.h>
#include <ctype.h>

#define NUM_CHILDREN_PER_CYCLE 50
#define NUM_CYCLES 180

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
	numPausedInCycle = mmap(NULL, sizeof(*numPausedInCycle), PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	
	int i;
	for(i = 0; i < NUM_CYCLES; i++) {
		int j;
		for(j = 0; j < NUM_CHILDREN_PER_CYCLE; j++) {
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

			if (child_pid == 0) {
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
