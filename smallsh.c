/*****************************************************************************
 * Author: Jacob Karcz
 * Date: 02.18.2017
 * Course: [CS344-400: Operating Systems]   Assignment: [Program3: smallsh]
 * File: smallsh.c
 * Usage:
 * Description:
 ****************************************************************************/

/*headers
 **********************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h> //open files
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <string.h>


/*   constants
 **********************/
#define MAX_ARGS     512
#define CMD_LIN_MAX  2048
#define MAX_BG_PROCS 666


/* booleans!
 ------------*/
enum bool { true, false };
typedef enum bool bool;


/* globals (if needed)
 **********************/
int bgProcDisabled = 0; //for sigTstp (couldnt figure out how to pass an int by reference) 



/* PARENT PROCESS FUNCTIONS
 ***************************/
void prompt();
void execute(char* argv[]);
int parse(char* commandLine, int lineLength, char* argv[]);
char *replace_str(char *str, char *orig, char *rep, int start);
int translateCommand(char** args, int* numArgs);
int cleanArgs(char* argv[]);
int freeArgs(char* argv[]);
void killDaChildren();
void reapZombies(pid_t zombies[], int* numZombies);

/*CHILD PROCESS FUNCTIONS
 ************************/
void bgDisableSig(int signo);
void fgRedirects(char* argv[], int* numArgs);
void bgRedirects(char* argv[], int* numArgs);

/**********************************************************************************************************************
 |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|[ Main ]|/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 **********************************************************************************************************************/

int main() {

	/*  variables
	 *********************/
	pid_t spawnPid = -18;
	pid_t bgZombies[100];
	
	int zombieIndex = 0;
	int exitStat = -18;
	int exitShell = 1;
	int numArgs;
	int shouldForkOff; //set by translateCommand (not a bool)
	int fileDescriptor;
	int i, j;

	char* argv[MAX_ARGS]; 
	//char* commandLine = NULL; 	//dynamically allocated buffer
	char commandLine[CMD_LIN_MAX]; //buffer of CMD_LIN_MAX chars
		memset(commandLine, '\0', sizeof(commandLine));

	size_t lineLength;
	size_t lineCapacity = 0;

	sigset_t signals;

	struct sigaction sigInt;
	struct sigaction sigInt_O;
	struct sigaction sigTstp;
	struct sigaction sigTstp_O;

	//SIGTSTP -> disable/enable bg commands (parent ignores the &)
	sigTstp.sa_handler = bgDisableSig;
	sigaction(SIGTSTP, &sigTstp, &sigTstp_O); 

	//SIGINT
	sigInt.sa_handler = SIG_IGN; //parent ignores it
	sigaction(SIGINT, &sigInt, &sigInt_O);

	//initialize array of argumentss
	for (i = 0; i < MAX_ARGS; i++) {
		argv[i] = (char *)malloc(100);
	}
	
	do {
		/* Parent Process
		 *********************/
		if (spawnPid != 0) {
	
			/* prompt + user input
			 ----------------------*/
			prompt();
			lineLength = lineCapacity = 0;
			//lineLength = getline(&commandLine, &lineCapacity, stdin); // w dynamic buffer: char* commandLine
			fgets(commandLine, CMD_LIN_MAX, stdin);			    // w buffer of spec size
			if (commandLine[strlen(commandLine) - 1] == '\n' ) {
				commandLine[strlen(commandLine) - 1] = '\0';
			}
				//testing |
				//printf("line length: %d || lineCap: %d\n%s", lineLength, lineCapacity, commandLine);
			
			/* check for basic built-in main-shell commands first
			 -----------------------------------------------------*/
			//exit
			if (strcmp(commandLine, "exit") == 0)  {
			exitShell = 0; //cleanUp after loop
			}
			//status
			if (strncmp(commandLine, "status", 6) == 0)  {
				if (WIFEXITED(exitStat)) { //exitStat
					printf("exit value %d\n", WEXITSTATUS(exitStat));
					fflush(stdout);
				}
				else if (WIFSIGNALED(exitStat)) { //sigStat
					printf("terminated by signal %d\n", WTERMSIG(exitStat));
					fflush(stdout);
				}
			} 

			/*Parse command line for other arguments
			 -----------------------------------------*/
			numArgs = 0;
			numArgs = parse(commandLine, (int)lineLength, argv); //turn buffer into array of args
			//free(commandLine);  // <-- if using dynamic buffer
			memset(commandLine, '\0', sizeof(commandLine)); // <--- if using static buffer

				//testing stuff
				if (argv[0] != NULL) { //ignore empty command line
					/*testing |
					-----------*
					printf("printing argv\n");
					printf("command: %s\n", argv[0]);
					printf("last arg: %s\n", argv[numArgs-1]);
					printf("numArgs == %d\n", numArgs);
					*------------------------------------------*/
				}
			
			/*translate the command(s)
			 ------------------------------*/
			shouldForkOff = 0;
			if (numArgs > 0) {
				shouldForkOff = translateCommand(argv, &numArgs); 
			}	// [0,1,2 == no action] [3 == fg] [4 == bg] [-18 == err]

			/*spawn a fg or bg process
			 ---------------------------*/
			if(shouldForkOff == 3 || shouldForkOff == 4) {
				spawnPid = fork();
				//bg process
				if (shouldForkOff == 4) { 
					if (spawnPid == 0) { //child
						bgRedirects(argv, &numArgs);}
					if (spawnPid != 0) { //parent
						bgZombies[zombieIndex++] = spawnPid; 
						printf("background pid is %d\n", spawnPid);
						fflush(stdout); }
				}
				//fg process		
				if (shouldForkOff == 3) {
					if (spawnPid == 0) { //CHILD: do the redirection bit
						fgRedirects(argv, &numArgs); 
					}
					if(spawnPid !=0) { //PARENT: wait for child to finish (signal if term'd)
						spawnPid = waitpid(spawnPid, &exitStat, 0);
						if (WIFSIGNALED(exitStat)) { //print terminating signal, if any
							printf("terminated by signal %d\n", WTERMSIG(exitStat));
							fflush(stdout);
						}
					}
				}
			}

			/*Parental DirtyWork
			 --------------------*/
			if(spawnPid !=0 ) {
				//reap zombies
				reapZombies(bgZombies, &zombieIndex);
				//reset args array
				cleanArgs(argv);
			}
		}

		/* Child Process
		 ***************/
		else if (spawnPid == 0) {			
			/* set SIGINT for childProcs
			 -----------------------------*/
			sigInt.sa_handler = SIG_DFL; // exec will inherit default signal behavior
			sigaction(SIGINT, &sigInt, &sigInt_O); //SIGINT will terminate child processes

			/* execute function
			 -------------------*/
			execute(argv);
			//execvp(argv[0], argv);
			//perror("child process exec failure");
			//exit(1);
		}
		/*error spawn
		 *************/
		else if (spawnPid == -1) { //danger will robinson 
			printf("Spawned process error:spawnPID = %d", spawnPid);
		}				
		
	
	}while (exitShell == 1);

	/*clean up
	 ************/
	freeArgs(argv);
	killDaChildren();


	return(0);
}
/**********************************************************************************************************************
 |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|[ Function Definitions ]|/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 **********************************************************************************************************************/
/*********************************
 * Prompt the user
 ********************************/
void prompt() {
	fflush(stdin);
	printf(":");
	fflush(stdout);
}

/************************************************
 * function to parse a command into an arguement list
 ************************************************/
int parse(char* commandLine, int lineLength, char* argv[]) {
	char c;
	int index = 0;
	char* delim = " \n";

	char *token = strtok(commandLine, " \n");
	while(token != NULL) {
		//argv[index] = token;
		strcpy(argv[index], token);  // <--- malloc first & often, or seg faults!!!
		//find $$ in substring
		if (strstr(argv[index], "$$") != NULL) {
			int pid = getpid();
			char* pids = (char *)malloc(10);
			snprintf(pids, sizeof(pids), "%d", pid);
			argv[index] = replace_str(argv[index], "$$", pids, 0);
			free(pids);
		}
		token = strtok(NULL, " \n");
		index++;
	}
	//set last arg to NULL
	//				<–––––––––– think about adding free(argv[index]); here, before nulling it? memory....
	argv[index] = NULL;

		//error checking
		//---------------
		//printf("total arguements: %d\n", index);
		//int i;
		//for(i = 0; i < index; i++) {
			//printf("%s\t", argv[i]);
		//}
		//printf("\n");	

	return index;
}

/************************************************
 * function to translate an array of arguments
 * 0 == no args
 * 1 == chdir
 * 2 == comment || status || exit
 * 3 == fork() it in fg!
 * 4 == fork() it in bg!
 * -18 == danger Will Robinson!
 ************************************************/
int translateCommand(char* argv[],int* numArgs) {
	
	//chdir
	if(strcmp(argv[0], "cd") == 0) {
		//getcwd()?
		char* newDir = argv[1];
		if (newDir == NULL) {
			newDir = getenv("HOME");
		}
		int movDir = chdir(newDir);
		if (movDir == -1) {
			perror("chdir error");
			//exit(1);
		}
		//printf("chdir to %s\n", newDir);
		//fflush(stdout);
		return 1;
	}
	//comment || exit || status
//	else if(strcmp(argv[0], "#") == 0) { //if there is no " ", comapre args[0][0] (or 
	else if ((strncmp(argv[0], "#", 1) == 0) || (strcmp(argv[0], "exit") == 0))  {
		/*-----------testing-------------	
		int i = 0;
		//printf("printing comment:\n");
		while (args[i] != NULL) {
			printf("%s ", args[i]);
			fflush(stdout);
			i++;
		}
		printf("\n");
		fflush(stdout);
		-------------------------------*/
		return 2;
	}

	/* Status (I like it better in the main loop)
	 *********
	else if ((strcmp(argv[0], "status") == 0) && *numArgs == 1 )  { 
		int exitStat = -18;
		if (WIFEXITED(exitStat)) { //exitStat
			printf("exit value %d\n", WEXITSTATUS(exitStat));
			fflush(stdout);
		}
		else if (WIFSIGNALED(exitStat)) { //sigStat
			printf("terminated by signal %d\n", WTERMSIG(exitStat));
			fflush(stdout);
		}
		return 2;
	} */

	//fg proc
	else if ((strcmp(argv[0], "#") != 0) && (strcmp(argv[0], "cd") != 0) && (strcmp(argv[0], "status") != 0) 
			&& (strcmp(argv[*numArgs-1], "&") != 0)) {
		/*-----------testing-------------	
		printf("translation: fg child proc\n");
		int i = 0;
		while (argv[i] != NULL) {
			printf("[%s] ", argv[i]);
			fflush(stdout);
			i++;
		}
		printf("\n");
		fflush(stdout);
		-------------------------------*/

		return 3;
	}
	//bg proc
	else if((strncmp(argv[*numArgs-1], "&", 1) == 0) && (strcmp(argv[0], "exit") != 0) && (strcmp(argv[0], "status") != 0)) {  
		//get rid of & before it gets sent
		//argv[numArgs-1] = argv[numArgs]; //now NULL

		/*******
		free(argv[*numArgs-1]); // = NULL; //get rid of &
		argv[*numArgs-1] = (char *)malloc(100);
		argv[*numArgs-1] = NULL;
		printf("number of arguments was: %d\n", *numArgs);
		fflush(stdout);
		*numArgs -= 1; //just in case
		printf("number of arguments is now: %d\n", *numArgs);
		*******/

		//this approach will waste mem but it wont seg fault
		int j;
		for (j = (*numArgs-1); j < *numArgs; j++) {
			if (argv[j] != NULL) {
		//		free(argv[j]);  	// <––––––––––––––––––––––––––––––––––––––NEW FREE CODE (TEST IT)
				argv[j] = argv[j+1];
			}
		}
		*numArgs -= 1; // ([*numargs--;] ==> 0, wtf?)



		//printf("starting bg proc w last arg as [%s]\n", argv[*numArgs-1]);
		//fflush(stdout);

		if (bgProcDisabled == 1) { //if SIGTSTP enabled
			return 3; //run it in fg
		}
		else {
			return 4; //run it in bg
		}
	}
	//empty command line
	else if(argv[0] == NULL) {
		//no input
		printf("no args, translate returns 0\n");
		fflush(stdout);
		return 0;
	}
	//oops!
	//printf("error: unable to process request\n");
	fflush(stdout);
	return -18;
}


/*************************************
 * function to execute a new process
 **************************************/
void execute(char** argv) {
	execvp(*argv, argv);
	perror("child process exec failure");
	exit(1);
}


/*************************************
 * function to handle redirects in fg procs
 **************************************/
void fgRedirects(char* argv[], int* numArgs) {
	//variables
	int i, j;
	int inFD;
	int outFD;
	int redir;
	int NULLstdout = 1;
	int NULLstdin = 1;
	//char* inputFile;
        //char* outputFile;
	char inputFile[64];
        char outputFile[64];
	memset(inputFile, '\0', sizeof(inputFile));
	memset(outputFile, '\0', sizeof(outputFile));

	//redirects
	for (i = 0; i < *numArgs; i++) {
		if (strcmp(argv[i], "<") == 0) { //input
			//open the file or throw error
			strcpy(inputFile, argv[i+1]);
			//inputFile = argv[i+1];
			inFD = open(inputFile, O_RDONLY);
			if (inFD == -1) { //open file failed
				printf("process %d cannot open %s for input\n", getpid(), inputFile);
				fflush(stdout);
				exit(1); 
			}
			else { //file open, set up redirect & fix args
				redir = dup2(inFD, STDIN_FILENO);
				if (redir == -1) { //redirect failed
					perror("dup2");
					exit(1); 
				}

				close(inFD);
				//remove < and inputFile from argv array (and shift elements)
				for (j = i; j < *numArgs; j++) {
					if (argv[j] != NULL) {
			//			free(argv[j]);  // <––––––––––––––––––––––––––––––––––––––NEW FREE CODE (TEST IT)
						//FREE STUFF HERE!!!!!!!!!!! free(argv[j]);//free argv[j] = NULL;//init
						argv[j] = argv[j+2];
					}
				}
				i--;
				*numArgs -= 2;
				// subtract 1 from i here? don't wanna miss a < after inputFile!
			}
		}
		if (strcmp(argv[i], ">") == 0) { //output
			//open the file or throw error
			strcpy(outputFile, argv[i+1]);
			//outputFile = argv[i+1]; // Syscall param open(filename) points to unaddressable byte(s) CAUSES SEG FAULT
			outFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0765); 
			if (outFD == -1) { //open file failed
				printf("cannot open %s for output", outputFile);
				fflush(stdout);
				exit(1); 
			}
			else { 
				//set up redirect
				redir = dup2(outFD, STDOUT_FILENO);
				if (redir == -1) { //redirect failed
					perror("dup2");
					exit(1); 
				}
				close(outFD);
				//remove < and inputFile from argv array
				for (j = i; j < *numArgs; j++) {
					if (argv[j] != NULL) {
			//			free(argv[j]);  // <––––––––––––––––––––––––––––––––––––––NEW FREE CODE (TEST IT)
						//FREE STUFF HERE!!!!!!!!!!! free(argv[j]);//free argv[j] = NULL;//init
						argv[j] = argv[j+2];
					}
				}
				i--;
				*numArgs -= 2;
				// subtract 1 from i here? don't wanna miss a < after outputFile!
			}
		}
	}
}

/*************************************
 * function to handle redirects in bg procs
 **************************************/
void bgRedirects(char* argv[], int* numArgs) {
	//variables
	int i, j;
	int inFD;
	int outFD;
	int redir;
	int NULLstdout = 1;
	int NULLstdin = 1;
	char* inputFile;
        char* outputFile;

	//redirects
	for (i = 0; i < *numArgs; i++) {
		if (strcmp(argv[i], "<") == 0) { //input
			//open the file or throw error
			inputFile = argv[i+1];
			inFD = open(inputFile, O_RDONLY);
			if (inFD == -1) { //open file failed
				printf("cannot open %s for input", inputFile);
				fflush(stdout);
				exit(1); 
			}
			else { //file open, set up redirect & fix args
				redir = dup2(inFD, 0);
				if (redir == -1) { //redirect failed
					perror("dup2");
					exit(1); 
				}

				close(inFD);
				//remove < and inputFile from argv array
				for (j = i; j < *numArgs; j++) {
					if (argv[j] != NULL) {
						argv[j] = argv[j+2];
					}
				}
				NULLstdin = 0;
			}
		}
		else if (strcmp(argv[i], ">") == 0) { //output
			//open the file or throw error
			outputFile = argv[i+1]; 
			outFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0755); 
			//maybe need to set permissions.... chmod(outFD, 0755?);
			if (outFD == -1) { //open file failed
				printf("cannot open %s for output", outputFile);
				fflush(stdout);
				exit(1); 
			}
			else { 
				//set up redirect
				redir = dup2(outFD, 1);
				if (redir == -1) { //redirect failed
					perror("dup2");
					exit(1); 
				}

				close(outFD);
				//remove < and inputFile from argv array
				for (j = i; j < *numArgs; j++) {
					if (argv[j] != NULL) {
						argv[j] = argv[j+2];
					}
				}
				NULLstdout = 0;
			}
		}
		if (NULLstdout == 1) {
			//redirect stdout to /dev/null
			outputFile = "/dev/null"; 
			outFD = open(outputFile, O_WRONLY | O_CREAT, 0744);
			if (outFD == -1) { //open file failed
				printf("cannot open %s for output", outputFile);
				fflush(stdout);
				exit(1); 
			}
			else { 
				//setup redirect to devnull
				redir = dup2(outFD, STDOUT_FILENO);
				if (redir == -1) { //redirect failed
					perror("dup2");
					exit(1); 
				}
				close(outFD);
			}
		}
		if (NULLstdin == 1) {
			//redirect stdout to /dev/null
			inputFile = "/dev/null";
			inFD = open(inputFile, O_RDONLY);
			if (inFD == -1) {
				printf("cannot open %s for input", inputFile);
				fflush(stdout);
				exit(1); 
			}
			else {
				redir = dup2(inFD, STDIN_FILENO);
				if (redir == -1) { //redirect failed
					perror("dup2");
					exit(1); 
				}
				close(inFD);
			}
		}

	}
}

/********************************************************
 * SIGTSTP signal handler function to set bg procs on/off
 ********************************************************/
void bgDisableSig(int signo) { 
	if (bgProcDisabled == 0) {
		char* message = "Entering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 49);
		//fflush(stdout);
		bgProcDisabled = 1;
	}
	else if (bgProcDisabled == 1) {
		char* message = "Exiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 29);
		//fflush(stdout);
		bgProcDisabled = 0;
	}
	else {
		char* message = "Error in SIGTSTP\n";
		write(STDOUT_FILENO, message, 17);
	}
}


/************************************************
 * function to clean out the arrguments array
 ************************************************/
int cleanArgs(char* argv[]) {
	int i;
	for (i = 0; i < MAX_ARGS; i++) {
		free(argv[i]);
		argv[i] = (char *)malloc(100);
		//argv[i] = NULL;
	}
	return 0;
}

/************************************************
 * function to clean out the arrguments array
 ************************************************/
int freeArgs(char* argv[]) {
	int i;
	for (i = 0; i < MAX_ARGS; i++) {
		free(argv[i]);
	}
	return 0;
}

/****************************************************
 * function to reap bakground child zombie processes 
 ***************************************************/
void reapZombies(pid_t zombies[], int* numZombies) {
	int i = 0;
	int exitStat;
	int deadZombies = 0;
	pid_t zombieChild;
	for(i = 0; i < *numZombies; i++) {
		zombieChild = waitpid(zombies[i], &exitStat, WNOHANG);
		if (zombieChild > 0 ) {
			deadZombies++;
			printf("background pid %d is done: ", zombieChild);
			fflush(stdout);	
			if (WIFEXITED(exitStat)) { //exitStat
				printf("exit value %d\n", WEXITSTATUS(exitStat));
				fflush(stdout);
			}
			else if (WIFSIGNALED(exitStat)) { //sigStat
				printf("terminated by signal %d\n", WTERMSIG(exitStat));
				fflush(stdout);
			}
		}
	}
	*numZombies -= deadZombies;
}

/*************************************************************
 * function to kill off all active processes for a clean exit
 ************************************************************/
void killDaChildren() {
	int exitStat;
	pid_t child = waitpid(-1, &exitStat, WNOHANG);
	while (child != -1) {
		//kill(child, SIGTERM);
		kill(child, SIGKILL);
		child = waitpid(-1, &exitStat, WNOHANG);
	}
}


/*************************************************************
 * function to replace substring
 * Credit: Tudor (http://stackoverflow.com/users/808486/tudor)
 * From: http://stackoverflow.com/questions/8137244/best-way-to-replace-a-part-of-string-by-another-in-c
 * I spent way too long trying to make something work in my loop, this function is amazing
 *************************************************************/
char *replace_str(char *str, char *orig, char *rep, int start) {
  static char temp[128];
  static char buffer[128];
  char *p;

  strcpy(temp, str + start);

  if(!(p = strstr(temp, orig)))  // Is 'orig' even in 'temp'?
    return temp;

  strncpy(buffer, temp, p-temp); // Copy characters from 'temp' start to 'orig' str
  buffer[p-temp] = '\0';

  sprintf(buffer + (p - temp), "%s%s", rep, p + strlen(orig));
  sprintf(str + start, "%s", buffer);    

  return str;

 	 /**** my last failed attempts (in the loop) ***
	//turn $$ into pid
	if(strstr(argv[index], "$$") != NULL ) { //returns pointer to substr
		int pid = getpid();
		//argv[index][p-2] = '\0';
		int loc = strlen(argv[index]) - 2;
		//printf("arg is %s, the char at %d will be replaces with %d\n", argv[index], loc, pid);
		//fflush(stdout);
		char* arg = argv[index];
		arg[loc] = '\0';
		snprintf(argv[index], sizeof(argv[index]), "%s%d", arg, pid);
	}
	*****************************************/

}



/*****************************************************************************
 * NOTES:
 *
 * saw someone create bools in c with something like enum bool {true, false} <-- sweet bc enum's are indexed
 *  not that sweet bc c doesn't like enums
 *
 *-------------------------------------------------------------------------
 *   
 ****************************************************************************/

