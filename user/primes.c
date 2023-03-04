#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int pipArray[36] = {0};

void handle(int i) {
    int p = -1;
    int readNum = -1;
    printf("pid %d\n");
    while (1) {
        if (read(pipArray[i], &readNum, sizeof(int)) != sizeof(int)) {
            close(pipArray[i + 1]);
            //printf("pid %d ret! \n", i);
            exit(0);
        }
        if (p == -1) {
            p = readNum;
            printf("prime %d\n", p);
            continue;
        }
        if (readNum % p == 0) continue ;
        write(pipArray[i + 1], &readNum, sizeof(int));
    }
}

int
main(int argc, char *argv[])
{
  for (int i = 0; i < 36; i++) {
    if (pipe(&pipArray[i]) != 0) {
        printf("%d false !\n", i);
        exit(0);
    }
  }

  for (int i = 0; i < 35; i++) {
    if (fork() == 0) {
        handle(i);
    }
  }
  for (int i = 2; i <= 35; i++) {
    write(pipArray[0], &i, sizeof(int));
  }
  close(pipArray[0]);
  for (int i = 0; i < 35; i++) {
    wait((int *)0);
  }
  exit(0);
}