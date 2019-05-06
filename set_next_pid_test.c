#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>

// Code heavily inspired by: 
// efiop-notes.blogspot.com/2014/06/how-to-set-pid-using-nslast-pid.html
int main(int argc, char *argv[]) {
    if (argc != 2)
     return 1;

    int last_pid_fd = open("/proc/sys/kernel/ns_last_pid", O_RDWR | O_CREAT, 0644);
    if (last_pid_fd < 0) {
        perror("Can't open ns_last_pid");
        return 1;
    }

    if (flock(last_pid_fd, LOCK_EX)) {
        close(last_pid_fd);
        printf("Can't lock ns_last_pid\n");
        return 1;
    }

    int target_pid = atoi(argv[1]);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", target_pid - 1);

    if (write(last_pid_fd, buf, strlen(buf)) != strlen(buf)) {
        printf("Error writing to buf\n");
        return 1;
    }
    if (flock(last_pid_fd, LOCK_UN)) {
        printf("Can't unlock");
    }
    close(last_pid_fd);

	int i;
	for(i = 0; i < 10; i++) {
		int child_pid = fork();
		if (child_pid == 0) {
			pause();
			exit(0);
		}
		printf("Child pid: %d\n", child_pid);
	}

	pause();
    return 0;
}
