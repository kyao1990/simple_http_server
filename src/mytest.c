#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "http.h"

int main(int argc, char ** argv)
{

    /* argv[1] is the base directory */
/* argv[2] is the file or path to be served within the directory */
struct flags flag;
flags_init(&flag);
flag.dir = malloc(strlen(argv[1])+1);
strcpy((char *)flag.dir,argv[1]);

int http_status = 0;
    int status = 0;
/* printf("argv[1]: %s\nargv[2]: %s\n",argv[1],argv[2]); */
    status = checkuri(argv[2],&http_status,&flag);
/* status = checkuri(argv[2],NULL, &flag); */
printf("HTTP Return code: %d\n",http_status);
/* printf("%d\n",status); */
exit(0);
}
