#include "exec.h"
#include "echo.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

// Must be identical to int array[2];
typedef struct {
  FD source; // is read from
  FD sink;   // is written into
} Pipe;

static const char* const PROGRAM_NAME = "minish";

bool executeCommand(char* argv[]) {
  if (!argv)
    return true;

  Pipe p = {0};
  if (pipe((int*)&p)) {
    printErr("pipe failed");
    return true;
  }

  FD forkPid = fork();
  if (forkPid == 0) {
    // child
    checkError(close(p.source));
    checkError(dup2(p.sink, STDOUT_FD));
    checkError(close(p.sink));

    if (execvp(argv[0], argv) < 0) {
      if (errno == ENOENT)
        fprintf(stderr, 
                "%s: %s: command not found\n",
                PROGRAM_NAME, argv[0]);
      else
        printErr("execvp failed");
    }

    exit(0);
  }

  // parent
  checkError(close(p.sink));
  if (echoFile(p.source))
    printErr("echoFile failed");
  checkError(wait(NULL));

  return false;
}
