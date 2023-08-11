/*********************************************************************
   Program  : miniShell                   Version    : 1.3
 --------------------------------------------------------------------
   skeleton code for linix/unix/minix command line interpreter
 --------------------------------------------------------------------
   File			: minishell.c
   Compiler/System	: gcc/linux

********************************************************************/

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define NV 20			    /* max number of command tokens */
#define NL 100			    /* input buffer size */
char            line[NL];	/* command input buffer */

int job_count = 0;          // count the child process

// store background process
typedef struct {
    pid_t pid;
    char command[NL];
    int number;     // background process sequence
    int running;    // running flag: 1 for running, 0 for completed
} Job;

Job jobs[NV];  // initialize an empty array

// save background process
void save_background_process(pid_t pid, char* cmd_line, int count)
{
    int i;

    // loop to find an empty place to store the background process
    for (i = 0; i < NV; i++)
    {
        if (jobs[i].pid == 0)
        {
            // store details
            jobs[i].pid = pid;
            strncpy(jobs[i].command, cmd_line, NL - 1);
            jobs[i].command[NL - 1] = '\0';
            jobs[i].number = count;
            jobs[i].running = 1;
            job_count++;  //increase the job count when a new job is saved
            break;
        }
    }
}

// chech if the background job is complete and if all background jobs are complete
void check_completed_jobs() 
{
    int status;
    int found;

    // check if the background job is done
    do
    {
        found = 0;  //reset flag
        for (int i = 0; i < NV; i++)
        {
            if (jobs[i].pid == 0)  //empty slot, no need to check
                continue;

            // background process is done
            if (waitpid(jobs[i].pid, &status, WNOHANG) > 0 && jobs[i].pid > 0)
            {
                fprintf(stdout, "[%d]+ Done\t\t\t%s\n", jobs[i].number, jobs[i].command);
                fflush(stdout);
                found = 1;  //set the flag indicating that a completed process was found.
                jobs[i].running = 0;
            }
        }
    } while (found);

    //check if all background jobs are done
    int all_done = 1;
    for (int i = 0; i < NV; i++)
    {
        if (jobs[i].running == 1)
        {
            all_done = 0;
            break;
        }
    }

    if (all_done)
    {
        job_count = 0;
        for (int i = 0; i < NV; i++)
        {
            jobs[i].pid = 0;
            jobs[i].command[0] = '\0';
            jobs[i].number = 0;
            jobs[i].running = 0;
        }
    }
}

/*
    shell prompt
 */
void prompt(void)
{   

    //fprintf(stdout, "\n msh> ");  no need
    fflush(stdout);
}

int main(int argk, char *argv[], char *envp[])
/* argk - number of arguments */
/* argv - argument vector from command line */
/* envp - environment pointer */
{   
    int            frkRtnVal;	/* value returned by fork sys call */
    int            wpid;		/* value returned by wait */
    char           *v[NV];	/* array of pointers to command line tokens */
    char           *sep = " \t\n";/* command line token separators    */
    int            i;		/* parse index */

    int background = 0;
    char original_line[NL];  //the original command

    /* prompt for and process one command line at a time  */
    while (1) 
    {			/* do Forever */
                
        prompt();
        fgets(line, NL, stdin);
        strcpy(original_line, line); // store the original command before strtok modifies it
        fflush(stdin);

        char* ampersand = strchr(original_line, '&');  // find the '&' character
        if (ampersand)
        {
            *ampersand = '\0';  // replace '&' with null character
            
            // remove spaces before '&'
            while (ampersand > original_line && *(ampersand - 1) == ' ')
            {
                ampersand--;
                *ampersand = '\0';
            }
        }

        if (feof(stdin)) 
        {
            //fprintf(stderr, "EOF pid %d feof %d ferror %d\n", getpid(),
	        //feof(stdin), ferror(stdin));
            exit(0);
        }
    
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\000')
            continue;			/* to prompt */

        v[0] = strtok(line, sep);
        for (i = 1; i < NV; i++) 
        {
            v[i] = strtok(NULL, sep);
            if (v[i] == NULL)
                break;

            if (strcmp(v[i], "&") == 0)
            {
                background = 1;
                v[i] = NULL;
                break;
            }
        }
        /* assert i is number of tokens + 1 */

        if (strcmp(v[0], "cd") == 0)   //cd command
        {
            if (v[1] == NULL)
                fprintf(stderr, "no argument\n");
            else
            {
                if (chdir(v[1]) != 0)
                {
                    perror("chdir");
                }
            }
            continue;
        }
       
        /* fork a child process to exec the command in v[0] */

        switch (frkRtnVal = fork()) 
        {
            case -1:			/* fork returns error to parent process */
            {
                perror("fork");
                break;
            }
            case 0:			/* code executed only by child process */
            {
                if (execvp(v[0], v) == -1)
                {
                    perror("execvp");
                    exit(EXIT_FAILURE);
                }	
            }
            default:			/* code executed only by parent process */
            { 
                if (background) // check if this process is in background mode
                {
                    printf("[%d] %d\n", job_count + 1, frkRtnVal);
                    save_background_process(frkRtnVal, original_line, job_count + 1);
                }
                else
                {
                    wpid = waitpid(frkRtnVal, NULL, 0); // wait current child process in fork
                    if (wpid == -1)
                        perror("wait");

                    check_completed_jobs();             // check background process and print
                }
                break;
            }
        }				/* switch */
        background = 0;  //reset background
    }				/* while */

    return 0;
}				/* main */
