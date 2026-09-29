#include "exec.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
  if (argc < 1)
    return 0;

  if (argc > 1) {
    executeCommand(argv + 1);
    return 0;
  } else {
    printf("No command were provided!\n");
  }

  return 0;
}
