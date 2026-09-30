#include "exec.h"

// TODO: EOF sending aint working properly
// TODO: cat with no args doesnt work correctly
// TODO: input to a child process is buggy
// TODO: piping

int main(int argc, char* argv[]) {
  if (argc < 1)
    return 0;

  return (argc > 1)
         ? executeCommand(argv + 1)
         : repl();
}
