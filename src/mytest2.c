#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

int main(int argc, char ** argv)
{
  char buf[PATH_MAX + 1];
  const char * directory = "~/lbustama";
  realpath(directory,(char *)&buf);

  printf("%s\n",buf);
  exit(0);
}
