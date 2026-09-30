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

typedef struct {
  Pipe pipe;
  char** argv;
} Subcommand;

static bool executeCommand(Subcommand* subcmds, size_t subcmdCount);

static const size_t CMD_BUF_BASE_SZ     = 256;
static const size_t ARGS_BUF_BASE_SZ    = 16;
static const size_t SUBCMDS_BUF_BASE_SZ = 8;
static const size_t REALLOC_MULT = 2;
static const char* const PROGRAM_NAME = "minish";
static const SizedString PROMPT   = STATIC_SIZED_STRING("$ ");
static const SizedString EXIT_CMD = STATIC_SIZED_STRING("exit");

static bool analyzeAndTrimCommand(char* cmd, size_t* argc, size_t* subcmdCount);
static void populateSubcommands(char** argv, size_t argc, 
                                Subcommand* subcmds, size_t subcmdCount,
                                char* cmd);
static bool doSmth(Subcommand* start, Subcommand* end);
static bool doSmthRec(Subcommand* subcommand, 
                      Subcommand* start, Subcommand* end);

static void execute(char** argv);

typedef int PID;
static PID CHILD_PID = -1;

static void setupSignalHandlers(PID childPid, 
                                struct sigaction* newAct,
                                struct sigaction* oldAct); 
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
  bool isCmdInited     = false,
       isArgvInited    = false,
       isSubcmdsInited = false;

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

  size_t subcmdsCap = SUBCMDS_BUF_BASE_SZ;
  Subcommand* subcmds = (Subcommand*)calloc(subcmdsCap, sizeof(Subcommand*));
  if (!subcmds) {
    fprintf(stderr, "No memory for subcmds buffer! Exiting...\n");
    RETURN(true);
  }
  isSubcmdsInited = true;

  size_t subcmdCount = 1;
  do {
    size_t argc = 0;
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
    } while (analyzeAndTrimCommand(cmd, &argc, &subcmdCount));

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

    if (subcmdCount > subcmdsCap) {
      size_t newCap = subcmdCount * REALLOC_MULT;
      Subcommand* temp = (Subcommand*)realloc(subcmds, newCap * sizeof(Subcommand));
      if (!temp) {
        fprintf(stderr, "No memory for subcmd buffer! Exiting...\n");
        RETURN(true);
      }

      subcmds = temp;
      subcmdsCap = newCap;
    }

    populateSubcommands(argv, argc, subcmds, subcmdCount, cmd);
  } while (executeCommand(subcmds, subcmdCount) == 0);

// a cleanup label, because i was sick of writing frees everywhere
exit:
  if (isCmdInited)
    free(cmd);
  if (isArgvInited)
    free(argv);
  if (isSubcmdsInited)
    free(subcmds);
  return ret;
}

#undef RETURN

bool executeSingletonSubcommand(char** argv) {
  if (!argv || !argv[0])
    return true;

  if (strncmp(argv[0], EXIT_CMD.str, EXIT_CMD.size) == 0)
    return true;

  PID forkPid = fork();
  if (forkPid == 0) {
    // child 
    execute(argv);
    exit(0);
  }

  // parent

  // setup custom signal handling
  struct sigaction act = {
    .sa_flags = SA_RESETHAND | SA_RESTART,
    .sa_handler = &killChild,
  };
  struct sigaction oldAct = {0};
  setupSignalHandlers(forkPid, &act, &oldAct);

  // wait for child
  checkError(wait(NULL));
  // restore old sigaction
  setupSignalHandlers(-1, &oldAct, NULL);

  return false;
}

static bool executeCommand(Subcommand* subcmds, size_t subcmdCount) {
  if (!subcmds         || 
      !subcmdCount     ||
      !subcmds[0].argv ||
      !subcmds[0].argv[0])
    return true;

  if (subcmdCount == 1) 
    return executeSingletonSubcommand(subcmds[0].argv);

  if (doSmth(subcmds, subcmds + subcmdCount - 1))
    return true;
  
  return false;
}

inline static bool doSmth(Subcommand* start, Subcommand* end) {
  PID forkPid = fork();
  if (forkPid == 0) {
    // child 
    doSmthRec(end - 1, start, end - 1);
    execute(end->argv);
    exit(0);
  }

  // parent

  // setup custom signal handling
  struct sigaction act = {
    .sa_flags = SA_RESETHAND | SA_RESTART,
    .sa_handler = &killChild,
  };
  struct sigaction oldAct = {0};
  setupSignalHandlers(forkPid, &act, &oldAct);

  // wait for child
  checkError(wait(NULL));
  // restore old sigaction
  setupSignalHandlers(-1, &oldAct, NULL);

  return false;
}

static bool doSmthRec(Subcommand* subcommand, 
                      Subcommand* start, Subcommand* end) {
  if (!subcommand          ||
      !subcommand->argv    ||
      !subcommand->argv[0] ||
      !start || !end)
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

    if (subcommand != start)
      if (doSmthRec(subcommand - 1, start, end))
        exit(1);

    execute(subcommand->argv);
    exit(0);
  }

  // parent

  // setup signal handling
  struct sigaction act = {
    .sa_flags = SA_RESETHAND | SA_RESTART,
    .sa_handler = &killChild,
  };
  struct sigaction oldAct = {0};
  setupSignalHandlers(forkPid, &act, &oldAct);

  // setup fds
  checkError(close(fromChild.sink));
  checkError(dup2(fromChild.source, STDIN_FD));
  checkError(close(fromChild.source));

  checkError(wait(NULL));
  // restore old sigaction
  setupSignalHandlers(-1, &oldAct, NULL);

  return false;
}


static bool analyzeAndTrimCommand(char* cmd, size_t* argc, size_t* subcmdCount) {
  if (!cmd || !argc || !subcmdCount)
    return 0;

  *argc  = 0;
  *subcmdCount = 1;

  size_t lastPipe = 0;
  bool isWhitespace = true;
  for (; *cmd; cmd++) {
    if (isspace(*cmd)) {
      if (!isWhitespace)
        *cmd = '\0';
      isWhitespace = true;
      continue;
    }

    if (*cmd == '|') {
      if (lastPipe == *argc) {
        if (lastPipe == 0)
          fprintf(stderr,
                  "Ill-formed command, leading pipe\n");
        else
          fprintf(stderr,
                  "Ill-formed command, pipes are next to each other\n");

        return true;
      }

      (*argc)++;
      (*subcmdCount)++;
      lastPipe = *argc;
      isWhitespace = true;
      continue;
    }

    if (isWhitespace) {
      (*argc)++;
      isWhitespace = false;
    }
  }

  if (lastPipe == *argc) {
    fprintf(stderr, 
            "Ill-formed command, trailing pipe\n");
    return true;
  }

  return false;
}

static void populateSubcommands(char** argv, size_t argc, 
                                Subcommand* subcmds, size_t subcmdCount,
                                char* cmd) {
  if (!argv || !argc ||
      !subcmds || !subcmdCount ||
      !cmd)
    return;

  subcmds[0].argv = argv;
  argv[0] = cmd;
  for (size_t i = 1, curSubcmd = 1; i < argc; i++) {
    for (; *cmd && *cmd != '|'; cmd++);

    #define handlePipeSymbol()               \
       argv[i] = NULL;                       \
       i++;                                  \
       subcmds[curSubcmd++].argv = argv + i; \
       *cmd = '\0';  

    if (*cmd == '|') {
      handlePipeSymbol();
    } else if (*(cmd + 1) == '\0') {
      break;
    }
    cmd++; // we can safely step one char ahead in all cases

    // skip extra spaces/handle pipes 
    // (example: "ab\0     c\0\0");
    // we are here  ^
    for (; isspace(*cmd) || *cmd == '|'; cmd++) {
      if (*cmd == '|') {
        handlePipeSymbol();
      }
    }

    #undef handlePipeSymbol

    argv[i] = cmd;
  }
  argv[argc] = NULL; 

  return;
}

static void execute(char** argv) {
  if (!argv || !argv[0])
    return;

  if (execvp(argv[0], argv) < 0) {
      if (errno == ENOENT)
        fprintf(stderr, 
                "%s: %s: command not found\n",
                PROGRAM_NAME, argv[0]);
      else
        printErr("execvp failed");
  }
}

static void setupSignalHandlers(PID childPid, 
                                struct sigaction* newAct,
                                struct sigaction* oldAct) {
  if (!newAct)
    return;

  CHILD_PID = childPid;
  checkError(sigaction(SIGINT, newAct, oldAct));
}

static void killChild(_unused int sig){ 
  if (CHILD_PID < 0)
    return;

  write(STDOUT_FD, "\n", 1);
  kill(CHILD_PID, SIGTERM);
  CHILD_PID = -1;
  return;
}
