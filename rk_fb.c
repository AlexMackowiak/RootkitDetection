#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#define NUM_CHILDREN 10000

int main() {
	int i;
	for(i = 0; i < NUM_CHILDREN; i++) {
		pid_t child_pid = fork();

		if (child_pid == -1) {
			// Shit went down
			if (errno == EAGAIN) {
				// Some other error
				printf("EAGAIN\n");
			}
			if (errno == ENOMEM) {
				// That's the money
				printf("ENOMEM\n");
			} 
		}

		if (child_pid != 0) {
			// Parent
			int status;
			waitpid(child_pid, &status, 0);
			//printf("Parent of %d exiting\n", child_pid);
			break;
		} else {
			//printf("Child with %d created\n", getpid());
			if(i % 1000 == 0) {
				printf("%d child processes created\n", i);
			}
		}
	}

	return 0;
}
