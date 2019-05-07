#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>

//#define TARGET_MAX_PID 1000
#define NUM_CHILDREN_PER_CYCLE 100

// Code heavily inspired by:
// efiop-notes.blogspot.com/2014/06/how-to-set-pid-using-nslast-pid.html
// Function sets the most likely next pid to be target_pid
// I'm not certain this is guaranteed to be deterministic though
// returns 1 on success, and 0 on failure at any point in the process
int setNextPid(int target_pid) {
    int last_pid_fd = open("/proc/sys/kernel/ns_last_pid", O_RDWR | O_CREAT, 0644);
    if (last_pid_fd < 0) {
        perror("Can't open ns_last_pid");
        return 0;
    }

    if (flock(last_pid_fd, LOCK_EX)) {
        close(last_pid_fd);
        printf("Can't lock ns_last_pid\n");
        return 0;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", target_pid - 1);

    if (write(last_pid_fd, buf, strlen(buf)) != strlen(buf)) {
        printf("Error writing from buf\n");
        return 0;
    }
    if (flock(last_pid_fd, LOCK_UN)) {
        printf("Can't unlock\n");
    }
    close(last_pid_fd);
}

int setMaxPid(int newMax) {
	// Open pid_max "file"
	int max_pid_fd = open("/proc/sys/kernel/pid_max", O_RDWR | O_CREAT, 0644);
	if (max_pid_fd < 0) {
		perror("Error opening pid_max");
		return -1;
	}

	// Lock the file, not sure if this is actually necessary here
	if (flock(max_pid_fd, LOCK_EX)) {
        close(max_pid_fd);
        printf("Can't lock pid_max\n");
        return -1;
    }

	// Store the previous value for later
	char prev_val[32];
	if (read(max_pid_fd, prev_val, 32) < 0) {
		printf("Error reading pid_max file\n");
		return -1;
	}

	// Write the new value and unlock the file
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", newMax);
    if (write(max_pid_fd, buf, strlen(buf)) != strlen(buf)) {
        printf("Error writing from buf\n");
        return -1;
    }
    if (flock(max_pid_fd, LOCK_UN)) {
        printf("Can't unlock\n");
		return -1;
    }

	close(max_pid_fd);
	printf("Max PID value is now: %d\n", newMax);
	return atoi(prev_val);
}

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

int main(int argc, char* argv[]) {
	if (argc != 2) {
		printf("Need one argument for the target max pid value\n");
		return 1;
	}
	int TARGET_MAX_PID = atoi(argv[1]);

	// By setting the next PID to 2, fork() calls will start from that pid and count up
	//  filling in gaps, even in pids reserved by the kernel (pid less than 300)
	setNextPid(2);

	// Create shared memory so child processes can report to parent
	numPausedInCycle = mmap(NULL, sizeof(*numPausedInCycle), PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	int currentNumProcesses = getProcessCount();
	int numProcessesToCreate = TARGET_MAX_PID - currentNumProcesses;

	// How many full cycles of process creation do we need to reach the target?
	int numFullCreationCycles = numProcessesToCreate / NUM_CHILDREN_PER_CYCLE;

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

	// Handle making leftover processes if there aren't enough left for a full cycle
	int numLeftovers = 0;
	while (getProcessCount() < TARGET_MAX_PID) {
		int childCreated = createAndPauseChild();
		if (!childCreated) {
			printf("Error encountered, please SIGINT this program and try again\n");
			goto fail;
		}

		while (*numPausedInCycle < 1);
		*numPausedInCycle = 0;
		numLeftovers++;
	}

	// Verify we actually reached the target number of processes
	currentNumProcesses = getProcessCount();
	printf("Total number of processes should now be: %d\n", TARGET_MAX_PID);
	printf("Actual number of non-hidden processes: %d\n", currentNumProcesses);
	if (currentNumProcesses != TARGET_MAX_PID) {
		printf("Another process may have started in the middle of running this program\n");
		goto fail;
	}

	// Modify the max process ID such that one more fork() call would hopefully reveal an issue
	// For reasons I cannot explain, modifying pid_max in C leads to strange behaviors
	// This is why the below code is commented out, and run_script is needed for this to work
	//int prev_max_pid = setMaxPid(TARGET_MAX_PID - 500);
	//if (prev_max_pid < 0) {
	//	printf("Problem encountered setting pid_max\n");
	//	goto fail;
	//}

	// One last verfication that no extra process spawned to mess things up
	int processCount = getProcessCount();
	printf("Process count: %d\n", processCount);
	if (processCount == TARGET_MAX_PID) {
		// If there are no hidden processes with a PID below TARGET_MAX_PID,
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
			printf("No hidden processes detected with PID below %d\n", TARGET_MAX_PID);

			// Might as well kill this last process just in case we need a free pid
			kill(child_pid, SIGINT);
			int status;
			waitpid(child_pid, &status, 0);
		}
	} else {
		printf("Process count is off (%d) before final fork(), please rerun\n", processCount);
	}

	// Restore the pid_max value from before
	int prev_max_pid = 32768;
	setMaxPid(prev_max_pid);

fail:
	pause();
	munmap(numPausedInCycle, sizeof(*numPausedInCycle));
	return 0;
}
