#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>

#define TARGET_TOTAL_PROCESSES 5000
#define NUM_CHILDREN_PER_CYCLE 100

// Returns the total number of non-hidden processes running, including this program
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
				printf("Error encountered, please SIGINT this program and try again\n");
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
				printf("Error encountered, please SIGINT this program and try again\n");
				goto fail;
			}
		}
		while (*numPausedInCycle < numLeftoverNeeded);
		*numPausedInCycle = 0;
	}

	// Verify we actually reached the target number of processes
	currentNumProcesses = getProcessCount();
	printf("Total number of processes should now be: %d\n", TARGET_TOTAL_PROCESSES);
	printf("Actual number of non-hidden processes: %d\n", currentNumProcesses);
	if (currentNumProcesses != TARGET_TOTAL_PROCESSES) {
		printf("Another process may have started in the middle of running this program\n");
		goto fail;
	}

	// Read the value of pid_max to restore it later
	FILE* maxPidFile = fopen("/proc/sys/kernel/pid_max", "r");
	if (!maxPidFile) {
		printf("Could not read pid_max value\n");
		goto fail;
	}
	char* maxPid = NULL;
	size_t len = 0;
	getline(&maxPid, &len, maxPidFile);

	// Modify the max process ID such that one more fork() call would hopefully reveal an issue
	// Note to self: It's a security risk to use system() with root privileges
	char lowerMaxPidCommand[40];
	//sprintf(lowerMaxPidCommand, "sysctl -w kernel.pid_max=%d", TARGET_TOTAL_PROCESSES + 1);
	sprintf(lowerMaxPidCommand, "sysctl -w kernel.pid_max=%d", TARGET_TOTAL_PROCESSES - 100);
	int commandResult = system(lowerMaxPidCommand);
	if (commandResult != 0) {
		printf("Problem encountered running \"%s\"\n", lowerMaxPidCommand);
		goto fail;
	}

	// If there are no hidden processes with a PID below TARGET_TOTAL_PROCESSES, 
	//  then in theory one last fork() call should succeed, otherwise it would need to be
	//  assigned a PID above pid_max and should error with EAGAIN
	pid_t child_pid = fork();

	if (child_pid == -1) {
		if (errno == EAGAIN) {
			printf("EAGAIN indicates a hidden process with high certainty\n");
		}
		if (errno == ENOMEM) {
			printf("System ran out of memory before fork() could finish, very strange\n");
		}
	} else {
		if (child_pid == 0) {
			// This is the child process
			pause();
			exit(0);
		}
		printf("No hidden processes detected with PID below %d\n", TARGET_TOTAL_PROCESSES);

		// Need to kill this last child so a process ID exists to reset kernel.pid_max
		kill(child_pid, SIGINT);
		int status;
		waitpid(child_pid, &status, 0);
	}

	// Restore the pid_max value from before
	char resetMaxPidCommand[40];
	sprintf(resetMaxPidCommand, "sysctl -w kernel.pid_max=%s", maxPid);
	commandResult = system(resetMaxPidCommand);
	if (commandResult != 0) {
		printf("Problem encountered running \"%s\"\n", resetMaxPidCommand);
		goto fail;
	}

fail:
	pause();
	munmap(numPausedInCycle, sizeof(*numPausedInCycle));
	return 0;
}
