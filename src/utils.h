#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

typedef int FD;

static const FD STDIN_FD  = 0;
static const FD STDOUT_FD = 1;

#define stringify(a) stringify_(a)
#define stringify_(a) #a
#define printErr(str) \
  perror(__FILE__ ":" stringify(__LINE__) " [ERROR]: " str)

// Call a function and 
// if its return value is negative, parse errno.
// Does not exit or return, so use this for soft errors
#define checkError(func) \
  if (func < 0)          \
    printErr(stringify(func) " failed")

typedef struct {
  char* str;
  size_t size;
} SizedString;

#define STATIC_SIZED_STRING(cstr) \
  (SizedString){ .str = cstr, .size = sizeof(cstr) - 1 }

#endif
