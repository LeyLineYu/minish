#include "exec.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

static const size_t CMD_BUF_BASE_SZ = 256;

int main(int argc, char* argv[]) {
  if (argc < 1)
    return 0;

  if (argc > 1) {
    bool failed = executeCommand(argv + 1);
    return failed ? 1 : 0;
  } else {
    size_t cmdCap = CMD_BUF_BASE_SZ;
    char* cmd = calloc(cmdCap, sizeof(char));
    if (!cmd) {
      fprintf(stderr, "No memory for cmd_buf! Exiting...\n");
      return 1;
    }

    char* args[2] = {cmd, NULL};
    do {
      printf()
      ssize_t nBytes = getline(&cmd, &cmdCap, stdin);
      if (nBytes < 0) {
        printErr("getline failed");
        free(cmd);
        return 1;
      }
      cmd[(size_t)nBytes - 1] = '\0';
    } while (executeCommand(args) == 0);

    free(cmd);
  }

  return 0;
}
