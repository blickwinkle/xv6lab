#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int fatherToChild, childToFather;
  pipe(&fatherToChild);
  pipe(&childToFather);
  char t = 's';
  if (fork() == 0) {
    read(fatherToChild, &t, 1);
    printf("%d: received ping\n", getpid());
    write(childToFather, &t, 1);
  } else {
    write(fatherToChild, &t, 1);
    wait((int *)(0));
    read(childToFather, &t, 1);
    printf("%d: received pong\n", getpid());
  }
  exit(0);
}
