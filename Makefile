TOP=.
include $(TOP)/configure/CONFIG

PROD_HOST_DEFAULT = fwdCliS

PROD_LIBS = Com

fwdCliS_SRCS = fwd_server.c 
fwdCliS_SRCS += fwd_child.c 
fwdCliS_SRCS += fwd_err.c

include $(TOP)/configure/RULES
include $(TOP)/configure/RULES_TOP
