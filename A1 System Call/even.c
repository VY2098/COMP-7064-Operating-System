#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

void handle_sigint(int sig)
{
	printf("Yeah!\n");
	fflush(stdout);   // force printf to output immediately
}

void handle_sighup(int sig)
{
	printf("Ouch!\n");
	fflush(stdout);   // force printf to output immediately
}

int main(int argc, char *argv[])
{
	int n = atoi(argv[1]);          //initiate the integer n
	
	signal(SIGINT, handle_sigint);   // attach handler to SIGINT
	signal(SIGHUP, handle_sighup);   // attach handler to SIGHUP

	for (int i = 0; i < n; i++)     //for loop to print every even number
	{
		printf("%d\n", 2 * i);
		fflush(stdout);             // force printf to output immediately
		sleep(5);                   //sleep
	}

	return 0;
}