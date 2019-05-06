#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>

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
        printf("Error writing to buf\n");
        return 0;
    }
    if (flock(last_pid_fd, LOCK_UN)) {
        printf("Can't unlock");
    }
    close(last_pid_fd);
}

int main(int argc, char *argv[]) {
    if (argc != 2)
		return 1;

    int targetPid = atoi(argv[1]);
	int nextPidSuccess = setNextPid(targetPid);
	if (!nextPidSuccess) {
		printf("FAILURE\n");
		return 1;
	}

	int i;
	for (i = 0; i < 10; i++) {
		int childPid = fork();
		if (childPid == 0) {
			pause();
			exit(0);
		}
		printf("Child pid: %d\n", childPid);
	}

	pause();
    return 0;
}
