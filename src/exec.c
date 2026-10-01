#include "exec.h"
#include "utils.h"

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

// This is now redudant but I'm used to it now,
// so I'm not planning on deleting this struct
typedef struct {
  char** argv;
} Subcommand;

static const size_t CMD_BUF_BASE_SZ     = 256;
static const size_t ARGS_BUF_BASE_SZ    = 16;
static const size_t SUBCMDS_BUF_BASE_SZ = 8;
static const size_t REALLOC_MULT = 2;
static const char* const PROGRAM_NAME = "minish";
static const char* const PROMPT   = "$ ";
static const SizedString EXIT_CMD = STATIC_SIZED_STRING("exit");

static bool executeCommand(Subcommand* subcmds, size_t subcmdCount);
static bool analyzeAndTrimCommand(char* cmd, size_t* argc, size_t* subcmdCount);
static void populateSubcommands(char** argv, size_t argc, 
                                Subcommand* subcmds, size_t subcmdCount,
                                char* cmd);
static bool executePipedSubcommands(Subcommand* start, Subcommand* end);
static bool executePipedSubcommandsRec(Subcommand* subcommand, 
                      Subcommand* start, Subcommand* end);
static void execute(char** argv);

typedef int PID;
static PID CHILD_PID = -1;
static void handleSignals(int sig);

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
  Subcommand* subcmds = (Subcommand*)calloc(subcmdsCap, sizeof(Subcommand));
  if (!subcmds) {
    fprintf(stderr, "No memory for subcmds buffer! Exiting...\n");
    RETURN(true);
  }
  isSubcmdsInited = true;

  size_t subcmdCount = 1;
  do {
    size_t argc = 0;
    do {
      fputs(PROMPT, stdout);
      fflush(stdout);

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
    .sa_handler = &handleSignals,
  };
  struct sigaction oldActSIGINT  = {0};
  struct sigaction oldActSIGCHLD = {0};
  CHILD_PID = forkPid;
  checkError(sigaction(SIGINT,  &act, &oldActSIGINT));
  checkError(sigaction(SIGCHLD, &act, &oldActSIGCHLD));

  // wait for child
  checkError(wait(NULL));
  // restore old sigactions
  CHILD_PID = -1;
  checkError(sigaction(SIGINT,  &oldActSIGINT, NULL));
  checkError(sigaction(SIGCHLD, &oldActSIGCHLD, NULL));

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

  if (executePipedSubcommands(subcmds, subcmds + subcmdCount - 1))
    return true;
  
  return false;
}

static bool executePipedSubcommands(Subcommand* start, Subcommand* end) {
  if (!start || !end)
    return true;

  // setup custom signal handling
  struct sigaction act = {
    .sa_flags = SA_RESETHAND | SA_RESTART,
    .sa_handler = &handleSignals,
  };
  struct sigaction oldActSIGINT  = {0};
  struct sigaction oldActSIGCHLD = {0};
  checkError(sigaction(SIGINT,  &act, &oldActSIGINT));
  checkError(sigaction(SIGCHLD, &act, &oldActSIGCHLD));

  PID forkPid = fork();
  if (forkPid == 0) {
    // child 
    CHILD_PID = -1;
    executePipedSubcommandsRec(end - 1, start, end - 1);
    execute(end->argv);
    exit(0);
  }

  // parent

  CHILD_PID = forkPid;
  // wait for child
  checkError(wait(NULL));
  // restore old sigactions
  CHILD_PID = -1;
  checkError(sigaction(SIGINT,  &oldActSIGINT, NULL));
  checkError(sigaction(SIGCHLD, &oldActSIGCHLD, NULL));

  return false;
}

static bool executePipedSubcommandsRec(Subcommand* subcommand, 
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
    CHILD_PID = -1;
    checkError(close(fromChild.source));
    checkError(dup2(fromChild.sink, STDOUT_FD));
    checkError(close(fromChild.sink));

    if (subcommand != start)
      if (executePipedSubcommandsRec(subcommand - 1, start, end))
        exit(1);

    execute(subcommand->argv);
    checkError(wait(NULL));
    exit(0);
  }

  // parent
  
  CHILD_PID = forkPid;

  checkError(close(fromChild.sink));
  checkError(dup2(fromChild.source, STDIN_FD));
  checkError(close(fromChild.source));

  return false;
}


static bool analyzeAndTrimCommand(char* cmd, size_t* argc, size_t* subcmdCount) {
  if (!cmd || !argc || !subcmdCount)
    return false;

  *argc  = 0;
  *subcmdCount = 1;

  if (*cmd == '\n')
    return true;

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
 
static void handleSignals(int sig){ 
  if (CHILD_PID < 0) {
    putc('\n', stdout);
    return;
  }

  if (sig == SIGINT)
    kill(CHILD_PID, SIGINT);

  CHILD_PID = -1;
}
