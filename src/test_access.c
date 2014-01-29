
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>

int main(int argc, char ** argv)
{
  char directories[2][20] = {"/tmp/swsdira/","/tmp/swsdirb/"};
  char files[5][4] = {"","r","rw","rwx","n"};
  char buf[250], test[20];
  int i, j, k, mode;

  for (i = 0; i< 2; i++)
  {
    for (j = 0; j<4; j++)
    {
      strcpy(buf, directories[i]);
      strcat(buf,files[j]);
      printf("%s\n",buf);

      for(k = 0; k< 4; k++)
      {
        switch(k)
        {
          case 0:
            mode = F_OK;
            strcpy(test,"exists");
            break;
          case 1:
            mode = R_OK;
            strcpy(test,"read");
            break;
          case 2:
            mode = W_OK;
            strcpy(test,"write");
            break;
          case 3:
            mode = X_OK;
            strcpy(test,"execute");
            break;
        }
        if(access(buf,mode) == 0)
        {
          printf("\t%s : OK\n",test);
        }
        else
        {
          printf("\t%s : %s\n",test,strerror(errno));
        }
      }
      printf("\n");
    }
  }
  
  exit(0);
}
