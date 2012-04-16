/*
      **MEMBER**=NONE
      **CMS**=NONE
==============================================================================

  Abs:  Main entry point for the forward server code.  Parent task.

  Name:  fwd_server.c

  Proto: fwd_server.h

  Auth: 01-Oct-1997, Ron MacKenzie
  Rev:  DD-MMM-YYYY, Reviewer's Name (.NE. Author's Name)

------------------------------------------------------------------------------

  Mod:  (newest to oldest)
       20-Jul-1999, Ron MacKenzie
          I took the proxy forward server code and scaled it down
          to make this server.  

Questions I have.
-------------------------------------------
1. What's this fwdd.pid file stuff.  It's for killing the correct daemon?
       Can I or do I want to use it on flora/gateway?
2. Why set uid to 99?  I get two errors on flora under my account
         'not owner' and 'cant set uid to 99'   So I took it out.
3. If I run it when it's already running I get flooded with errors.
         Did I take something out that is needed?
         error :== "select bad file number"
4. What's the official way to kill the server and children?  ps -Af / grep/kill
5. How to do log files, debug output, etc : lOG_DEBUG, LOG_FATAL?
6. How to debug children (gdb?)

============================================================================*/
static char *version_info = "fwd_server.c    .20 11/08/95,07/29/98";

/*
 * This code is copied and modified UNIX wrapper from the SSH Demon indicated
 * below ...
 * $Id: fwd_server.c,v 1.1.1.1 2007/10/26 23:35:34 ronm Exp $
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>

#include <sys/ipc.h>   /* for SYS V Interprocess Communications */
#include <sys/shm.h>   /* for the shared memory protos and types */
#include <sys/un.h>    /* AF_UNIX socket descriptor stuff */

#include "servconf.h"   /* configuration structure mec */

#include "fwd_server.h"

#ifdef HAVE_ULIMIT_H
#include <ulimit.h>
#endif /* HAVE_ULIMIT_H */

#ifdef HAVE_ETC_SHADOW
#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif /* HAVE_SHADOW_H */
#ifndef SHADOW
#define SHADOW "/etc/shadow"
#endif
#endif /* HAVE_ETC_SHADOW */

#ifdef LIBWRAP
#include <tcpd.h>
#include <syslog.h>
#ifdef NEED_SYS_SYSLOG_H
#include <sys/syslog.h>
#endif /* NEED_SYS_SYSLOG_H */
int allow_severity = LOG_INFO;
int deny_severity = LOG_WARNING;
#endif /* LIBWRAP */

#ifndef O_NOCTTY
#define O_NOCTTY	0
#endif

#define FWD_VERSION "0.01"   /* add receipts ... used in error msgs */
#define HOSTTYPE "Solaris"

#define SERVER_CONFIG_FILE ".fwd_cfg"
#define ETCDIR "/etc"
/* Server configuration options. */
ServerOptions options;

/* Name of the server configuration file. */
char *config_file_name = SERVER_CONFIG_FILE;

/* Debug mode flag.  This can be set on the command line.  If debug
   mode is enabled, extra debugging output will be sent to the system
   log, the daemon will not go to background, and will exit after processing
   the first connection. */
int debug_flag = 0;

/* Flag indicating that the daemon is being started from inetd. */
int inetd_flag = 0;

/* argv[0] without path. */
char *av0;

/* Saved arguments to main(). */
char **saved_argv;

/* This is set to the socket that the server is listening; this is used in
   the SIGHUP signal handler. */
int listen_sock;

/* This is set to true when SIGHUP is received. */
int received_sighup = 0;

/* Prototypes for various functions defined later in this file. */
void fwd_run_child (int sock_in, int sock_out);
void get_remote_ipaddr (int);
void get_remote_port (int);
int fwd_child (int sock_fd, int pipe_fd, int ip, int port, int qos);

/*
 * Ones we need here 
 */
#define debug printf
#define error printf 
#define fatal printf 
#define log_msg printf

#define RETSIGTYPE void

/* Signal handler for SIGHUP. Fwd_server execs itself when it receives SIGHUP;
   the effect is to reread the configuration file */
RETSIGTYPE sighup_handler(int sig)
{
  received_sighup = 1;
  signal(SIGHUP, sighup_handler);
}

/* Called from the main program after receiving SIGHUP.  Restarts the 
   server. */
void sighup_restart()
{
  log_msg("Received SIGHUP; restarting.");
  close(listen_sock);
  execvp(saved_argv[0], saved_argv);
  log_msg("RESTART FAILED: av[0]='%s', error: %s.", 
      saved_argv[0], strerror(errno));
  exit(1);
}

/* Generic signal handler for terminating signals in the master daemon. 
   These close the listen socket; not closing it seems to cause "Address
   already in use" problems on some machines, which is inconvenient. */
RETSIGTYPE sigterm_handler(int sig)
{
  FILE *f;
  int process_count; /* number of children terminated */

  log_msg("Received signal %d; terminating.", sig);
  close(listen_sock);

  /*
   * Kill all children also
   */
  process_count = kill (0, SIGTERM); /* kill 'em  all (Linux says < -1 ??) */

  log_msg("SIGTERMing %d processes", process_count);

  exit(255);   /* WHATZIT?  255? */
}

/* SIGCHLD handler.  This is called whenever a child dies.  This will then 
   reap any zombies left by exited c. */
RETSIGTYPE main_sigchld_handler(int sig)
{
  int status;
#ifdef HAVE_WAITPID
  /* Reap all childrens */
  while (waitpid(-1, &status, WNOHANG) > 0)
    ;
#else
  wait(&status);
#endif
  signal(SIGCHLD, main_sigchld_handler);
}



/*============================================================================
  Abs:  Main program for the daemon
  Name: main
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
int main(int ac, char **av)
{
  extern  int optind;
  extern  char *optarg;
  int     opt;
  int     aux;
  int     sock_in;
  int     sock_out;
  int     newsock;
  int     i;
  int     pid;
  int     on = 1;
  int     ret;
  fd_set  fdset;
  FILE   *f;
  struct  sockaddr_in sin;
  char    buf[100];         /* Must not be larger than remote_version. */
  char    unix_name[20];    /* file name for /tmp removal */

#ifdef SO_LINGER
  struct linger linger;
#endif /* SO_LINGER */


  /* Save argv[0] for server restarts */
  saved_argv = av;
  if (strchr(av[0], '/'))
    av0 = strrchr(av[0], '/') + 1; /* RONM prev. ADDED cast on 1 for gcc err*/
  else
    av0 = av[0];

  /* Setup a few defaults here */
  options.port = FWD_SERVER_PORT; /* standard fwd_server port */

  /* Parse command-line arguments. */
  while ((opt = getopt(ac, av, "f:p:b:k:h:g:diq")) != EOF)
    {
      switch (opt)
	{
	case 'f':
	  config_file_name = optarg;
	  break;
	case 'd':
	  debug_flag = 1;
	  break;
	case 'i':
	  inetd_flag = 1;
	  break;
	case 'q':
	  options.quiet_mode = 1;
	  break;
	case 'p':
	  options.port = atoi(optarg);
	  break;
	case '?':
	default:

	  fprintf(stderr, "fwd version %s [%s]\n", FWD_VERSION, HOSTTYPE);
	  fprintf(stderr, "Usage: %s [options]\n", av0);
	  fprintf(stderr, "Options:\n");
	  fprintf(stderr, "  -f file    Configuration file (default %s/fwd_config)\n", ETCDIR);
	  fprintf(stderr, "  -d         Debugging mode\n");
	  fprintf(stderr, "  -i         Started from inetd\n");
	  fprintf(stderr, "  -p port    Listen on the specified port (default: 22)\n");
	  exit(1);
	}
    }

  /* Check certain values for sanity. */

  if (options.port < 1 || options.port > 65535)
    {
      fprintf(stderr, "fatal: Bad port number.\n");
      exit(1);
    }

  /* Check that there are no remaining arguments. */
  if (optind < ac)
    {
      fprintf(stderr, "fatal: Extra argument %s.\n", av[optind]);
      exit(1);
    }

  debug("fwd_server version %.100s [%.100s]\n", FWD_VERSION, HOSTTYPE);

  /*
   * Set the user and group id to something less than root!
   */

  /**
  if (setuid((uid_t)99) == ERROR)
  {
      perror ("FWDD:");
      fprintf(stderr,"FWDD: Can't set user id to %d\n", 99);
  }
  **/

  /*
   * Tried to (setgid((gid_t)99) but always returned no permitted! Why?
   */

  /* 
   * If not in debugging mode, and not started from inetd, disconnect from
   * the controlling terminal, and fork.  The original process exits. 
   */
  if (!debug_flag && !inetd_flag)
#ifdef HAVE_DAEMON
    if (daemon(0, 0) < 0)
      error("daemon: %.100s", strerror(errno));
#else /* HAVE_DAEMON */
    {
#ifdef TIOCNOTTY
      int fd;
#endif /* TIOCNOTTY */

      /* Fork, and have the parent exit.  The child becomes the server. */
      if (fork())
	exit(0);

      /* Redirect stdin, stdout, and stderr to /dev/null. */
/* Na, let's not!
 *      freopen("/dev/null", "r", stdin);
 *     freopen("/dev/null", "w", stdout);
 *     freopen("/dev/null", "w", stderr);
 */

      /* Disconnect from the controlling tty. */
#ifdef TIOCNOTTY
      fd = open("/dev/tty", O_RDWR|O_NOCTTY);
      if (fd >= 0)
	{
	  (void)ioctl(fd, TIOCNOTTY, NULL);
	  close(fd);
	}
#endif /* TIOCNOTTY */
    }
#endif /* HAVE_DAEMON */
    
  /* Reinitialize the log (because of the fork above). */
    /*  log_init(av0, debug_flag && !inetd_flag, 
	   debug_flag || options.fascist_logging, 
	   options.quiet_mode, options.log_facility);
  */
  
  /* Chdir to the root directory so that the current disk can be unmounted
     if desired. And for now use permissions a shown */
  chdir("/");
 
  /* Start listening for a socket, unless started from inetd. */
  if (inetd_flag)
    {
      int s1, s2;
      s1 = dup(0);  /* Make sure descriptors 0, 1, and 2 are in use. */
      s2 = dup(s1);
      sock_in = dup(0);
      sock_out = dup(1);
      /* We intentionally do not close the descriptors 0, 1, and 2 as our
	 code for setting the descriptors won\'t work if ttyfd happens to
	 be one of those. */
      debug("inetd sockets after dupping: %d, %d", sock_in, sock_out);

    }
  else
    {
      /* Create socket for listening. */
      listen_sock = socket(AF_INET, SOCK_STREAM, 0);
      if (listen_sock < 0)
	fatal("socket: %.100s", strerror(errno));

      /* Set socket options.  We try to make the port reusable and have it
	 close as fast as possible without waiting in unnecessary wait states
	 on close. */
      setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (void *)&on, 
		 sizeof(on));
#ifdef SO_LINGER
      linger.l_onoff = 1;
      linger.l_linger = 15;
      setsockopt(listen_sock, SOL_SOCKET, SO_LINGER, (void *)&linger, 
		 sizeof(linger));
#endif /* SO_LINGER */

      /* Initialize the socket address. */
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = htonl(INADDR_ANY);
      sin.sin_port = htons(options.port);

      /* Bind the socket to the desired port. */
/*
 * Seems that we can bind to the same port that a previous fwd_server
 * is listing on!  How can this be?
 */
      if (bind(listen_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
      {
        error("bind: %.100s", strerror(errno));
        shutdown(listen_sock, 2);
        close(listen_sock);
        fatal("Bind to port %d failed: %.200s.", options.port,
               strerror(errno));
      }

      if (!debug_flag)
      {
	/* Record our pid in to make it easier to kill the
           correct fwdd.  We don't want to do this before the bind above
           because the bind will fail if there already is a daemon, and this
           will overwrite any old pid in the file. */

/**
ASK MARK ABOUT THIS....
        f = fopen("/var/run/fwdd.pid", "w");
        if (f)
        {
          fprintf(f, "%u\n", (unsigned int)getpid());
          fclose(f);
        }
**/
      }

      /* Start listening on the port. */
      log_msg("Server listening on port %d\n", options.port);
      if (listen(listen_sock, 5) < 0)
	fatal("listen: %.100s", strerror(errno));

      /* Arrange to restart on SIGHUP.  The handler needs listen_sock. */
      signal(SIGHUP, sighup_handler);
      signal(SIGTERM, sigterm_handler);
      signal(SIGQUIT, sigterm_handler);
      signal(SIGCHLD,SIG_IGN);

      /* Stay listening for connections until the system crashes or the
	 daemon is killed with a signal. */
      for (;;)
      {
	  if (received_sighup)
	    sighup_restart();
	  
	  /* Wait in select until there is a connection. */
	  FD_ZERO(&fdset);
	  FD_SET(listen_sock, &fdset);
	  ret = select(listen_sock + 1, &fdset, NULL, NULL, NULL);
	  if (ret < 0 || !FD_ISSET(listen_sock, &fdset))
	  {
	      if (errno == EINTR)
		continue;
	      error("select: %.100s", strerror(errno));
	      sleep (1); /* if we are already running, we'll loop fast here*/
	      continue;
	  }
	  
	  aux = sizeof(sin);
	  newsock = accept(listen_sock, (struct sockaddr *)&sin, &aux);
	  if (newsock < 0)
	  {
	      if (errno == EINTR)
		continue;
	      error("accept: %.100s\n", strerror(errno));
	      continue;
	  }

	  /* Got connection.  Fork a child to handle it, unless we are in
	     debugging mode. */
	  if (debug_flag)
	  {
	      /* In debugging mode.  Close the listening socket, and start
		 processing the connection without forking. */
	      debug("Server will not fork when running in debugging mode.");
	      close(listen_sock);
	      sock_in = newsock;
	      sock_out = newsock;
	      pid = getpid();
	      break;
          }
	  else
          {
      log_msg("*FORKING a CHILD to handle the connection just made**\n");
	      /* Normal production daemon.  Fork, and have the child process
		 the connection.  The parent continues listening. */
	      if ((pid = fork()) == 0)
              { 
		  /* Child.  Close the listening socket, and start using
		     the accepted socket.  Reinitialize logging (since our
		     pid has changed).  We break out of the loop to handle
		     the connection. */
		  close(listen_sock);
		  sock_in = newsock;
		  sock_out = newsock;

		  break; /* Child context so move on */
	      }
	  }

	  /* Parent.  Stay in the loop. */
	  if (pid < 0)
	    error("fork: %.100s\n", strerror(errno));
	  else
            ;

	  /* Close the new socket (the child is now taking care of it). */
	  close(newsock);
	}
    }

/*===========================================================================
  This is the child processing a new connection.
 ============================================================================*/
  /*  We will not restart on SIGHUP since it no longer makes sense. */
  /* Lets try the daemon defaults!
   *  signal(SIGALRM, SIG_DFL);
   *  signal(SIGHUP, SIG_DFL);
   * signal(SIGTERM, SIG_DFL);
   * signal(SIGQUIT, SIG_DFL);
   */

  signal(SIGTERM, SIG_DFL);

  /* Ignore the child signal so there will be no zombies */
  signal(SIGCHLD, SIG_IGN);

  /* Set socket options for the connection.  We want the socket to close
     as fast as possible without waiting for anything.  If the connection
     is not a socket, these will do nothing. */
  /* setsockopt(sock_in, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on)); */
#ifdef SO_LINGER
  linger.l_onoff = 1;
  linger.l_linger = 15;
  setsockopt(sock_in, SOL_SOCKET, SO_LINGER, (void *)&linger, sizeof(linger));
#endif /* SO_LINGER */

  /* Log the connection. */
  /*  log_msg("Connection from %.100s port %d", 
      get_remote_ipaddr(), get_remote_port()); ^^*/

  /* Handle the child - cleans up system stuff and calls fwd_child to loop */
      fwd_run_child (sock_in, sock_out);

  exit(0);
}

/*============================================================================
  Abs:   Performs common processing for the child, such as setting up the 
         environment, closing extra file descriptors, setting the user and
         group ids, and executing the forwarding algorithm
  Name:  fwd_run_child
  Type:  void
  Args:             
          Use:
          Type:
          Acc:
          Mech:
  Rem:
  Side:
  Ret:
============================================================================*/

/* Performs common processing for the child, such as setting up the 
   environment, closing extra file descriptors, setting the user and group 
   ids, and executing the forwarding algorithm */

void fwd_run_child (int sock_in, int sock_out)
{
  int           i;
  int           ip;
  int           port;
  int           qos;
  int           pid;
  int           remote_port;
  int           nchars;
  int           count = 256;
  int           status;
  int           addrlen;          /* temp for getpeername call */  
  int           unix_fd;          /* Unix socket descriptor LEAVE IN */ 
  char          unix_name[20];    /* name for the UNIX socket */
  char          buf[256];
  char         *argv[10];
  char         *remote_ip;
  char        **env;
  unsigned int  envsize;
  extern char **environ;
  const char   *shell;
  const char   *cp;
  FILE         *f;
  struct stat  st;
  struct sockaddr_in  serveraddr;       /* server's address */
  struct sockaddr_in  tempaddr;         /* for temp storage */
  struct sockaddr_un  name;             /* AF_UNIX name for socket */
  fwd_cache_ts        fwd_cache_s;      /* for fwd_cache_init call */

  /*
   * Setup the local Unix socket where output requests come into this child.
   * First make a AF_UNIX namespace name for this socket using the pid. Then
   * get a socket descriptor and bind it to that name.
   */
  pid = getpid();



  memset ((char *)&tempaddr, 0, sizeof (struct sockaddr));
  addrlen = sizeof (struct sockaddr);
  if (getpeername (sock_in, (struct sockaddr *)&tempaddr, &addrlen) == ERROR)
  {
    fprintf(stderr,"FWDC: getpeer error\n");
    /* close and exit? */
  } 



  status = fwd_child (sock_in, unix_fd, fwd_cache_s.real_ip_port_u.s.ip,
                      fwd_cache_s.real_ip_port_u.s.port, qos);

egress:
    close (sock_in); /* close the TCP socket */
    /* close (unix_fd); */ /* close the local socket */
    /* remove (unix_name); */ /* remove the file with a char * */
    exit (255);

  /* Close the connection descriptors; note that this is the child, and the 
     server will still have the socket open, and it is important that we
     do not shutdown it.  Note that the descriptors cannot be closed before
     building the environment, as we call get_remote_ipaddr there. */

  /* Close any extra file descriptors.  Note that there may still be
     descriptors left by system functions.  They will be closed later. */

  /* Close any extra open file descriptors so that we don't have them
     hanging around in clients. */
}






























