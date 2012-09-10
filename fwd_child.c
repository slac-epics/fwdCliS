/*
      **MEMBER**=SLCLIBS:xxxSHRLIB
      **CMS**=xxxUTIL
==============================================================================

  Abs:  The forked children of the fwd_server

  Name: fwd_child.c
          fwd_child        - FWD server TCP client task (one for each client)
          tcp_get          - wrapper for the TCP stream receive work
          fwd_child_death  - kill errant child
          fwd_chksum_make  - calculate and return a checksum of the header
          fwd_chksum_check - check the crc value of a header

  Proto: fwd_server.h

  Auth: 01-Oct-1997, Ron MacKenzie (ronm)
  Rev:  DD-MMM-YYYY, Reviewer's Name (.NE. Author's Name)

------------------------------------------------------------------------------
 
  Mod:  (newest to oldest)
       26-Oct-2007, Ron MacKenzie
          Small change to make gcc compiler happy for lcls linux.
          Add compile time option to log to logServer instead of cmlogServer.
                Enable with the define USE_LOGSERVER.
                THIS MEANS YOU CAN LOG TO CMLOG OR IOCLOGFWDSERVER BUT NOT
                BOTH AT ONCE.

          To log to iocLogServer, you can set these 2 environment variables:
              EPICS_IOC_LOG_PORT  (defaults to 6500)
              EPICS_IOC_LOG_INET  (inet means internet address I think, not node.)

       26-Jul-2007, Ron MacKenzie
          Left justify the host.  Host is now 16 chars, not 8.
       20-Jul-1999, Ron MacKenzie
          I took the proxy  forward server code and scaled it down
          to make this server.  

============================================================================*/
static char *version_info = "fwdCliS.c 1.0 07/30/99, 07/30/99";

#define USE_LOGSERVER  1   /* Log to epics iocLogServer instead of cmlogServer */

#include <sys/types.h>
#include <sys/time.h>   /* select timeout arg */
#include <sys/socket.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>     /* fo rthe fcntl call */
#include <unistd.h>    /* select call */
#include <sys/ipc.h>   /* for SYS V Interprocess Communications */
#include <sys/shm.h>   /* for the shared memory protos and types */
#include <sys/un.h>    /* AF_UNIX socket descriptor stuff */

#ifdef USE_LOGSERVER
#include "errlog.h"       /* for epics errlogPrintf logging to iocLogServer */
#include "logClient.h"
#include "envDefs.h"
#else
#include <cmlog.h>
#endif

#include "fwd_server.h" 

/* FOR SLC TESTING 9/27/11 */
#define LOG_DEBUG 1 
/*
#define LOG_DEBUG 0 
*/

/*
 * Since the GNU source for 2.0.36 doesn't include TCP_NODELAY I have to make
 * my own!  NOT GOOD!
 */
#define TCP_NODELAY     1

#define SELECT_TIMEOUT (28800)   /* in seconds 90,000 = 25 hours
				  * 28800 = 8 hours
				  * 3600 = one hour
                                  * this timeout determins how long a child
                                  * will wait for I/O from a client.  When
                                  * this time expires the child will die
                                  * so that resources can be reclaimed 
                                  */ 

extern int  *Fwd_inhibit_p;   /* flag to shut the children up */   

#ifdef USE_LOGSERVER
int            logServer_connected_ = 0;
logClientId    id_;
#else
cmlog_client_t cl;           /* client handle. global cauz child_death needs */
#endif

/*============================================================================

  Abs:   FWD server TCP client task (one spawned for each client)

  Name:  fwd_child

  Type:

  Args:
          Use:
          Type:
          Acc:
          Mech:

  Rem:   child task who creates fds for select call from socket desc and 
         local socket fd. Select() wakes and takes one of two paths, either 
         handle incoming data from socket and write to memory and pokes 
         another child through a local socket, or gets info from local 
         socket and send data to socket from memory. 

  Side:  Uses memory, creates network traffic

  Ret:   never?  This function does not return.  It will exit
                 through child_death if it needs to.  In the ideal world,
                 it would just keep on running forever processing messages
		 until the Lord Jesus returns (and maybe even after).

============================================================================*/

void trim(char s[]) {
	int ptr;
	int len;

	if (!s)
		return NULL;   // handle NULL string
	if (!*s)
		return s;      // handle empty string
	len = strlen(s);
	for (ptr = 0 + len - 1; (ptr >= 0) && isspace(s[ptr]); --ptr);
		s[ptr+1] = '\0';
	return s;
}

int fwd_child (int sock_fd, int unix_fd, int ip, int port, int qos)
{
	int             i;
	int             pid;              /* returned from cache lookup */
	int             dump_buf;         /* flag to force a TCp buffer dump */
	int             bytes_written;    /* return from write()for local socket */
	int             fd_setsize;       /* bit mask size for select */
	int             send_fd_setsize;  /* bit mask size for select */
	int             num_descr;        /* number of descr return from select */
	int             num_send_descr;   /* number of descr return from select */
	int             true = TRUE;      /* used in socket option calls */
	int             actual_cnt;       /* number of TCP bytes returned */
	int             nchars;           /* counter for send and receive */
	int             flags = 1;        /* flags for the child death routine */
	int             num_messages;     /* check on entries in a local socket */
	int             alloc_flag;       /* allow re-try of failed allocs */
	int             status;
	time_t          temp_time;        /* holds unix time (seconds) */

	unsigned long   block_cmd;        /* setup blocking sckets */
	unsigned long   out_len;          /* place to store output length */
	unsigned long   lookup_flag;      /* tell routine what vlue to use */
	unsigned char   local_cmd;        /* use to store a fwd_hdr command */
	char           *buff_ptr;         /* a place to hold the calloc ptr */
	char            err_msg_buf[132]; /* Place to build messages SLC size */
	char            ip_port_to_string[] = {0,0,0,0,0}; /* make string */

	fd_set          readfds;          /* desc ready for reading */
	fd_set          writefds;         /* desc ready for writing */
	struct timeval  timeout_s;        /* timeout for read select call */
	struct timeval  send_timeout_s;   /* timeout for send select call */
	struct timeval  tv_s;             /* timeout for delay select call */
	struct sockaddr_un dest_name;     /* place to make UNIX socket address */

	pipe_ts         pipe_s;           /* structure to send to local socket */
	ip_port_tu      lookup_key_u;     /* pass hash value to cache_lookup */
	ip_port_tu      real_ip_port_u;   /* Place to make up ip/port longword */
	fwd_hdr_ts      temp_hdr_s;       /* a place to fiddle with data in */
	fwd_hdr_ts     *temp_hdr_ps;      /* a place to fiddle with data out */
	fwd_cache_ts   *our_cache_ps;     /* data about this child */
	fwd_cache_ts   *cache_ps;         /* data about the child pointed to */

	char cmlog_name[] = "fwdCliS";    /* pass this name to cmlog as our name */
	char thefacility[40];
	char output[1056];

	/** USE DEFINES FOR THESE SIZES */

	typedef struct              /* Typedef for the thing that holds error message*/
	{                           /* from vms.  The vms client sender has same. */
		/****  char          time[8];  ****/
		time_t        time;                /* unix time in network order */
		/**  char          facility[8]; */
		char          facility[40];
		char          host[16];
		/**  char          user[4]; */
		char          user[40];
		int4u         severity_int;        /* integer form of severity     */
		char          severity_char[4];    /* e.g. info, warn, err, ...    */
		char          error_code[20];        
		char          spare[8];
		char          msg_str[280];        /*  message string, null terminated. */
	} fwd_err_msg_ts;

	/*
	** The world standard msg header common to VAX and micro.
	SHOULD REALLY USE STANDARD INCLUDE FILE BUT IT CONFLICTS WITH FWD_SERVER.H
	*/

	typedef struct {
		char          source[4];
		char          dest[4];
		int4u         timestamp[2];
		int2u         func;
		int2u         datalen;
	} msgheader_ts;
	          

	typedef struct {
		msgheader_ts msg_hdr_s;             /* sms header */
		fwd_err_msg_ts fwd_err_msg_s;       /* the error message itself. */
	} err_msg_ts;    /* Holds data part of message which is sms_hdr + err msg. */

	err_msg_ts *err_msg_ps;

	char message[256];
	char host[24];
	char facility[40]; /* store facility temporally */
	char severity[8];  /* store severity temp */
	char user[40];     /* store user temp */
	//char temp_error_code[24]; /* store error code temp. */
	char temp_error_code[20]; /* store error code temp. */
	char s[100];  /* store time here temporarally */

	/*********************************  CODE **********************************/

	/**DEBUG??? IS THIS NEEDED?  real_ is used for error messages if nothing else*/
	/* 
	* Makeup the local ip_port word for this child.  Used as the definitive
	* identification for this connection.  Assumption is that there can
	* only be one connection for each port on a single IP node
	*/
	real_ip_port_u.s.ip = ip;
	real_ip_port_u.s.port = port;

	fprintf(stderr,"real_ip_port: IP=%X, port=%d\n", real_ip_port_u.s.ip, real_ip_port_u.s.port);

	// Connect to epics logServer
	connect_logServer();

	// turn on KEEPALIVE so if the client crashes this task will find out and exit
	status = setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&true, sizeof true);
	if (status == ERROR) {
		if (LOG_FATAL) { fprintf(stderr,"FWDC: SO_KEEPALIVE option set failed\n"); }
		fwd_child_death (real_ip_port_u);
	}

	// set TCP input and output buffer sizes 
	i = MAX_MESSAGE_SIZE * 2;
	status = setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, (char *)&i, sizeof(i));
	if (status < 0) {
		if (LOG_FATAL) { fprintf(stderr,"FWDC: SO_SNDBUF set failed\n"); }
		fwd_child_death (real_ip_port_u);
	}

	i = MAX_MESSAGE_SIZE * 2;
	status = setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, (char *)&i, sizeof(i));
	if (status < 0) {
		if (LOG_FATAL) { fprintf(stderr,"FWDC: SO_RCVBUF set failed\n"); }
		fwd_child_death (real_ip_port_u);
	}

	/*
	* Disable the Nagle alg to force packets out, NOW!
	* 1 = TCP_NODELAY (Nagle off)  : 0 = Nagle ON
	*
	* The VMS host code does not do looping TCP reads when it reads the
	* fwd_header so if a fwd_header is split between two ethernet packets
	* it will note an error and close the connection.  When the Nagle alg
	* is enabled there is a good chance that a fwd_header will be split
	* between to ethernet packets so this is explicitly turned off here.
	* This does create additional ethernet traffic but since most transfers
	* fit into one ethernet packet it's not a huge increase. 
	*
	* If the Nagle alg MUST be turned on then we must insure that a 
	* fwd_header is not split between two ethernet packets (yea right) 
	* or change te the VMS host code to do a looping read on the fwd_header.
	* The VMS change is a difficult one but is easier than trying to insure
	* packet sizes here in the proxy
	*/
	i = 1;  /* the off flag */
	status = setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&i, sizeof(i));
	if (status < 0) {
		if (LOG_FATAL) { fprintf(stderr,"FWDC: TCP_NODELAY set failed\n"); }
		fwd_child_death (real_ip_port_u);
	}

    // Tell the user the status of the child connection
	if (LOG_DEBUG) {
		fprintf(stderr,"FWDC: Received connection request from addr %x, port %d local socket name is %s \n", 
		        our_cache_ps->real_ip_port_u.s.ip, our_cache_ps->real_ip_port_u.s.port, our_cache_ps->unix_name);
	}

	/*
	*************************************************************************
	* Start the main child loop
	*     
	* The child loop which looks for data from either the socket or
	* the local socket. The data is then forwarded and the loop begins again.
	* The UNIX sockets were used for interprocess communications so that
	* a single select call can wait for both new data from a TCP socket or
	* a new forwarding request from a UNIX socket.
	*
	* We never exit this loop.  We exit through child_death instead.
	*/
	// allocate memory for data buff
	buff_ptr = (err_msg_ts*)malloc(sizeof(err_msg_ts));

	while (1) {
		//Setup the select call timeout data. 
		timeout_s.tv_sec = SELECT_TIMEOUT;
		timeout_s.tv_usec = 0;

		// Zero descriptors, set desc for new socket, set size then go see if there is data to read from the socket.
		//FD_ZERO ((fd_set *) &readfds);
		FD_ZERO(&readfds);
		// FD_SET (unix_fd, (fd_set *) &readfds); 
		//FD_SET (sock_fd, (fd_set *) &readfds);
		FD_SET(sock_fd, &readfds);
		//fd_setsize = FD_SETSIZE;
		fd_setsize = sock_fd+1;

		fprintf(stderr, "before select FD_SETSIZE=%d, sock_fd=%d, num_descr=%d\n", fd_setsize, sock_fd, num_descr);

		if ((num_descr = select (fd_setsize, &readfds, NULL, NULL, &timeout_s)) == ERROR) {
			if (LOG_FATAL) { fprintf(stderr,"FWDC: select error\n"); }
			fwd_child_death (real_ip_port_u);
		}
		if (num_descr == 0) {
			if (LOG_FATAL) { fprintf(stderr,"FWDC: select timeout! exiting...\n"); }
			fwd_child_death (real_ip_port_u);
		}        

		fprintf(stderr, "FD_SETSIZE=%d, sock_fd=%d, num_descr=%d\n", fd_setsize, sock_fd, num_descr);
		
		//*************************************************************
		// If there is I/O pending at the network socket, handle it!
		// This is data coming INTO the forward server
		//if (LOG_DEBUG) { fprintf(stderr,"\n\n NEW MSG IS HERE***. Select just completed in child.\n"); }
		if (FD_ISSET(sock_fd, (fd_set *) &readfds)) {
			if (LOG_DEBUG) {
				fprintf(stderr,"-----------------------------------------------\n");
				fprintf(stderr,"FWDC: Child Message Socket Signal\n");
			}
			dump_buf = 0; /* Setup to not dump buffer after header */

			// Get the header information to get buffer alloc size
			if (status = tcp_get (sock_fd, (char *)&temp_hdr_s, sizeof(fwd_hdr_ts), &actual_cnt) == ERROR) {
				if (LOG_FATAL) fprintf(stderr,"FWDC: header access error\n");
				fwd_child_death (real_ip_port_u);
			}

			// Check for a wrong length in the forward header.  Hopefully
			// this is usually a zero length indicating a normal disconnected
			// socket, anyway close the socket and go away...
			if (actual_cnt != sizeof(fwd_hdr_ts)) {
				if (LOG_FATAL) fprintf(stderr,"FWDC: header size error\n");
				fwd_child_death (real_ip_port_u);
			}

			// Check that the packet is not too big for us to handle.  If
			// it is then we have no choice but to die since the serial
			// stream will be corrupt.  One day you could think of finding the
			// next good header in the stream and continuing from there.
			if (ntohl(temp_hdr_s.len) > MAX_MESSAGE_SIZE) {
				fprintf(stderr,"FWDC: Child Message Too BIG! len = %x \n", ntohl(temp_hdr_s.len));
				fwd_child_death (real_ip_port_u);
			}

			if (LOG_DEBUG) {
				fprintf(stderr,"FWDC: Child Message Header Verified\n");
				fprintf(stderr,"Child Rx: IP= %x,  port=%x, len=%x, user=%x, cmd=%x, crc=%x \n",
					ntohs(temp_hdr_s.ip_port_u.s.ip), ntohs(temp_hdr_s.ip_port_u.s.port),
					ntohl(temp_hdr_s.len), temp_hdr_s.user, temp_hdr_s.cmd, temp_hdr_s.crc);
			}
              
			// TODO CHECK THIS DUMP_BUF STUFF. 

			// Find out what the user wants us todo with this packet.  Correct
			// endian-ness as required
			if (LOG_DEBUG) fprintf(stderr,"**Got the header, The header command function is %d \n", temp_hdr_s.cmd & (int1u)PX_FUNC_MASK);

			switch (temp_hdr_s.cmd & (int1u)PX_FUNC_MASK) {
				case PX_REGISTER_FUNC:  // Really the Register Alias command 
					dump_buf = 0;  // We don't get anything more off sock 
					if (LOG_DEBUG) fprintf(stderr, "PX_REGISTER_FUNC=%d\n", temp_hdr_s.cmd & (int1u)PX_FUNC_MASK);
				break;
				case PX_REGISTER_PORT_FUNC:
					dump_buf = 1; // make sure all data is read from socket 
					if (LOG_DEBUG) fprintf(stderr, "PX_REGISTER_PORT_FUNC=%d\n", temp_hdr_s.cmd & (int1u)PX_FUNC_MASK);
				break;
				case PX_FORWARD_FUNC:
					//ADD CMLOG LOGIC HERE FOR THE FORWARD HEADER (not data) IF NEEDED>
					dump_buf = 1; // yes, we want to get stuff off socket 
					if (LOG_DEBUG) fprintf(stderr, "PX_FORWARD_FUNC=%d\n", temp_hdr_s.cmd & (int1u)PX_FUNC_MASK);
				break;
				case PX_FORWARD_ALIAS_FUNC:
					if (LOG_DEBUG) fprintf(stderr, "PX_FORWARD_ALIAS_FUNC=%d\n", temp_hdr_s.cmd & (int1u)PX_FUNC_MASK);
				break;
				default:
					if (LOG_FATAL) fprintf(stderr,"FWDC: No valid forwarding command! %i\n", temp_hdr_s.cmd);
					dump_buf = 1; // make sure all data is read from socket 
				break;
			} // end 'o case 


			// Remove any extra data from the socket if indicated.  If
			// no data needs to be removed then go back to select call.
			dump_buf: out_len = ntohl(temp_hdr_s.len);

			//TODO CHECK USE OF DUMP_BUF LOGIC HERE AND ABOVE. 
			// Is it kinda like an 'ok to proceed flag'? 
			if ((dump_buf == 1) && (out_len > 0)) {
				// sending structs, so shouldn't buffer always be the same size??
				//out_len = sizeof(err_msg_ts);  // resize out_len to struct size, doesn't work for SLC msgs
				fprintf(stderr, "dump_buf=%d, out_len=%lu, sizeof(err_msg_ts)=%d\n", dump_buf, out_len, sizeof(err_msg_ts));				
				//buff_ptr = (char *)calloc (1,out_len);  
				memset(buff_ptr, '\0', sizeof(err_msg_ts));
				// RONM changed from malloc 
				// buff_ptr contains the input from the socket */
				if ((status = tcp_get(sock_fd, buff_ptr, out_len, &actual_cnt)) == ERROR) {
					if (LOG_FATAL) fprintf(stderr,"FWDC: dump data access error\n");
					fwd_child_death (real_ip_port_u);
				}

				// sending structs, but SLC is sending chopping off empty part of message text and sending shorter length in temp_hdr_s.len
				if (actual_cnt != ntohl(temp_hdr_s.len)) {
					if (LOG_FATAL) fprintf(stderr,"FWDC: dump data size error\n");
					fwd_child_death (real_ip_port_u);
				}

				// SEND TO cmlogServer.
				err_msg_ps = (err_msg_ts *)buff_ptr;

				// time is in the format 14-Feb-2012 14:24:02.09
				temp_time = time(NULL);
				if (LOG_DEBUG) fprintf(stderr, "Current RAW* time is: %d \n", temp_time);
				temp_time = htonl(err_msg_ps->fwd_err_msg_s.time);
				if(LOG_DEBUG) { 
					//					fprintf(stderr, "Buffer RAW* time is:  %d \n", temp_time);
					fprintf(stderr, "Buffer ctime time is: %28.28s", ctime(&temp_time));
					//					fprintf(stderr, "Test time is: %28.28s \n", asctime(localtime(&temp_time)));
				}
/*
				{     
				struct tm * tm_ps;
				tm_ps = localtime(&temp_time);
				if(LOG_DEBUG) {
					strftime(s,100,"%c",localtime(&temp_time));
					fprintf(stderr, "strftime is: %28.28s \n", s);
				}
				}
*/
				// Null terminate the host. 
				sprintf (host, "%-.15s", err_msg_ps->fwd_err_msg_s.host);
				// Null terminate the facility 
				sprintf (facility, "%-.39s", err_msg_ps->fwd_err_msg_s.facility);
				// Null terminate the user 
				sprintf (user, "%-.39s", err_msg_ps->fwd_err_msg_s.user);
				// Convert the unix time we were passed to host format. 
				temp_time = htonl(err_msg_ps->fwd_err_msg_s.time); 
				// Fix up the error code DELETE THIS 
				sprintf (temp_error_code, "%-.19s", err_msg_ps->fwd_err_msg_s.error_code);
				// Null terminate the servity character value 
				sprintf (severity, "%-.4s", err_msg_ps->fwd_err_msg_s.severity_char);

				if(LOG_DEBUG) {
					fprintf(stderr, " facility=%s\n", facility);
					fprintf(stderr, " host=%s\n", host);
					fprintf(stderr, " user=%s\n", user);
					fprintf(stderr, " severity char=%4s\n", severity);
					fprintf(stderr, " error code=%s\n", temp_error_code);
					fprintf(stderr, " msg=%s\n", err_msg_ps->fwd_err_msg_s.msg_str);
					fprintf(stderr, " msg len=%d \n", strlen(err_msg_ps->fwd_err_msg_s.msg_str));            
/*					fprintf(stderr, "host=%15.15s\n", err_msg_ps->fwd_err_msg_s.host);
					fprintf(stderr, "Buffer user is: %39.39s \n", err_msg_ps->fwd_err_msg_s.user);
					fprintf(stderr, "Buffer severity char is: %4.4s \n", err_msg_ps->fwd_err_msg_s.severity_char);
					fprintf(stderr, "Buffer error code is: %20.20s \n", err_msg_ps->fwd_err_msg_s.error_code);
					fprintf(stderr, "Buffer string is: %s \n", err_msg_ps->fwd_err_msg_s.msg_str);
					fprintf(stderr, "Buffer string strlen is: %d \n", strlen(err_msg_ps->fwd_err_msg_s.msg_str));            
					fprintf(stderr, "**** err_code_char is: %s \n",temp_error_code);
					fprintf(stderr, "**** about to send facility = %s \n",facility);
*/
				}

#ifdef USE_LOGSERVER

				// make sure connected to oracle
				if (!logServer_connected_) connect_logServer();

				//fprintf(stderr, "host=%16s, facility=%40s, user=%40s, code=%20s, severity=%4s, msg=%s\n", err_msg_ps->fwd_err_msg_s.host, err_msg_ps->fwd_err_msg_s.facility, err_msg_ps->fwd_err_msg_s.user, err_msg_ps->fwd_err_msg_s.error_code, err_msg_ps->fwd_err_msg_s.severity_char, err_msg_ps->fwd_err_msg_s.msg_str);
				//char output[1056];
				memset(output, '\0', sizeof(output));

				// DO THE NULL TERMINATES FROM ABOVE 
				// Null terminate the host. 
				sprintf (host, "%-.15s", err_msg_ps->fwd_err_msg_s.host); 
				trim(host);
				// Null terminate the facility 
				sprintf (facility, "%-.39s", err_msg_ps->fwd_err_msg_s.facility);
				trim(facility);
				// Null terminate the user 
				sprintf (user, "%-.39s", err_msg_ps->fwd_err_msg_s.user);
				trim(user);
				// Convert the unix time we were passed to host format. 
				temp_time = htonl(err_msg_ps->fwd_err_msg_s.time); 
				// Fix up the error code DELETE THIS 
				sprintf (temp_error_code, "%-.19s", err_msg_ps->fwd_err_msg_s.error_code);
				trim(temp_error_code);
				// Null terminate the servity character value 
				sprintf (severity, "%-.4s", err_msg_ps->fwd_err_msg_s.severity_char);
				trim(severity);

				// prepend SLC if coming from mcc or MCC host 
				// copy facility to longer string 
				if ((strncmp(host, "MCC", 3)==0) || (strncmp(host, "mcc.", 4)==0)) {
					strcpy(thefacility, "SLC-");
					strncat(thefacility, facility, 35);
					trim(thefacility);
					printf("host is %s, update current facility=%s to %s\n", host, facility, thefacility);
				} else {
					strcpy(thefacility, facility);
				}					

				// TO DO: MORE TAGS! ? 
				// NOW SEND THE WHOLE MESSAGE WITH TAGS 
				// sprintf(output, "fac=%s host=%s user=%s %s\n", facility, host, user, err_msg_ps->fwd_err_msg_s.msg_str); */ 
				//sprintf(output, "fac=%s host=%s user=%s %s\n", thefacility, host, user, err_msg_ps->fwd_err_msg_s.msg_str); 
				//char test[1056];
				sprintf(output, "fac=%s host=%s user=%s code=%s sevr=%s %s\n", thefacility, host, user, temp_error_code, severity, err_msg_ps->fwd_err_msg_s.msg_str);
				/* THIS IS OUTPUT FOR SLC TESTING 9/27/11 */
				fprintf(stderr, "===> SENDING TO LOGSERVER: %s", output);

				//fprintf(stderr, "fwd_hdr_ts=%d, msg_hdr_s=%d fwd_err_msg_s=%d\n", sizeof(fwd_hdr_ts), sizeof(err_msg_ps->msg_hdr_s), sizeof(err_msg_ps->fwd_err_msg_s));
				if (logServer_connected_) {
					logClientSend(id_, output);
					logClientFlush(id_);
					fprintf(stderr, "Sent to logServer\n");
				}
#else
				// There's a bug in cmlog time processing.  We pass the vms timestamp from the error message in the "time"
				// cdev tag but don't ever use it in the browser.  That's because the cmlog client code timestamps  error messages
				// in the cmlogTime cdev tag and we can't use that one because cmlog client will just write over anything we put in there
				// with it's own time stamp.  Jie said he will fix that in the next release where we can pass a value ourselves.
				// Epics messaes have the the timestamp  in cmlogTime, so we want to use that tag too so that the time shows
				// up on the browser using one tag for slc and epics. So for now, we let cmlog client code timestamp the vms mesage
				// instead of using the timestamp that came from vms.  But, we do pass the vms timestamp in the 'time' cdev tag in case 
				// anyone wants to look at it.	

				// Ship the message to cmlogServer with this cmlog API call 
				status = cmlog_logmsg (cl,         /* cmlog_client_t handle */
										64,    /* verbosity (length)  THESE NEED DEFINES*/
										0,     /* cdev tag = severity (integer) */
										0,     /* cdev tag = code (integer) */
										user,          /* cdev tag = facility */
										"time = %s process = %s host = %s severity = %s code = %s status = %d text = %s", 
										ctime(&temp_time),      /*format specifier,like C*/
										facility, host, severity, temp_error_code, err_msg_ps->fwd_err_msg_s.severity_int, err_msg_ps->fwd_err_msg_s.msg_str); 
#endif  // end if use_logserver 
				// continue;  We're using dump_buf logic to get msg off sock
				//fprintf(stderr, "FREEd buff_ptr\n");
				//free(buff_ptr);                 

			} // endif dump_buf and out_len
		} /* endif readfds for socket is set in select */
	} /* end of main child while loop */

    /*
     *  ????????????????    free_client(client); 
     */

/*** NEVER RETURNS. IT EXITS THROUGH CHILD_DEATH 
    return 0;
***/

} /* end of child routine */

/*============================================================================

  Abs:    wrapper for the TCP stream receive work

  Name:   tcp_get

  Type:

  Args:
          Use:
          Type:
          Acc:
          Mech:

  Rem:

  Side:   

  Ret:

============================================================================*/
static int tcp_get(int sock_fd, char *buff, int request_cnt, int *actual_cnt)
{

	int  nchars;
	int  bytes_left;
	long status = 0;
	int  iss = 0;

    bytes_left = request_cnt; /* let's get all the bytes */
    while (bytes_left > 0) {
      /*
       * do we want a select cal here to try and not block waiting for data?
       * We usually get here from a select call but some protections might
       * be in order. Maybe a flag to use it or not...
       *
       *  if (flag == USE_SELECT) then use it with timeout in seconds
       */
        nchars = recv(sock_fd, (buff + (request_cnt - bytes_left)), bytes_left, 0);
        if (nchars==0) {
            if(LOG_ERROR) fprintf(stderr,"FWDC: zero length TCP recv\n");
            iss = ERROR;
            break;
        } else if (nchars<=0) {
            perror ("FWDC:");
            // normal conn lost conditions
            if ((status!=ECONNABORTED && status!=ECONNRESET && status!=ETIMEDOUT) || LOG_FATAL) {
              fprintf(stderr,"FWDC: client disconnect - errno=%d, sock_fd=%d\n", errno, sock_fd);
            } else {
               fprintf(stderr,"FWDC: child TCP recv error - errno=%d\n", status);
            }
            iss = ERROR;
            break;
        } else if(nchars > 0) {
            bytes_left -= nchars;
        }
    }/* end of while loop */

    *actual_cnt = request_cnt - bytes_left;

	fprintf(stderr, "tcp_get(): actual_cnt=%d, request_cnt=%d, bytes_left=%d\n", *actual_cnt, request_cnt, bytes_left);
    return (iss);
} /* end 'o  wrapper */


/*============================================================================

  Abs:   kill errant child

  Name:  fwd_child_death

  Type:  static int

  Args:  real_ip_port_u
          Use: read
          Type: ip_port_tu
          Acc: value
          Mech: 

  Rem:  Here is how this code should work:
        - set the cache state to CLOSING to stop more I/O 
        - close the socket descriptor
        - empty the local socket freeing buffers as you go
        - close the local socket descriptor
        - clear the cache entry for reuse

  Side: possible memory hole if local socket cannot be cleared.  Network 
        activity from the socket close. Empty but unusable cache entry if not 
        completed. Deleted task if successful

  Ret: 

============================================================================*/
static int fwd_child_death (ip_port_tu real_ip_port_u)
{
  int            status = 0;
  char           err_msg_buf[132]; /* Place to build messages */
  fwd_cache_ts  *cache_ps;         /* data about the child pointed to */
  int            pid;              /* local place to store the pid */

   /* 
    * Tell the host someone is closing
    *
    * Well this is how you could do it but since it just makes more noise
    * on the central host lets not for now.
    *
    *    memset (err_msg_buf, 0, sizeof (err_msg_buf));
    *    sprintf(err_msg_buf,
    *            "Proxy task closing, IP/PORT = %X", real_ip_port_u.word);
    *    fwd_err (err_msg_buf);
    */

               
fprintf(stderr,"Were INSIDE FWD_CHILD_DEATH.  Whats wrong?\n");


        if (LOG_FATAL)
        {
           fprintf(stderr,"CHILD: task end IP=%X, port=%d\n",
                   real_ip_port_u.s.ip, real_ip_port_u.s.port);
        }


        /*
         * Simple, stupid death. 
         */

	/** THIS IS NOT COMPLETE** we don't always exit here.  But
	    the signal handlers are in another module and they
	    don't have cl.  So, we'll need to external them there
	    and do cmlog_close in the signal handlers.  But this
	    is ok for now, since the process run-down disconnects
	    us anyway.
	**/

#ifdef USE_LOGSERVER

#else
     cmlog_close (cl);   /* close out the connection to cmlogServer */
#endif


     exit (1);  /* UNIX death  Should we do more??*/

     /* RONM TOOK OUT MORE ELABORATE CHECKING HERE FOR CACHE ISSUES */

}


/*============================================================================

  Abs:   calculate and return a one's compliemnt checksum of the header

  Name:  fwd_chksum_make

  Type:  unsigned char

  Args:
          Use:
          Type:
          Acc:
          Mech:

  Rem:   for now let's spend the time for the calcualtion but just use the
         fake value until the VMS code does the checksums

  Side:  burns CPU time, might want to make it assy code

  Ret:   unsigned char

============================================================================*/
unsigned char fwd_chksum_make (fwd_hdr_ts *fwd_hdr_ps)
{
  unsigned short cnt;
  unsigned short sum;
  char * hdr_cp;

    cnt = sizeof(fwd_hdr_ts);
    hdr_cp = (char *) fwd_hdr_ps;
    sum = 0;

    /*
     * Reach in and zero the crc field in case the user has not cleared it
     */
   fwd_hdr_ps->crc = 0;

    /*
     * do a straight two's complement sum over the entire header
     */
    while (cnt--)
        sum += *hdr_cp++;

    /* 
     * add in the sum of the `carry' bits, making this one's complement
     */
    sum = (sum & (int2u)0xFF) + ((sum >> 8) & (int2u)0xFF);
    if (sum & (int2u)0x100)     /* one last possible carry */
        sum = (sum + 1) & (int2u)0xFF;
    if (sum == (int2u)0xFF)     /* remove the -0 ambiguity */
        sum = (int2u)0;

    /*
     * The checksum value is the value which will make the checksum of this
     * header equal to zero.  That makes checking the checksum easier since
     * it is always compared to zero.
     */
    /*    sum = ~sum; does not work yet */

    sum = TEMP_CRC; /* force the default for now */
    return ((unsigned char) sum);
}

/*============================================================================

  Abs:    check the crc value of a header

  Name:   fwd_chksum_check

  Type:   int

  Args:
          Use:
          Type:
          Acc:
          Mech:

  Rem:   for now let's spend the time for the calcualtion but just use  the 
         fake value until the VMS code does the checksums 

  Side:  Burns more CPU!

  Ret:

============================================================================*/
int fwd_chksum_check (fwd_hdr_ts *fwd_hdr_ps)
{
  fwd_hdr_ts local_fwd_hdr_s;
  unsigned short cnt;
  unsigned short sum;
  unsigned char  chksum;
  char *hdr_cp;

    local_fwd_hdr_s = *fwd_hdr_ps;  /* make a local fast copy */
    cnt = sizeof(fwd_hdr_ts);
    hdr_cp = (char *)&local_fwd_hdr_s;
    sum = 0;

    chksum = local_fwd_hdr_s.crc; /* get original checksum */
    local_fwd_hdr_s.crc = 0;   /* then zero it */

    /*
     * do a straight two's complement sum over the entire header
     */
    while (cnt--)
        sum += *hdr_cp++;

    /* 
     * add in the sum of the `carry' bits, making this one's complement
     */
    sum = (sum & (int2u)0xFF) + ((sum >> 8) & (int2u)0xFF);
    if (sum & (int2u)0x100)     /* one last possible carry */
        sum = (sum + 1) & (int2u)0xFF;
    if (sum == (int2u)0xFF)     /* remove the -0 ambiguity */
        sum = (int2u)0;

    if (chksum == TEMP_CRC || chksum == (unsigned char)sum)
        return (0);
    else
        return (-1);
}


#ifdef USE_LOGSERVER

//----------------------------------------------------------------------------
// This function connects to epics iocLogAndFwdServer 
//----------------------------------------------------------------------------

int connect_logServer(void)
{

#define MAX_CONN_TRIES 10
#define NUM_RETRIES_TO_WAIT 1000

  static int num_conn_tries = 0;
  static int num_retries_waiting = 0;
  //
  // We're called by the send routine for each message when it sees that logServer is not connected.
  // Try to reestablish connection MAX_CONN_TRIES number of times.
  // If that limit is reached, then, don't retry connection for NUM_RETRIES_TO_WAIT number of times.
  // After that, reset both counters and try to connect again (starting the cycle over again).
  // 
  if(num_conn_tries >= MAX_CONN_TRIES) {
    if(num_retries_waiting >= NUM_RETRIES_TO_WAIT) {
      num_retries_waiting = 0;
      num_conn_tries = 0;      
    }
    num_retries_waiting++;
  }
  num_conn_tries++;
    
  struct in_addr server_addr;
  unsigned short server_port;
  struct hostent *hp;
  char* temp_p;
  char host_ip[100];
  char *pstring;
  struct in_addr iaddr;

  char dummy_a[10];

  logServer_connected_ = 0;  // DEBUG How does one delete the old logserver?

/*  hp = gethostbyname("cdlx03"); */
/*  hp = gethostbyname("lcls-prod02"); */

	pstring = envGetConfigParam(
			&EPICS_IOC_LOG_INET, 
			sizeof host_ip,
			host_ip);

  	inet_aton(host_ip, &iaddr);

	hp = gethostbyaddr((const void*)&iaddr, strlen(host_ip), AF_INET);


  if (hp == NULL) {
                fprintf(stderr, "iocLogServer host unknown for %s, host_ip len=%d\n", host_ip, strlen(host_ip));
		return 0;
  }
  else {
    /***
      memcpy(&server_addr, hp->h_addr, hp->h_length);
    ***/

      memcpy(&server_addr, hp->h_addr, hp->h_length);

      server_port = 7004;                                  //DEBUG GET FROM ENV.
      id_ = logClientCreate(server_addr, server_port);
      logServer_connected_ = 1;
  }
}

#endif





















