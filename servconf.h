/*

servconf.h

Author: Tatu Ylonen <ylo@cs.hut.fi>

Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
                   All rights reserved

Created: Mon Aug 21 15:35:03 1995 ylo

Definitions for server configuration data and for the functions reading it.

*/

/*
 * $Id: servconf.h,v 1.1.1.1 2007/10/26 23:35:34 ronm Exp $
 * $Log: servconf.h,v $
 * Revision 1.1.1.1  2007/10/26 23:35:34  ronm
 * Initial Release
 *
 */

#ifndef SERVCONF_H
#define SERVCONF_H

#define MAX_ALLOW_HOSTS		256 /* Max # hosts on allow list. */
#define MAX_DENY_HOSTS		256 /* Max # hosts on deny list. */

typedef struct
{
  int port;			/* Port number to listen on. */
  struct in_addr listen_addr;	/* Address on which the server listens. */
  char *pid_file;		/* File containing process ID number. */
  int login_grace_time;		/* Disconnect if no auth in this time (sec).*/
  int quiet_mode;		/* If true, don't log anything but fatals. */
  int fascist_logging;		/* Perform very verbose logging. */
  int print_motd;		/* If true, print /etc/motd. */
  int keepalives;		/* If true, set SO_KEEPALIVE. */
  time_t idle_timeout;		/* If non zero, sets idle-timeout */
  int umask;			/* Umask */
  unsigned int num_allow_hosts;
  char *allow_hosts[MAX_ALLOW_HOSTS];
  unsigned int num_deny_hosts;
  char *deny_hosts[MAX_DENY_HOSTS];

} ServerOptions;

/* Initializes the server options to special values that indicate that they
   have not yet been set. */
void initialize_server_options(ServerOptions *options);

/* Reads the server configuration file.  This only sets the values for those
   options that have the special value indicating they have not been set. */
void read_server_config(ServerOptions *options, const char *filename);

/* Sets values for those values that have not yet been set. */
void fill_default_server_options(ServerOptions *options);

#endif /* SERVCONF_H */
