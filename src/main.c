#include "exec.h"

// TODO: piping

int main(int argc, char* argv[]) {
  if (argc < 1)
    return 0;

  return (argc > 1)
         ? executeCommand(argv + 1)
         : repl();
}
