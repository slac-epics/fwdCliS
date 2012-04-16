/*
      **MEMBER**=SLCLIBS:xxxSHRLIB
      **CMS**=xxxUTIL
==============================================================================

  Abs:  Provide a general way to log forward server messages.

  Name: FWD_ERR.C
        fwd_err - do the thing!

  Proto: fwd_server.h

  Auth: 23-Jun-1997, Ron MacKenzie (ronm)
  Rev:  DD-MMM-YYYY, Reviewer's Name (.NE. Author's Name)

------------------------------------------------------------------------------

  Mod:  (newest to oldest)
          
        MODIFIED PROXY FORWARD SERVER BY RONM TO BE CMLOG FOWAARD PROGRAM.

        16-Feb-1999, Mark Crane (MARK)
           clean up a bit, add gethostname to log correctly
        12-Nov-1998, Mark Crane (MARK):
           change to Unix
        17-May-1998, Mark Crane (MARK)
           Added code to determine what proxy this is and keep a static copy
           of the name as the source for the error message.  Also limited the
           size of the error message text to 52 chars and changed malloc to 
           calloc to zero the buffer first.

============================================================================*/

static char *version_info = "fwd_err.c    .01 06/23/97";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <sys/ipc.h>   /* for SYS V Interprocess Communications */
#include <sys/shm.h>   /* for the shared memory protos and types */
#include <sys/un.h>    /* AF_UNIX socket descriptor stuff */
#include <unistd.h>    /* for the getpid and gethostname calls */

#include "fwd_server.h"

#define MAX_HOSTNAME_LEN 128  /* Hostname length */
#define MAX_ERR_STRING 132   /* max number of chars to be sent to err_int */

/*
** The world standard SLC SMS msg header common to VAX and micro.
*/
typedef struct
{
   char           source[4];
   char           dest[4];
   unsigned long  timestamp[2];
   unsigned short func;
   unsigned short datalen;
 } msgheader_ts;

/*
 ************************************************
 */
int fwd_err(char* err_cp)
{
  int             i;
  int             pid;              /* our pid to register the shm_alloc */
  int             status;
  int             first_time = 0;   /* only do this code once */
  int             err_c_len;        /* provide length bounds for err string */
  int             bytes_written;    /* local socket info counter */
  char            dest_c[4] = {'V','0','1','7'};
  char           *msg_ptr;
  char            temp_name_c[MAX_HOSTNAME_LEN];
                                    /* BIG place to get hostname */
  static char     Src_c[4];         /* keep the source name until death */
  unsigned long   out_len;          /* place to store output length */
  unsigned long   lookup_flag;      /* tell routine what value to use */
  size_t          total_size;       /* total size of the buffer to send */ 
  size_t          fwd_size;         /* total size of the forward payload */ 
  size_t          sms_size;         /* total size of the SMS payload */ 
  pipe_ts         pipe_s;           /* structure to send to the pipe */
  ip_port_tu      lookup_key_u;     /* pass hash value to cache_lookup */
  fwd_hdr_ts     *fwd_hdr_ps;       /* pointer to part of messages buff */
  fwd_cache_ts   *cache_ps;         /* data about the child */
  msgheader_ts   *msgheader_ps;     /* pointer to part of messages buff */
  struct sockaddr_un dest_name;     /* place to make UNIX socket address */


  /* FOR CMLOG FORWARDER, JUST DO PRINTF FOR NOW.  SOME DAY, LOG TO
     SYSLOG LIKE MARK SAID */

/*
 * Get the host name for this machine for logging the messages, but only
 * do it once each child to save time.
 */
if (first_time == 0)
{
    first_time++;
    if (gethostname(temp_name_c, MAX_HOSTNAME_LEN) == ERROR)
    {
        if (LOG_ERROR)
        {
            fprintf(stderr,"ERR: gethostname error! %s\n", temp_name_c);
            perror ("ERR:");
        }
    }
    else
        memcpy (Src_c, temp_name_c, 4);  /* copy only 4 chars! */

    /*
     * Convert to uppercase
     */
    for (i = 0; i < sizeof(Src_c); i++)
        Src_c[i] = (char)(toupper ((int)Src_c[i]));
}

/*
 * Prevent possible err_int string overrun
 */
    err_c_len = strlen (err_cp) + 1;
    if (err_c_len > MAX_ERR_STRING)
    {
        err_c_len = MAX_ERR_STRING;
        *(err_cp + err_c_len) = '\0';
    }

    total_size = err_c_len + 4 + sizeof (msgheader_ts) + sizeof (fwd_hdr_ts);

    pid = getpid();  /* SOMEDAY log this along with message like mark did */


    fprintf(stderr,"ERR: Error occured in cmlog forward program \n");
    fprintf(stderr,"Error is: %s \n",err_cp);

    return (0);
}





