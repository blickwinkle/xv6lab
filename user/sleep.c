#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"



int
main(int argc, char *argv[])
{

  if(argc != 2){
    fprintf(2, "sleep: Num of Args error, should be 2!\n");
    exit(1);
  }

  int sleepTime = atoi(argv[1]);
  
  exit(sleep(sleepTime));
}
