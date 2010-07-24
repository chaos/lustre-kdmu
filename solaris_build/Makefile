CC = /usr/sfw/bin/gcc
LD = /usr/ccs/bin/ld
AR = /usr/bin/gar cru
RM = /usr/bin/rm
CTFCONVERT = /opt/onbld/bin/i386/ctfconvert
CTFMERGE = /opt/onbld/bin/i386/ctfmerge

archstr = i86pc

LUSTRE_UCPPFLAGS = -D__arch_lib__ -D_LARGEFILE64_SOURCE=1 -DLUSTRE_UTILS=1
LUSTRE_UCPPFLAGS += -D_GNU_SOURCE
LUSTRE_UCPPFLAGS += -DHAVE_PTHREAD=1 -DHAVE_NETDB_H=1
LUSTRE_UCPPFLAGS += -DHAVE_UNISTD_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_STDINT_H=1
LUSTRE_UCPPFLAGS += -DHAVE_SYS_IOCTL_H=1 -DHAVE_GETHOSTBYNAME=1
LUSTRE_UCPPFLAGS += -DHAVE_LIBREADLINE=1
LUSTRE_UCPPFLAGS +=  -D__WORDSIZE=64 -DPTHREAD_STACK_MIN=16384

LUSTRE_KCPPFLAGS = -D__KERNEL__ -DBITS_PER_LONG=64

LUSTRE_CPPFLAGS += -DCDEBUG_ENABLED -DWITH_WATCHDOG
LUSTRE_CPPFLAGS += -DLNET_MAX_PAYLOAD=LNET_MTU
LUSTRE_CPPFLAGS += -DCPU_AFFINITY=1 -DCONFIG_SMP=1
LUSTRE_CPPFLAGS += -DCONFIG_FS_POSIX_ACL=1 -DCONFIG_X86_64=1
LUSTRE_CPPFLAGS += -DENABLE_PINGER=1 -DHAVE_LRU_RESIZE_SUPPORT=1
LUSTRE_CPPFLAGS += -DENABLE_CHECKSUM=1 -DDMU_OSD=1 -DLIBCFS_DEBUG=1
LUSTRE_CPPFLAGS += -DUSE_LU_REF=1

# To use adler32 we need to fix solaris common/zmod/mapfile
# file to treat z_adler32 as one of the global functions.
# LUSTRE_CPPFLAGS += -DHAVE_ADLER=1

LUSTRE_CPPFLAGS += -DENABLE_LIBLUSTRE_RECOVERY=1
LUSTRE_CPPFLAGS += -DLIBLUSTRE_POSIX_ACL=1
LUSTRE_CPPFLAGS += -DOBD_MAX_IOCTL_BUFFER=8192

# if !defined(SOLARIS_LSERVER) test may be used to exclude lustre code we
# may still need later when we port the client to Solaris. Lustre code not
# needed by either Solaris client or server is excluded with
# !if defined(__sun__) test.

LUSTRE_CPPFLAGS += -DSOLARIS_LSERVER

SOLARIS_KCPPFLAGS = -D_KERNEL -D_MACHDEP

ONNV_TREE = ./onnv_125_h

SOLARIS_INCL_DIRS = -I$(ONNV_TREE)/usr/src/uts/common

SOLARIS_UINCL_DIRS = -I$(ONNV_TREE)/usr/src/head

SOLARIS_KINCL_DIRS = -I$(ONNV_TREE)/usr/src/uts/intel      \
                     -I$(ONNV_TREE)/usr/src/uts/$(archstr)

LUSTRE_INCL_DIRS  = -I$(TOP)/libcfs/include \
                    -I$(TOP)/lnet/include   \
                    -I$(TOP)/lustre/include/solaris/versions \
                    -I$(TOP)/lustre/include

KINCL_DIRS = -nostdinc                                                   \
             -isystem /usr/sfw/lib/gcc/i386-pc-solaris2.11/3.4.3/include \
             $(SOLARIS_INCL_DIRS) $(SOLARIS_KINCL_DIRS) $(LUSTRE_INCL_DIRS)

KCPPFLAGS = $(KINCL_DIRS) $(LUSTRE_KCPPFLAGS) $(LUSTRE_CPPFLAGS) \
            $(SOLARIS_KCPPFLAGS)

UCPPFLAGS = $(SOLARIS_UINCL_DIRS) $(SOLARIS_INCL_DIRS) $(LUSTRE_INCL_DIRS) \
            $(LUSTRE_UCPPFLAGS) $(LUSTRE_CPPFLAGS)

UCFLAGS = -m64 -g -fPIC -Werror
PTHREAD_UCFLAGS = -pthreads

KCFLAGS = -ffreestanding -nodefaultlibs -m64 -mcmodel=kernel \
          -mno-red-zone
KCFLAGS += -Wall \
           -Wno-sign-compare \
           -Wno-unknown-pragmas \
           -Wno-unused \
           -Wno-format \
           -Wno-missing-braces \
           -Werror
KCFLAGS += -gdwarf-2 -fno-dwarf2-indirect-strings

CTFLBL_FLAGS = -l LUSTREFS_LBL

TOP = ..

LIBCFS_UTIL_PATH=libcfs/libcfs/util
LNET_UTILS_PATH=lnet/utils
LUSTRE_UTILS_PATH=lustre/utils
LUSTRE_SOLARIS_UTILS_PATH=lustre/utils/solaris

GENERIC_LIBCFS_PATH=libcfs/libcfs
SOLARIS_LIBCFS_PATH=libcfs/libcfs/solaris

LNET_PATH=lnet/lnet
KSOCKLND_PATH=lnet/klnds/socklnd
SELFTEST_PATH=lnet/selftest

PTLRPC_PATH=lustre/ptlrpc
LDLM_PATH=lustre/ldlm
OBDCLASS_PATH=lustre/obdclass
MISC_PATH=lustre/lvfs
OST_PATH=lustre/ost
ECHO_PATH=lustre/obdecho

LOADGEN_SRCS        = $(LUSTRE_UTILS_PATH)/loadgen.c \
                      $(LUSTRE_UTILS_PATH)/lustre_cfg.c \
                      $(LUSTRE_UTILS_PATH)/obd.c

#LIBLUSTREAPI_A_SRCS = $(LUSTRE_UTILS_PATH)/liblustreapi.c

LIBLUSTREAPI_A_SRCS = $(GENERIC_LIBCFS_PATH)/kernel_user_comm.c

LIBPTLCTL_A_SRCS    = $(LNET_UTILS_PATH)/debug.c \
                      $(LNET_UTILS_PATH)/portals.c

LIBCFSUTIL_A_SRCS   = $(GENERIC_LIBCFS_PATH)/nidstrings.c \
                      $(LIBCFS_UTIL_PATH)/parser.c \
                      $(LIBCFS_UTIL_PATH)/util.c \
                      $(LIBCFS_UTIL_PATH)/params_tree_util.c

LIB_LAPI_CFSUTIL_CMN_SRCS = $(LIBCFS_UTIL_PATH)/l_ioctl.c

CFSD_SRCS           = $(LUSTRE_SOLARIS_UTILS_PATH)/cfsd.c

GENERIC_LIBCFS_SRCS = $(GENERIC_LIBCFS_PATH)/module.c \
                      $(GENERIC_LIBCFS_PATH)/debug.c \
                      $(GENERIC_LIBCFS_PATH)/tracefile.c \
                      $(GENERIC_LIBCFS_PATH)/libcfs_string.c \
                      $(GENERIC_LIBCFS_PATH)/prng.c \
                      $(GENERIC_LIBCFS_PATH)/errno.c \
                      $(GENERIC_LIBCFS_PATH)/lwt.c \
                      $(GENERIC_LIBCFS_PATH)/nidstrings.c \
                      $(GENERIC_LIBCFS_PATH)/watchdog.c \
                      $(GENERIC_LIBCFS_PATH)/params_tree.c \
                      $(GENERIC_LIBCFS_PATH)/libcfs_sysctl.c \
                      $(GENERIC_LIBCFS_PATH)/upcall_cache.c \
                      $(GENERIC_LIBCFS_PATH)/hash.c

SOLARIS_LIBCFS_SRCS = $(SOLARIS_LIBCFS_PATH)/solaris-module.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-debug.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-prim.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-curproc.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-lock.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-tcpip.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-bitops.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-utils.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-mem.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-tracefile.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-watchdog.c \
                      $(SOLARIS_LIBCFS_PATH)/solaris-fs.c

LNET_SRCS           = $(LNET_PATH)/api-errno.c \
                      $(LNET_PATH)/config.c \
                      $(LNET_PATH)/lib-md.c \
                      $(LNET_PATH)/lib-move.c \
                      $(LNET_PATH)/lo.c \
                      $(LNET_PATH)/peer.c \
                      $(LNET_PATH)/router_proc.c \
                      $(LNET_PATH)/acceptor.c \
                      $(LNET_PATH)/api-ni.c \
                      $(LNET_PATH)/lib-eq.c \
                      $(LNET_PATH)/lib-me.c \
                      $(LNET_PATH)/lib-msg.c \
                      $(LNET_PATH)/module.c \
                      $(LNET_PATH)/router.c

KSOCKLND_SRCS       = $(KSOCKLND_PATH)/socklnd.c \
                      $(KSOCKLND_PATH)/socklnd_cb.c \
                      $(KSOCKLND_PATH)/socklnd_modparams.c \
                      $(KSOCKLND_PATH)/socklnd_proto.c \
                      $(KSOCKLND_PATH)/socklnd_lib-solaris.c \
                      $(KSOCKLND_PATH)/socklnd_lib-params.c

SELFTEST_SRCS      = $(SELFTEST_PATH)/brw_test.c \
                     $(SELFTEST_PATH)/conctl.c \
                     $(SELFTEST_PATH)/conrpc.c \
                     $(SELFTEST_PATH)/console.c \
                     $(SELFTEST_PATH)/framework.c \
                     $(SELFTEST_PATH)/module.c \
                     $(SELFTEST_PATH)/ping_test.c \
                     $(SELFTEST_PATH)/rpc.c \
                     $(SELFTEST_PATH)/timer.c \
                     $(SELFTEST_PATH)/workitem.c

PTLRPC_SRCS        = $(PTLRPC_PATH)/client.c \
                     $(PTLRPC_PATH)/connection.c \
                     $(PTLRPC_PATH)/events.c \
                     $(PTLRPC_PATH)/import.c \
                     $(PTLRPC_PATH)/layout.c \
                     $(PTLRPC_PATH)/lproc_ptlrpc.c \
                     $(PTLRPC_PATH)/niobuf.c \
                     $(PTLRPC_PATH)/pack_generic.c \
                     $(PTLRPC_PATH)/pers.c \
                     $(PTLRPC_PATH)/pinger.c \
                     $(PTLRPC_PATH)/ptlrpc_module.c \
                     $(PTLRPC_PATH)/ptlrpcd.c \
                     $(PTLRPC_PATH)/recover.c \
                     $(PTLRPC_PATH)/sec.c \
                     $(PTLRPC_PATH)/sec_bulk.c \
                     $(PTLRPC_PATH)/sec_config.c \
                     $(PTLRPC_PATH)/sec_gc.c \
                     $(PTLRPC_PATH)/sec_lproc.c \
                     $(PTLRPC_PATH)/sec_null.c \
                     $(PTLRPC_PATH)/sec_plain.c \
                     $(PTLRPC_PATH)/service.c \
                     $(PTLRPC_PATH)/target.c \
                     $(PTLRPC_PATH)/wiretest.c

LDLM_SRCS          = $(LDLM_PATH)/interval_tree.c \
                     $(LDLM_PATH)/l_lock.c \
                     $(LDLM_PATH)/ldlm_extent.c \
                     $(LDLM_PATH)/ldlm_flock.c \
                     $(LDLM_PATH)/ldlm_inodebits.c \
                     $(LDLM_PATH)/ldlm_lib.c \
                     $(LDLM_PATH)/ldlm_lock.c \
                     $(LDLM_PATH)/ldlm_lockd.c \
                     $(LDLM_PATH)/ldlm_plain.c \
                     $(LDLM_PATH)/ldlm_pool.c \
                     $(LDLM_PATH)/ldlm_request.c \
                     $(LDLM_PATH)/ldlm_resource.c

OBDCLASS_SRCS      = $(OBDCLASS_PATH)/acl.c \
                     $(OBDCLASS_PATH)/capa.c \
                     $(OBDCLASS_PATH)/class_obd.c \
                     $(OBDCLASS_PATH)/debug.c \
                     $(OBDCLASS_PATH)/dt_object.c \
                     $(OBDCLASS_PATH)/genops.c \
                     $(OBDCLASS_PATH)/hash.c \
                     $(OBDCLASS_PATH)/idmap.c \
                     $(OBDCLASS_PATH)/llog.c \
                     $(OBDCLASS_PATH)/llog_cat.c \
                     $(OBDCLASS_PATH)/llog_ioctl.c \
                     $(OBDCLASS_PATH)/llog_obd.c \
                     $(OBDCLASS_PATH)/llog_osd.c \
                     $(OBDCLASS_PATH)/llog_swab.c \
                     $(OBDCLASS_PATH)/lprocfs_status.c \
                     $(OBDCLASS_PATH)/lu_object.c \
                     $(OBDCLASS_PATH)/lu_ref.c \
                     $(OBDCLASS_PATH)/lu_time.c \
                     $(OBDCLASS_PATH)/lustre-sysctl.c \
                     $(OBDCLASS_PATH)/lustre_handles.c \
                     $(OBDCLASS_PATH)/lustre_peer.c \
                     $(OBDCLASS_PATH)/md_local_object.c \
                     $(OBDCLASS_PATH)/mea.c \
                     $(OBDCLASS_PATH)/obd_config.c \
                     $(OBDCLASS_PATH)/obd_mount.c \
                     $(OBDCLASS_PATH)/obdo.c \
                     $(OBDCLASS_PATH)/statfs_pack.c \
                     $(OBDCLASS_PATH)/uuid.c

MISC_SRCS          = $(MISC_PATH)/lvfs_lib.c

OST_SRCS           = $(OST_PATH)/ost_handler.c \
                     $(OST_PATH)/lproc_ost.c

ECHO_SRCS          = $(ECHO_PATH)/echo.c \
                     $(ECHO_PATH)/lproc_echo.c

LOADGEN_OBJS        = $(LOADGEN_SRCS:.c=.o)
LIBLUSTREAPI_A_OBJS = $(LIBLUSTREAPI_A_SRCS:.c=.o)
LIBPTLCTL_A_OBJS    = $(LIBPTLCTL_A_SRCS:.c=.o)
LIBCFSUTIL_A_OBJS   = $(LIBCFSUTIL_A_SRCS:.c=.o)
LIB_LAPI_CFSUTIL_CMN_OBJS = $(LIB_LAPI_CFSUTIL_CMN_SRCS:.c=.o)

PTHREAD_USER_UTILS_OBJS = $(LOADGEN_OBJS)
USER_UTILS_OBJS     = $(LIBLUSTREAPI_A_OBJS) \
                      $(LIBPTLCTL_A_OBJS) $(LIBCFSUTIL_A_OBJS) \
                      $(LIB_LAPI_CFSUTIL_CMN_OBJS)

CFSD_OBJS           = $(CFSD_SRCS:.c=.o)

LIBCFS_SRCS   = $(GENERIC_LIBCFS_SRCS) $(SOLARIS_LIBCFS_SRCS)
LIBCFS_OBJS   = $(LIBCFS_SRCS:%.c=kernel/%.o)
LNET_OBJS     = $(LNET_SRCS:%.c=kernel/%.o)
KSOCKLND_OBJS = $(KSOCKLND_SRCS:%.c=kernel/%.o)
SELFTEST_OBJS = $(SELFTEST_SRCS:%.c=kernel/%.o)

PTLRPC_OBJS   = $(PTLRPC_SRCS:%.c=kernel/%.o)
LDLM_OBJS     = $(LDLM_SRCS:%.c=kernel/%.o)
OBDCLASS_OBJS = $(OBDCLASS_SRCS:%.c=kernel/%.o)
MISC_OBJS     = $(MISC_SRCS:%.c=kernel/%.o)
OST_OBJS      = $(OST_SRCS:%.c=kernel/%.o)
ECHO_OBJS     = $(ECHO_SRCS:%.c=kernel/%.o)

LUSTREFS_OBJS = $(LIBCFS_OBJS) $(LNET_OBJS) $(KSOCKLND_OBJS) $(SELFTEST_OBJS)
LUSTREFS_OBJS += $(PTLRPC_OBJS) $(LDLM_OBJS) $(OBDCLASS_OBJS) $(MISC_OBJS)
LUSTREFS_OBJS += $(OST_OBJS) $(ECHO_OBJS)

LOADGEN = $(LUSTRE_UTILS_PATH)/loadgen
LIBLUSTREAPI_A = $(LUSTRE_UTILS_PATH)/liblustreapi.a
LIBPTLCTL_A = $(LNET_UTILS_PATH)/libptlctl.a
LIBCFSUTIL_A = $(GENERIC_LIBCFS_PATH)/libcfsutil.a

CFSD = $(LUSTRE_SOLARIS_UTILS_PATH)/cfsd

all: lustrefs $(LOADGEN) $(CFSD)

lustrefs: $(LUSTREFS_OBJS)
	$(LD) -dy -r -o $@ $(LUSTREFS_OBJS) -N misc/ksocket
	$(CTFMERGE) $(CTFLBL_FLAGS) -o $@ $(LUSTREFS_OBJS)

LOADGEN_DEPS = $(LOADGEN_OBJS) $(LIBLUSTREAPI_A) $(LIBPTLCTL_A)
LOADGEN_DEPS += $(LIBCFSUTIL_A)

LOADGEN_LIBS = -lreadline -lcurses -lnsl
LOADGEN_LIBS +=  $(LIBLUSTREAPI_A) $(LIBPTLCTL_A) $(LIBCFSUTIL_A)

$(LOADGEN): $(LOADGEN_DEPS)
	$(CC) -o $@ $(UCFLAGS) $(LOADGEN_OBJS) $(LOADGEN_LIBS)

$(LIBLUSTREAPI_A): $(LIBLUSTREAPI_A_OBJS) $(LIB_LAPI_CFSUTIL_CMN_OBJS)
	-$(RM) -f $@
	$(AR) $@ $(LIBLUSTREAPI_A_OBJS) $(LIB_LAPI_CFSUTIL_CMN_OBJS)

$(LIBPTLCTL_A): $(LIBPTLCTL_A_OBJS)
	-$(RM) -f $@
	$(AR) $@ $(LIBPTLCTL_A_OBJS)

$(LIBCFSUTIL_A): $(LIBCFSUTIL_A_OBJS) $(LIB_LAPI_CFSUTIL_CMN_OBJS)
	-$(RM) -f $@
	$(AR) $@ $(LIBCFSUTIL_A_OBJS) $(LIB_LAPI_CFSUTIL_CMN_OBJS)

$(CFSD): $(CFSD_OBJS)
	$(CC) -o $@ $(CFSD_OBJS)

kernel/%.o: $(TOP)/%.c
	$(CC) -c -o $@ $(KCPPFLAGS) $(KCFLAGS) $<
	$(CTFCONVERT) -i $(CTFLBL_FLAGS) $@

$(PTHREAD_USER_UTILS_OBJS): $(TOP)/$${@:.o=}.c
	$(CC) -c -o $@ $(UCPPFLAGS) $(UCFLAGS) $(PTHREAD_UCFLAGS) \
	      $(TOP)/${@:.o=.c}

$(USER_UTILS_OBJS): $(TOP)/$${@:.o=}.c
	$(CC) -c -o $@ $(UCPPFLAGS) $(UCFLAGS) $(TOP)/${@:.o=.c}

$(CFSD_OBJS): $(TOP)/$(CFSD_SRCS)
	$(CC) -D_REENTRANT -c -o $@ -I$(TOP)/libcfs/include \
              $(TOP)/$(CFSD_SRCS)

clean:
	-$(RM) -f lustrefs $(LUSTREFS_OBJS) $(LOADGEN) $(USER_UTILS_OBJS) \
              $(PTHREAD_USER_UTILS_OBJS) $(LIBLUSTREAPI_A) $(LIBPTLCTL_A) \
              $(LIBCFSUTIL_A) $(CFSD) $(CFSD_OBJS)

.INIT:
	@mkdir -p kernel/libcfs/libcfs/solaris
	@mkdir -p kernel/lnet/klnds/socklnd
	@mkdir -p kernel/lnet/lnet
	@mkdir -p kernel/lnet/selftest
	@mkdir -p kernel/lustre/ptlrpc
	@mkdir -p kernel/lustre/ldlm
	@mkdir -p kernel/lustre/obdclass
	@mkdir -p kernel/lustre/lvfs
	@mkdir -p kernel/lustre/ost
	@mkdir -p kernel/lustre/obdecho
	@mkdir -p libcfs/libcfs/util
	@mkdir -p lnet/utils
	@mkdir -p lustre/utils/solaris

.KEEP_STATE:
