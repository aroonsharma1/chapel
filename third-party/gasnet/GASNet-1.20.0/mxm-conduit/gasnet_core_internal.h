/*
 * Description: GASNet MXM conduit header for internal definitions in Core API
 * Copyright (c)  2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright (c)  2012, Mellanox Technologies LTD. All rights reserved.
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
    gasnetc_Short=0,
    gasnetc_Medium=1,
    gasnetc_Long=2,
    gasnetc_System=3
} gasnetc_category_t;

static const char * am_category_str[] = {
    [gasnetc_Short]  = "Short",
    [gasnetc_Medium] = "Medium",
    [gasnetc_Long]   = "Long",
    [gasnetc_System] = "System"
};

/* Message types for System category */
enum {
    SYSTEM_MSG_NA        = 0,
    SYSTEM_MSG_EXIT_FLOW = 1,
    SYSTEM_MSG_REG_REQ   = 2,
    SYSTEM_MSG_REG_REP   = 3
};

/* -------------------------------------------------------------------------- */

/* check (even in optimized build) for MXM errors */
#define GASNETC_MXM_CHECK(rc,msg) \
    if_pf ((rc) != 0) { \
        gasneti_fatalerror("Unexpected error %s (rc=%d errno=%d) %s", \
                            strerror(errno),(rc), errno,(msg));       \
    }

#define GASNETC_MXM_CHECK_PTR(ptr,msg) GASNETC_MXM_CHECK((ptr)==NULL,(msg))

#if GASNET_DEBUG_VERBOSE
#define MXM_LOG(fmt, ...) do { \
                printf("[I] [node %d] %s(): " fmt, \
                       gasneti_mynode, __FUNCTION__, ## __VA_ARGS__); \
                fflush(stdout); fflush(stderr); \
        } while (0)
#else
#define MXM_LOG(fmt, ...)
#endif

#if GASNET_DEBUG_VERBOSE
#define MXM_DEBUG(fmt, ...) do { \
                printf("[D] [node %d] %s(): " fmt, \
                       gasneti_mynode, __FUNCTION__, ## __VA_ARGS__); \
                fflush(stdout); fflush(stderr); \
        } while (0)
#else
#define MXM_DEBUG(fmt, ...)
#endif

#define GASNET_DEBUG_EXIT_FLOW 0

#if GASNET_DEBUG_EXIT_FLOW
#define MXM_DEBUG_EXIT_FLOW MXM_LOG
#else
#define MXM_DEBUG_EXIT_FLOW(fmt, ...)
#endif



#define MXM_WARN(fmt, ...) do { \
                printf("[WARNING] [node %d] %s(): " fmt, \
                       gasneti_mynode, __FUNCTION__, ## __VA_ARGS__); \
                fflush(stdout); fflush(stderr); \
        } while (0)

#define MXM_ERROR(fmt, ...) do { \
                printf("[ERROR] [node %d] %s(): " fmt, \
                       gasneti_mynode, __FUNCTION__, ## __VA_ARGS__); \
                fflush(stdout); fflush(stderr); \
        } while (0)

#if HAVE_SSH_SPAWNER
#include <ssh-spawner/gasnet_bootstrap_internal.h>
#endif
#if HAVE_MPI_SPAWNER
#include <mpi-spawner/gasnet_bootstrap_internal.h>
#endif

#include <mxm/api/mxm_api.h>
#include <mxm/api/mxm_addr.h>
#if HAVE_MMAP
#include <sys/mman.h> /* For MAP_FAILED */
#endif

#ifdef GASNETI_USE_ALLOCA
/* Keep defined */
#elif !PLATFORM_COMPILER_PGI
#define GASNETI_USE_ALLOCA 1
#endif

#ifndef MXM_VERSION
#define MXM_VERSION(major, minor) (((major)<<MXM_MAJOR_BIT) | ((minor)<<MXM_MINOR_BIT))
#endif

extern uintptr_t gasnetc_max_msg_sz;

/* Description of a pre-pinned memory region */
typedef struct {
    uintptr_t  addr;
    size_t     len;
    uintptr_t  end;    /* inclusive */
#if MXM_API < MXM_VERSION(1,5)
    uint32_t   lkey;   /* used for local access by HCA */
    uint32_t   rkey;   /* used for remote access by HCA */
#else
    mxm_mem_h  memh;
#endif
} gasnetc_memreg_t;

extern int gasnetc_connect_init(void);
extern int gasnetc_connect_fini(void);

/*
 * Routines in gasnet_core_sndrcv.c
 */
extern int gasnetc_RequestGeneric(gasnetc_category_t category,
                                  int dest, gasnet_handler_t handler,
                                  void *src_addr, int nbytes, void *dst_addr,
                                  uint8_t is_sync,
                                  int numargs, va_list argptr);
extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
                                gasnet_token_t token, gasnet_handler_t handler,
                                void *src_addr, int nbytes, void *dst_addr,
                                int numargs, va_list argptr);

#define gasnetc_unmap(reg)      gasneti_munmap((void *)((reg)->addr), (reg)->len)

#define GASNETC_MSG_HANDLERID(num)   ( (gasnet_handler_t)( (((uint32_t)(num)) >>  8) & ((uint32_t)0xFF)) )
#define GASNETC_MSG_NUMARGS(num)     ( (uint8_t)( (((uint32_t)(num)) >> 16) & ((uint32_t)0xFF)) )
#define GASNETC_MSG_NUMBER(num)      ( (uint8_t)( (((uint32_t)(num)) >> 24) & ((uint32_t)0xFF)) )
#define GASNETC_MSG_CATEGORY(num)    ( (gasnetc_category_t)((uint32_t)(num) & 3) )
#define GASNETC_MSG_ISREQUEST(num)   ( (uint8_t)(((uint32_t)(num) >> 2 ) & 1) )
#define GASNETC_MSG_ISREPLY(num)     (!GASNETC_MSG_ISREQUEST(num))
#define GASNETC_MSG_ISSYNC(num)      ( (uint8_t)(((uint32_t)(num) >> 3 ) & 1) )
#define GASNETC_MSG_TYPE_SYS(num)    ( (uint8_t)(((uint32_t)(num) >> 4 ) & 0xF) )

/*
 * Encoding meta-data in 4 bytes:
 *    buf[0]   : flags that are encoded as follows:
 *               bits [1..0]: message category
 *               bit  [2]   : boolean "is request" flag
 *               bit  [3]   : boolean "is synchronous request" flag
 *               bits [7..4]: message type for system messages
 *    buf[1]   : handler id
 *    buf[2]   : number of args for AM handler
 *    buf[3]   : message number
 */
#define GASNETC_MSG_METADATA(hand_id, numargs, category, is_request, is_sync_request, sys_msg_type, msgnum) \
      ( (uint32_t)( /* byte 0: category, is_request, is_sync, sys msg type */ \
                  ( (uint32_t)(((uint8_t )(category & 3))                 | \
                    ((is_request     ) ? (((uint8_t)1) << 2) : 0)         | \
                    ((is_sync_request) ? (((uint8_t)1) << 3) : 0)         | \
                    ((uint8_t)(sys_msg_type) & 0xF) << 4) )               | \
                  /* byte 1: handler ID */                                  \
                  ( ((uint32_t)((uint8_t)(hand_id)))      <<  8 )         | \
                  /* byte 2: num of args for handler */                     \
                  ( ((uint32_t)((uint8_t)(numargs)))      << 16 )         | \
                  /* byte 3: msg number */                                  \
                  ( ((uint32_t)((uint8_t)(msgnum)))       << 24 ) ) )

/*
 * Bootstrap support
 */
extern void (*gasneti_bootstrapFini_p)(void);
extern void (*gasneti_bootstrapAbort_p)(int exitcode);
extern void (*gasneti_bootstrapBarrier_p)(void);
extern void (*gasneti_bootstrapExchange_p)(void *src, size_t len, void *dest);
extern void (*gasneti_bootstrapAlltoall_p)(void *src, size_t len, void *dest);
extern void (*gasneti_bootstrapBroadcast_p)(void *src, size_t len, void *dest, int rootnode);
#define gasneti_bootstrapFini           (*gasneti_bootstrapFini_p)
#define gasneti_bootstrapAbort          (*gasneti_bootstrapAbort_p)
#define gasneti_bootstrapBarrier        (*gasneti_bootstrapBarrier_p)
#define gasneti_bootstrapExchange       (*gasneti_bootstrapExchange_p)
#define gasneti_bootstrapAlltoall       (*gasneti_bootstrapAlltoall_p)
#define gasneti_bootstrapBroadcast      (*gasneti_bootstrapBroadcast_p)

typedef struct _gasnet_mxm_ep_conn_info {
    struct sockaddr_storage  ptl_addr[MXM_PTL_LAST];
} gasnet_mxm_ep_conn_info_t;

typedef struct gasnetc_mkey_ {
    uint32_t lkey;
    uint32_t rkey;
} gasnetc_mkey_t;

typedef struct _gasnet_mxm_module {
    mxm_h                        mxm_context;
    mxm_ep_h                     mxm_ep;
    mxm_conn_h                 * connections;
    gasnet_mxm_ep_conn_info_t  * remote_eps;
    uint8_t                    * need_fence;
    uint8_t                      strict_api;
    mxm_mq_h                     mxm_mq;
    size_t                       rndv_thresh; /* MXM threshold for using rndv */
    size_t                       zcopy_thresh;/* MXM threshold for using zcopy*/
    size_t                       max_am_med;  /* max size for medium AM */
    gasnetc_memreg_t           * reg;         /* registered segments */
    mxm_recv_req_t               recv_req;    /* single recv request */
    gasnetc_memreg_t             recv_reg;    /* buffer for receive request data */
} gasnet_mxm_module_t;

typedef struct _gasnetc_am_token {
    gasnet_node_t src_node;
    uint8_t is_request;
    uint8_t is_sync_request;
    uint8_t msg_num;
} gasnetc_am_token_t;

#endif
