########################################################################
#
# Makefile for the FWD Server.
# It builds against iocLogAndFwdServer and cmlog too.
#
# ----------------------------------------------------------------------
#
# Mod:
#
#
########################################################################
# defines
########################################################################
#
#
CCOPTIONS = -g -DLinux -fpic -D_CMLOG_BUILD_CLIENT -c 
CINCLUDES = -I. -I/afs/slac/g/lcls/cmlog/include
#
#  CCOPTIONS = -DSYSV -DSVR4 -Dsolaris -g -D_CMLOG_BUILD_CLIENT -c -o
#  CINCLUDES = -I. -I/afs/slac.stanford.edu/package/cmlog/prod/include
#
CFLAGS = $(CCOPTIONS) $(CINCLUDES)
#
# CMLOG application LD flags
#
#CM_LDFLAGS = -L /afs/slac/g/lcls/cmlog/lib/Linux-i386 -lcmlog -lcmlogb -ldata -ldb  -lnsl -lc 
#
EPICS_LDFLAGS = -L/afs/slac/g/lcls/epics/base/base-R3-14-8-2-lcls6/lib/linux-x86 -lCom
#
#CM_LDFLAGS = /afs/slac.stanford.edu/package/cmlog/prod/lib/solaris/libcmlog.a -L/afs/slac.stanford.edu/package/cmlog/prod/lib/solaris -lcmlogb -ldata -ldb  -lsocket -lnsl -lc 
#
# -lxnet removed!
#
# CC = cc $(CFLAGS)
# LD = CC  $(LDFLAGS)
CC = gcc $(CFLAGS)
LD = g++ $(LDFLAGS)
#
TARGETS = all clean
#
$(TARGETS): phony
phony:
#
########################################################################
# Macros
########################################################################
#
# this is the list of non-system includes 
#
FWD_SERVER_INCLUDES = fwd_server.h servconf.h
EPICS_INCLUDES = -I/afs/slac/g/lcls/epics/base/base-R3-14-8-2-lcls6/include -I/afs/slac/g/lcls/epics/base/base-R3-14-8-2-lcls6/include/os/Linux
#
########################################################################
# top level build
########################################################################
#
# the code
# -----------
#
all:  fwd_server

fwd_server:  fwd_server.o fwd_child.o fwd_err.o
	$(LD) -o fwdCliS fwd_server.o fwd_child.o fwd_err.o $(CM_LDFLAGS) $(EPICS_LDFLAGS)
	# JROCK COMMENT OUT cp -fp fwdCliS ../bin/linux-x86

  fwd_server.o: fwd_server.c $(FWD_SERVER_INCLUDES)
	$(CC) -o fwd_server.o $(EPICS_INCLUDES) fwd_server.c
  fwd_child.o: fwd_child.c  $(FWD_SERVER_INCLUDES)
	$(CC) -o fwd_child.o $(EPICS_INCLUDES) fwd_child.c
  fwd_err.o: fwd_err.c  $(FWD_SERVER_INCLUDES)
	$(CC) -o fwd_err.o $(EPICS_INCLUDES) fwd_err.c
#
#
########################################################################
# Install as root:
########################################################################
install: phony
	touch ./old_fwd_server
#
########################################################################
# Clean:
########################################################################
clean: phony
	rm -f fwdCliS
	rm -f *.o
	rm -f *.*~
	rm -f *~

########################################################################
# when the make is begun:
########################################################################
#
# At the start we want to do nothing!
#
.INIT:
#
#
########################################################################
# when make is finished:
########################################################################
#
# At the end we want to clean up 
#
.DONE:
#
########################################################################
# end
########################################################################









