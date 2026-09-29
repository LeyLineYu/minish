#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdbool.h>

static const ssize_t READ_EOF = 0;
#define BUF_SZ 1024
#define stringify(a) stringify_(a)
#define stringify_(a) #a
#define printErr(str) \
  perror(__FILE__ ":" stringify(__LINE__) " [ERROR]: " str)

typedef int FD;

// must be identical to int array[2];
typedef struct {
  FD source; // is read from
  FD sink;   // is written into
} Pipe;

static const FD STDIN_FD  = 0;
static const FD STDOUT_FD = 1;

bool echoFile(FD fd);

int main(int argc, char* argv[]) {
  if (argc < 1)
    return 0;

  Pipe p = {0};
  if (pipe((int*)&p)) {
    printErr("pipe failed");
    return 1;
  }

  FD forkPid = fork();
  if (forkPid == 0) {
    close(p.source);
    dup2(p.sink, STDOUT_FD);
    close(p.sink);
    execvp(argv[1], argv + 1);

    return 0;
  }

  close(p.sink);
  echoFile(p.source);
  FD waitPid = wait(NULL);
  if (waitPid < 0)
    printErr("wait failed");

  return 0;
}

bool echoFile(FD fd) {
  if (fd < 0)
    return false;

  char buf[BUF_SZ] = {0};
  bool failed = false;
  while (true) {
    ssize_t readCount = read(fd, &buf, BUF_SZ);
    if (readCount == READ_EOF)
      break;
    else if (readCount < 0) {  
      printErr("read failed");
      failed = true;
      break;
    }

    for (ssize_t remainingCount = readCount; remainingCount > 0; ) {
      ssize_t writtenCount = write(STDOUT_FD, buf, remainingCount);
      if (writtenCount < 0) {  
        printErr("write failed");
        failed = true;
        break;
      }
      remainingCount -= writtenCount;
    }
  }

  return !failed;
}
