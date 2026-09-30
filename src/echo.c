#include "echo.h"
#include <unistd.h>

#define BUF_SZ 1024
static const ssize_t READ_EOF = 0;

bool echoFileTo(FD src, FD dest) {
  if (src < 0 || dest < 0)
    return true;

  char buf[BUF_SZ] = {0};
  bool failed = false;
  while (true) {
    ssize_t readCount = read(src, &buf, BUF_SZ);
    if (readCount == READ_EOF)
      break;
    else if (readCount < 0) {  
      printErr("read failed");
      failed = true;
      break;
    }

    for (ssize_t remainingCount = readCount; remainingCount > 0; ) {
      ssize_t writtenCount = write(dest, buf, remainingCount);
      if (writtenCount < 0) {  
        printErr("write failed");
        failed = true;
        break;
      }
      remainingCount -= writtenCount;
    }
  }

  return failed;
}
