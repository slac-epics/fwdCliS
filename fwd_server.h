/*
        **CMS**=none
==============================================================================

  Abs:  Forward Server (proxy) headers and related structures, prototypes

  Name: fwd_server.h

  Rem:  Designed for compiling on SunOS for VxWorks (then modified to 
        compile on Linux using gnu tools to run under Linux RH5.2

  Auth: 20-mar-1997, Mark Crane (MARK):
  Rev:  dd-mmm-19yy, Tom Slick (TXS):

------------------------------------------------------------------------------

  Mod:
        12-Jun-2023, Jesse Bellister (JESSEB)
          Update for 64 bit upgrade (unsigned long -> uint32_t)        
        11-Feb-1999, Mark Crane (MARK)
           a little cleanup
        12-Jan-1999, Mark Crane (MARK)
           remove args from fwd_shm_init and add pid to fqd_shm_alloc,
           add fwd_ptr_table_ts
        12-Nov-1998, Mark Crane (MARK)
           add fwd_cache_tas for Linux port
        09-Sep-1998, Mark Crane (MARK)
           change number of buffers from 60 to 80 (was 30 before)
        03-Sep-1998, Mark Crane (MARK)
           add fwd_dog prototypes
        19-Aug-1998, Mark Crane (MARK)
           fix up PX_*_FUNC defines and add enable/disable commands
        29-Sep-1998, Mark Crane (MARK)
           add stat_ts to cache_ts and cleaned up things

==============================================================================*/
#ifndef FWD_SERVER_H
#define FWD_SERVER_H

#include <stdint.h>

/*
 * How about the nice SLC typedefs here while we are at it.  Remove these
 * if this header ever moves closer to VMS
 */
typedef short             int2;           /* An I*2 */
typedef unsigned short    int2u;          /* An unsigned I*2 */
typedef long int          int4;           /* An I*4 */
typedef uint32_t          int4u;          /* An unsigned I*4 */
typedef unsigned char     int1u;          /* For unsigned byte arithmetic */
typedef unsigned char     charu;          /* For unsigned byte arithmetic */
typedef int4u             vmsstat_t;       /* VMS type status stays 32 bits */

#ifndef TRUE
#define TRUE 1                      /* since it's not done elsewhere */
#endif

#ifndef FALSE
#define FALSE -1                    /* since it's not done elsewhere */
#endif

#ifndef ERROR
#define ERROR -1                    /* since it's not done elsewhere */
#endif

#define TEMP_CRC 0x55               /* temp value until code is there */

/*
 * error reporting levels.  Setting a one enables the associated level
 */
#define LOG_NONE    (1)
#define LOG_FATAL   (1)
#define LOG_ERROR   (1)
#define LOG_INFO    (1)  /* RONM TURNED THEM ON FROM HERE OUT */
#define LOG_DEBUG   (1)

#define LOG_ALL     (1)

#define FWD_SERVER_PORT 6060        /* TCP port of server */
#define FWD_SERVER_IP  0x864F31C9   /* cdvw2 */

#define FWD_SERVER2_PORT 6061        /* TCP port of server */
#define FWD_SERVER2_IP  0x864F31C9   /* cdvw2 */

#define MAX_CACHE_ELEMENTS 500      /* 200 VMS processes + 100*3 micro conns*/
#define MAX_MESSAGE_SIZE  (8192)
/* look further down for MAX_SHM_MESSAGE_SIZE since it needs a fwd_header */

#define PIPE_NAME_SIZE    (20) /* string length */

/*
 * This is needed for external programs to map to the shared memory
 *
 * For some reason I can't make anything bigger than 1Meg (where 4 meg should
 * be the current max in RD52 Linux.  error = FWDD:: Invalid argument
 */
#define FWD_SHM_SIZE 1000000 /* shared memory, about 1 megabyte for now */

/*
 * These are used in the cache_lookup code to determin what the input value is
 */
#define USE_IPPORT 0
#define USE_ALIAS  1
#define USE_PORT   2

/*
 * These are used as the possible states of a cache entry
 */
#define CLOSED     0
#define CLOSING    1
#define OPEN       2

/*
 * Function codes placed in the forward header telling the proxy server
 * what to do with the message.  One example is the PX_KEEPALIVE_FUNC.
 * This implements an application level ping to the proxy server.
 */
#define PX_FORWARD_FUNC         1  /* Forward using IP/PORT values           */
#define PX_KEEPALIVE_FUNC       2  /* Ask proxy server to return an echo     */
#define PX_REGISTER_FUNC        3  /* Register this name with the proxy      */
#define PX_FORWARD_ALIAS_FUNC   4  /* Forward using the alias name           */
#define PX_REGISTER_PORT_FUNC   5  /* Register this port with the proxy      */
#define PX_DISABLE_CONN_FUNC    6  /* Disable all connections sans this one  */
#define PX_ENABLE_CONN_FUNC     7  /* Enable data flow on all connections    */
#define PX_FORWARD_RECEIPT_FUNC 8  /* Forward  using receipt functions       */

#define PX_FUNC_MASK 0x3F               /* hide the receipt bits */
#define PX_REQUEST_PROXY_RECEIPT  0x40  /* Proxy server returns receipt Bit */
#define PX_REQUEST_APP_RECEIPT    0x80  /* Application returns receipt Bit  */


/*
 * The forward server header structure as used everywhere
 *
 * Just like REF_:[C_INC]MSGHEAD.HC, well almost.  The SLAC macros are not
 * here so this is more specific for VxWorks
 */
typedef struct 
{
   unsigned short ip;   /* IP address in hex subnet.node format */
                        /* destination address when sent to FWD server */
                        /* source address when sent out of FWD server */
   unsigned short port; /* TCP port address */
                        /* used as destination port when sent to FWD server */
                        /* used as source port when sent out of FWD server */
} ip_port_ts;

typedef union 
{
  ip_port_ts      s;    /* structure access */
  uint32_t        word; /* long word access */
} ip_port_tu;

typedef struct
{
  ip_port_tu      ip_port_u; /* ip and port values */
  uint32_t        len;       /* stream length max minus sizeof(this header) */
  unsigned short  user;      /* user defined could be chunk count for buffs */
  unsigned char   cmd;       /* fwd_server application command, like ping */
  unsigned char   crc;       /* ones complement checksum over the header */ 
			     /* also delimits the TCP stream */
} fwd_hdr_ts;

/*
 * Setup here after the fwd_hdr_ts has been done
 */
#define MAX_SHM_MESSAGE_SIZE MAX_MESSAGE_SIZE + sizeof(fwd_hdr_ts)

/*
 * Structure to keep statistics about individual children
 */
typedef struct
{
  int enable;           /* is this structure in use? */
  int err_cnt;          /* total error count */
  int sd_out;           /* packets out */
  int sd_in;            /* packets in */
  int pipe_redo;        /* how many times to jump to redo this pipe */
  int qos_delay;        /* total count of qos delays, usefull? */
  int thru_time;        /* max time to do a packet */
  int max_pipe_size;    /* max number of pipe entries ever used */
  int chksum_err;       /* used to track number of recovered chksum ers */
  int keepalive;        /* count of keepalives processed */
  int fwd_ip;           /* count of forwardings done using IP/port info */
  int fwd_alias;        /* count of forwardings done usign names */
  int rx_l2k;           /* rx packet size < 2000 bytes */
  int rx_g2k;           /* rx packet size > 2000 bytes */
  int tx_l2k;           /* tx packet size < 2000 bytes */
  int tx_g2k;           /* tx packet size > 2000 bytes */
} stat_ts;

/*
 * All the data one might need to track destinations out of the proxy 
 */
typedef struct
{
  int           qos;            /* quality of service, used for bw limiting */
  int           state;          /* how is this entry used? */
  ip_port_tu    real_ip_port_u; /* real ip and port values  */
  ip_port_tu    tag_ip_port_u;  /* real or smashed ip and port values */
  uint32_t      alias;          /* user hash value, usually SLC id chars */
  int           sock_fd;        /* socket descriptor of connection to peer */ 
  int           unix_fd;        /* file desc of Unix socket to child */
  char          unix_name[PIPE_NAME_SIZE];
  int           pid;            /* Process ID for this entry */
  stat_ts       stat_s;         /* statistics structure */
} fwd_cache_ts;

/*
 * We usually store many fwd_cache_ts so let's make a typedef array for future
 * use 
 */
typedef fwd_cache_ts  fwd_cache_tas[MAX_CACHE_ELEMENTS];

/*
 * Structure to put data into the pipe que.  Provides enough data to track
 * buffer locations, destination for the buffer and a time stamp.
 */
typedef struct
{
  char          *buff_p;       /* pointer to buffer with header + data */
  ip_port_tu     ip_port_u;    /* where this packet came from */
  uint32_t       time;         /* store tick count when packet came in */
} pipe_ts;


/*
 * Structure to keep statistics about the whole proxy
 */
typedef struct
{
  int   num_children;   /* total number of children to report about */
  int   mem_left;       /* amount of memory left */
  int   mem_peak;       /* smallest amount of memory left */
  int   cpu_util;       /* max CPU used */
  int4u boot_ticks;     /* kernel tick when forward server actually starts */
  int   sd_in;          /* total packets in */
  int   sd_out;         /* total packets out */
  int   max_store;      /* maximum time a packet has been stored for */
  int   avg_store;      /* average timer a packet is stored for */
} global_stat_ts;

/*
 * Structure to keep statistics about the shared memory system. 
 * Keep some status counters in the global section
 */
typedef struct
{
  int   alloc;           /* total allocations */
  int   bad_alloc;       /* total bad allocations */
  int   free;            /* total frees */
  int   deleted;         /* number of segments deleted (janitor task ?) */
  int   free_big;        /* number of big sized free buffers */
  int   free_med;        /* number of medium sized free buffers */
  int   free_small;      /* number of small sized free buffers */
  int   bad_free_big;    /* number of big sized un-freed buffers */
  int   bad_free_med;    /* number of medium sized un-freed buffers */
  int   bad_free_small;  /* number of small sized un-freed buffers */
} shm_stat_ts;


/*
 * Structure to keep pointers to places in the shared memory segments
 */
typedef struct
{
  char *shm3_top_p;
  char *shm3_base_p;
  char *shm2_top_p;
  char *shm2_base_p;
  char *big_buf_base_p; /* which ends up being shm2_base_p */
  char *shm1_top_p;
  char *med_buf_base_p;
  char *small_buf_base_p;
  char *shm1_base_p;

  int   last_used_index;   /* last index value */
  int   big_buf_index;     /* med_buf_index + MAX_MED_BUF */
  int   med_buf_index;     /* small_buf_index + MAX_SMALL_BUF */
  int   small_buf_index;   /* should be zero */

  int   shmid1;  /* keep the shared memory IDs for the kill */
  int   shmid2;
  int   shmid3;
} shm_layout_ts;

/*
 * Describe what is in a buffer
 */
typedef struct
{
  int   used;
  int   pid;          /* sense of onership for deletion later */
  int   buffer_size;
  int   delete_cnt;   /* flag to aid in aging these packets */ 
  char *buf_addr_p;
} buf_info_ts;

/*
 * Define sizes for the different types of buffers
 */
#define SMALL_BUF  128   /* command type packets and small responses */
#define MED_BUF    2500  /* median sized packet (DBEX traffic */
#define BIG_BUF    MAX_MESSAGE_SIZE + sizeof(fwd_hdr_ts)
#define HUGE       BIG_BUF + 1 /* larger than BIG_BUF but less than 64K */

#define NUM_BIG_SEG 2        /* Two full segments of big buffers */
#define MAX_BIG_BUF_PER_SEG  FWD_SHM_SIZE/BIG_BUF /* a whole segment */
#define MAX_BIG_BUF   MAX_BIG_BUF_PER_SEG * NUM_BIG_SEG
#define MAX_MED_BUF   200
#define MAX_SMALL_BUF 200
#define MAX_SHM_BUF   MAX_SMALL_BUF+MAX_MED_BUF+MAX_BIG_BUF

/*
 * Make an easy way to address the pointer array
 */
typedef buf_info_ts buf_info_tas[MAX_SHM_BUF];

/*
 * Structure to keep a copy of all the important global pointers to share.
 * This structure ends up being the first thing in the first shared memory
 * segment so other processes can attach to memory and find these pointers.
 * They are filled in during the main server thread setup. The real purpose
 * for this structure is to allow external processes access to the shared
 * memory, mostly to provide status information.
 */
typedef struct
{
  char               version_info[128];   /* Place to put the Software ID */
  fwd_cache_tas     *fwd_cache_pas;       /* cache info */
  global_stat_ts    *global_stat_ps;      /* Fwd server statistics */
  shm_stat_ts       *shm_stat_ps;         /* shared memory statistics */
  buf_info_tas      *shm_buf_info_pas;    /* actual buffer usage stats */
  shm_layout_ts     *shm_layout_ps;       /* helper info */
} fwd_ptr_table_ts;

/*
 * Error Codes for VMS
 */
#define MSG_PX_EXNET_EOF                    (0x081C871A)
#define MSG_PX_SEND_FAILED                  (0x081C8712)
#define MSG_PX_RECV_RESET                   (0x081C870A)
#define MSG_PX_RECV_FAILED                  (0x081C8702)
#define MSG_PX_TIMEOUT                      (0x081C86FA)
#define MSG_PX_SELECT_FAILED                (0x081C86F2)
#define MSG_PX_CONNECT_FAILED               (0x081C86EA)
#define MSG_PX_SOCKET_CREATE_FAILED         (0x081C86E2)
#define MSG_PX_CHILD_FORK_ERROR             (0x081C86DA)
#define MSG_PX_NO_RECORD_FOUND              (0x081C86D2)
#define MSG_PX_TOO_MANY_BUFFERS             (0x081C86CA)
#define MSG_PX_NO_MEMORY                    (0x081C86C2)
#define MSG_PX_REGISTER_FAILED              (0x081C86BA)
#define MSG_PX_RX_SHORT                     (0x081C86B2)
#define MSG_PX_RX_TOOBIG                    (0x081C86AA)
#define MSG_PX_BAD_CRC                      (0x081C86A2)
#define MSG_PX_FWD_HRD_ERR                  (0x081C869A)
#define MSG_PX_NGNG                         (0x081C8692)
#define MSG_PX_DIAG_INFO                    (0x081C865B)
#define MSG_PX_CHILD_DEATH                  (0x081C8653)
#define MSG_PX_CHILD_REGISTRATION           (0x081C864B)
#define MSG_PX_CHILD_BIRTH                  (0x081C8643)

/*
 * Protos
 */
int        fwd_server (void);
int        fwd_child(int sock_fd, int pipe_fd, int ip, int port, int qos);
static int tcp_get(int sock_fd, char* buff, int request_cnt, int *actual_cnt);
static int fwd_child_death (ip_port_tu real_ip_port_u);
int        fwd_chksum_check (fwd_hdr_ts *fwd_hdr_ps);
unsigned char fwd_chksum_make (fwd_hdr_ts *fwd_hdr_ps);

int  fwd_cache_init (int num_elements);
int  fwd_cache_kill (void);
int  fwd_cache_add (fwd_cache_ts* cache_ps);
int  fwd_cache_delete (fwd_cache_ts* cache_ps);
int  fwd_cache_register_alias (ip_port_tu ip_port_u, unsigned long alias);
int  fwd_cache_register_port (ip_port_tu ip_port_u, ip_port_tu port_u);
int  fwd_cache_lookup (ip_port_tu lookup_key, unsigned long lookup_flag,
                       fwd_cache_ts **cache_ps);
int  fwd_cache_close (ip_port_tu lookup_key_u, unsigned long lookup_flag);
int  fwd_cache_close_sd (int sock_fd, fwd_cache_ts* cache_ps);
int  fwd_cache_free_sd (int sock_fd);
void fwd_cache_dump (void);

int  fwd_err (char *err_msg_buf);

int  fwd_dog_task (void);
int  fwd_dog_init (void);
int  fwd_dog_poke (void);
int  fwd_dog_kill (void);

int  fwd_shm_init (void);
int  fwd_shm_alloc (int size, int pid, char **buff_pp);
int  fwd_shm_free (char *buff_p);
int  fwd_shm_kill (void);
void fwd_shm_check (void);
int  fwd_shm_cleanup (int pid);

/*
 * Old protos kept here for the production compiles.  Remove these!
 */
int        fwd_crc_make (fwd_hdr_ts* fwd_hdr_ps);
int        fwd_crc_check (fwd_hdr_ts* fwd_hdr_ps);


#endif /* guard */








