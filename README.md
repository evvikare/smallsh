# smallsh

smallsh is a C program that serves as a lightweight shell akin to bash.
This was an assignment completed for CS344 Operating Systems at OSU.

## Usage


### Compiling and Running
To compile, type the follow command at a Linux terminal:

	gcc -o smallsh smallsh.c
	
It may be necessary to give the file execution permissions:

    chmod +x smallsh

To run:
 
	./smallsh

The general syntax of a command line is:

    command [arg1 arg2 ...] [< input_file] [> output_file] [&]

To compare the implementation of smallsh against the provided grading script:

    chmod +X ./p3testscript
    p3testscript > smallsh_testresults 2>&1

### Key Features

smallsh was designed for extensibility and currently has three built-in commands:

* **cd** changes the working directory. Without an argument, the target is the directory specified in the HOME environment variable. This command supports both absolute and relative paths.

* **exit** closes smallsh. It takes no arguments and kills any other processes or jobs that smallsh has started before it terminates itself.

* **status** displays either the exit status or the terminating signal of the last foreground process.

smallsh supports comments at the beginning of a line only, using ```#```.

## Notes

smallsh assumes correct syntax in command invocation, as shown above.

Command lines have a maximum length of 2048 characters and 512 arguments. 

smallsh cannot be terminated using CTRL-C. Instead, smallsh's foreground process is terminated.

smallsh has likewise repurposed CTRL-Z as a toggle for allowing and disallowing background processes.

smallsh expands any instance of "$$" into the PID of the shell itself. It does not otherwise perform variable expansion.


