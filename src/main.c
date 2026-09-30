#include "exec.h"

// TODO: piping

int main(int argc, char* argv[]) {
  if (argc < 1)
    return 0;

  if (argc == 1)
    return repl(); // interactive mode
  
  if (argc > 1) { 
    executeSingletonSubcommand(argv + 1); // one-off mode, no pipes
    return 0;
  }

  return 0;
}
