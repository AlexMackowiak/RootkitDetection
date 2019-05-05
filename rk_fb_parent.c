#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dirent.h>
#include <ctype.h>

#define TARGET_TOTAL_PROCESSES 5000
#define NUM_CHILDREN_PER_CYCLE 100

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

// Creates a child process and immediately pauses it, incrementing numPausedInCycle
// Returns 0 if any error occurred during fork(), and 1 for success
int createAndPauseChild() {
	pid_t child_pid = fork();

	if (child_pid == -1) {
		if (errno == EAGAIN) {
			printf("EAGAIN encountered too early, very suspicious\n");
			return 0;
		}
		if (errno == ENOMEM) {
			// I have absolutely no idea how to even cause this
			printf("System ran out of memory before fork() could finish\n");
			return 0;
		}
	}

	if (child_pid == 0) {
		// This is the child process
		*numPausedInCycle += 1;
		pause();
		exit(0);
	}

	return 1;
}

int main() {
	// Create shared memory so child processes can report to parent
	numPausedInCycle = mmap(NULL, sizeof(*numPausedInCycle), PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	int currentNumProcesses = getProcessCount();
	int numProcessesToCreate = TARGET_TOTAL_PROCESSES - currentNumProcesses;

	// How many full cycles of process creation do we need to reach the target?
	int numFullCreationCycles = numProcessesToCreate / NUM_CHILDREN_PER_CYCLE;

	// How many processes are still not made after the last cycle?
	int numLeftoverNeeded = numProcessesToCreate % NUM_CHILDREN_PER_CYCLE;

	int i;
	int j;
	for (i = 0; i < numFullCreationCycles; i++) {
		for (j = 0; j < NUM_CHILDREN_PER_CYCLE; j++) {
			int childCreated = createAndPauseChild();
			if (!childCreated) {
				goto fail;
			}
		}

		while (*numPausedInCycle < NUM_CHILDREN_PER_CYCLE);
		*numPausedInCycle = 0;
		printf("%d child processes created and paused\n", (i + 1) * NUM_CHILDREN_PER_CYCLE);
	}

	// Handle making leftover processes
	if (numLeftoverNeeded != 0) {
		for (i = 0; i < numLeftoverNeeded; i++) {
			int childCreated = createAndPauseChild();
			if (!childCreated) {
				goto fail;
			}
		}
		while (*numPausedInCycle < numLeftoverNeeded);
		*numPausedInCycle = 0;
	}
	printf("Total number of processes should now be: %d\n", TARGET_TOTAL_PROCESSES);

fail:
	pause();
	munmap(numPausedInCycle, sizeof(*numPausedInCycle));
	return 0;
}
