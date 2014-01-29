/*
 * Returns a null terminated string containing a line of input read from a socket.
 * it supports lines terminated in CRLF "\r\n" as well as just LF '\n'
 * 
 * socket - socket to be read
 * buf - bufer that will be filled with the line of input;
 * buf_size - size of the buffer to be filled
 *
 * return value - size of the returned string or -1 on failure. The max return value
 *  will be buf_size-1 the '\0' character is not counted.
 */

int
sgetline(int socket, char * buf, size_t buf_size)
{
  char a;
  int chars_read, count = 0;

  /* just in case user is trying to be "funny" */
  if (buf_size == 0) {
	return count;
  }
  if (buf_size == 1){
    buff[count] = '\0';
	return count;
  } 
  
  /*
   * read socket character by character and add them to buf until new line
   * character is encountered or until the end of the buffer is reached
   */
  while((chars_read = read(socket,a,1)) > 0 ){
    if (a == '\n'){
	  break;
	}
	
    buf[count++] = a;
	
	if( count == buf_size-1) {
	  break;
	}
  }
  
  /* read returns -1 on failure */
  if (chars_read < 0){
    /* return value indicating failure user can check errno for details */
	return chars_read;
  }
  
  /* if first character read is '\n' */
  if (count == 0){
	buf[count] = '\0'; /* empty string */
	return count;
  }
  /*
   * Discard carriage return character if present and add '\0' character
   * to the end of the string
   */
  if (buf[count - 1] == '\r'){
	buf[--count] = '\0';
  } else {
	buf[count] = '\0';
  }
  return count;
}