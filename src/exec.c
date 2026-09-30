#include "exec.h"
#include "echo.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

// Must be identical to int array[2];
typedef struct {
  FD source; // is read from
  FD sink;   // is written into
} Pipe;

static const size_t CMD_BUF_BASE_SZ = 256;
static const size_t ARGS_BUF_BASE_SZ = 16;
static const size_t REALLOC_MULT = 2;
static const char* const PROGRAM_NAME = "minish";
static const SizedString PROMPT   = STATIC_SIZED_STRING("$ ");
static const SizedString EXIT_CMD = STATIC_SIZED_STRING("exit");

static size_t splitByWhitespace(char* str);
static void populateArgv(char** argv, size_t argc, char* cmd);

typedef int PID;
static PID CHILD_PID = 0;
static void killChild(_unused int sig);

#define RETURN(value) \
{                     \
  ret = value;        \
  goto exit;          \
}

// Read, Execute, Print, Loop
bool repl() {
  bool ret = false;
  // bools for resource management
  bool isCmdInited  = false,
       isArgvInited = false;

  size_t cmdCap = CMD_BUF_BASE_SZ;
  char* cmd = (char*)calloc(cmdCap, sizeof(char));
  if (!cmd) {
    fprintf(stderr, "No memory for cmd buffer! Exiting...\n");
    return true;
  }
  isCmdInited = true;

  size_t argsCap = ARGS_BUF_BASE_SZ;
  char** argv = (char**)calloc(argsCap, sizeof(char*));
  if (!argv) {
    fprintf(stderr, "No memory for argv buffer! Exiting...\n");
    RETURN(true);
  }
  isArgvInited = true;

  do {
    write(STDOUT_FD, PROMPT.str, PROMPT.size); 
    // write and not fprintf because echoFile uses FDs not FILEs
    // so it's either write or fprintf + fflush...
    ssize_t nBytes = getline(&cmd, &cmdCap, stdin);
    if (nBytes < 0) {
      if (feof(stdin))
        RETURN(false);

      printErr("getline failed");
      RETURN(true);
    }

    size_t argc = splitByWhitespace(cmd); 
    if (argc + 1 > argsCap) {
      size_t newCap = (argc + 1) * REALLOC_MULT;
      char** temp = (char**)realloc(argv, newCap * sizeof(char*));
      if (!temp) {
        fprintf(stderr, "No memory for args buffer! Exiting...\n");
        RETURN(true);
      }

      argv = temp;
      argsCap = newCap;
    }

    populateArgv(argv, argc, cmd);

  } while (executeCommand(argv) == 0);

// a cleanup label, because i was sick of writing free's everywhere
exit:
  if (isCmdInited)
    free(cmd);
  if (isArgvInited)
    free(argv);
  return ret;
}

#undef RETURN

bool executeCommand(char* argv[]) {
  if (!argv || !argv[0])
    return true;

  if (strncmp(argv[0], EXIT_CMD.str, EXIT_CMD.size) == 0)
    return true;

  Pipe fromChild = {0};
  if (pipe((int*)&fromChild)) {
    printErr("pipe fromChild failed");
    return true;
  }

  PID forkPid = fork();
  if (forkPid == 0) {
    // child
    checkError(close(fromChild.source));
    checkError(dup2(fromChild.sink, STDOUT_FD));
    checkError(close(fromChild.sink));

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

  // setup signal handling
  CHILD_PID = forkPid;
  struct sigaction act = {
    .sa_flags = SA_RESETHAND | SA_RESTART,
    .sa_handler = &killChild,
  };
  struct sigaction oldAct = {0};
  checkError(sigaction(SIGINT, &act, &oldAct));

  // setup fds
  checkError(close(fromChild.sink));
  FD parentStdinCopy = dup(STDIN_FD);
  if (parentStdinCopy < 0) {
   printErr("dup for the parent stdin failed, i'm tired...");
   return true;
  }
  close(STDIN_FD);

  // echo all child's output
  if (echoFile(fromChild.source))
    printErr("echoFile failed");

  checkError(wait(NULL));
  // restore old sigaction
  CHILD_PID = -1;
  checkError(sigaction(SIGINT, &oldAct, NULL));
  // close remaining fds, and restore parent stdin
  checkError(close(fromChild.source));
  checkError(dup2(parentStdinCopy, STDIN_FD));
  checkError(close(parentStdinCopy));

  return false;
}

static size_t splitByWhitespace(char* str) {
  if (!str)
    return 0;

  size_t argc = 0;
  bool isWhitespace = true;
  for (; *str; str++) {
    if (isspace(*str)) {
      if (!isWhitespace)
        *str = '\0';
      isWhitespace = true;
      continue;
    }

    if (isWhitespace) {
      argc++;
      isWhitespace = false;
    }
  }

  return argc;
}

static void populateArgv(char** argv, size_t argc, char* cmd) {
  if (!argv || !argc || !cmd)
    return;

  argv[0] = cmd;
  for (size_t i = 1; i < argc; i++) {
    cmd = strchr(cmd, '\0');
    cmd++; // we can safely step one char ahead in all cases
    if (*cmd == '\0')
      break;

    // skip spaces (example: "ab\0     c\0\0");
    //        we are here ~~~~~~~~^
    for (; isspace(*cmd); cmd++);

    argv[i] = cmd;
  }
  argv[argc] = NULL; 

  return;
}

static void killChild(_unused int sig){ 
  if (CHILD_PID < 0)
    return;

  write(STDOUT_FD, "\n", 1);
  kill(CHILD_PID, SIGTERM);
  CHILD_PID = -1;
  return;
}
