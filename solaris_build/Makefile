CC = gcc
LD = /usr/ccs/bin/ld

archstr = i86pc

LUSTRE_CPPFLAGS = -D__KERNEL__ -DCDEBUG_ENABLED -DWITH_WATCHDOG -DLIBCFS_DEBUG \
		  -DLNET_MAX_PAYLOAD=LNET_MTU -DBITS_PER_LONG=64

SOLARIS_CPPFLAGS = -D_KERNEL -D_MACHDEP

INCL_DIRS = -nostdinc \
            -I$(ONNV_TREE)/usr/src/uts/common \
            -I$(ONNV_TREE)/usr/src/uts/intel \
            -I$(ONNV_TREE)/usr/src/uts/$(archstr) \
            -I$(TOP)/libcfs/include -I$(TOP)/lnet/include -I$(TOP)/libcfs/libcfs

CPPFLAGS = $(LUSTRE_CPPFLAGS) $(SOLARIS_CPPFLAGS) $(INCL_DIRS) 

CFLAGS = -ffreestanding -nodefaultlibs -O3 -m64 -mcmodel=kernel -mno-red-zone

TOP = ..

GENERIC_LIBCFS_PATH=libcfs/libcfs
SOLARIS_LIBCFS_PATH=libcfs/libcfs/solaris
LUSTRE_UTILS_PATH=lustre/utils/solaris
LNET_PATH=lnet/lnet
KSOCKLND_PATH=lnet/klnds/socklnd
SELFTEST_PATH=lnet/selftest

GENERIC_LIBCFS_SRC = $(GENERIC_LIBCFS_PATH)/module.c \
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

SOLARIS_LIBCFS_SRC = $(SOLARIS_LIBCFS_PATH)/solaris-module.c \
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

LNET_SRC           = $(LNET_PATH)/api-errno.c \
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

KSOCKLND_SRC       = $(KSOCKLND_PATH)/socklnd.c \
                     $(KSOCKLND_PATH)/socklnd_cb.c \
                     $(KSOCKLND_PATH)/socklnd_modparams.c \
                     $(KSOCKLND_PATH)/socklnd_proto.c \
                     $(KSOCKLND_PATH)/socklnd_lib-solaris.c \
                     $(KSOCKLND_PATH)/socklnd_lib-params.c

SELFTEST_SRC       = $(SELFTEST_PATH)/brw_test.c \
                     $(SELFTEST_PATH)/conctl.c \
                     $(SELFTEST_PATH)/conrpc.c \
                     $(SELFTEST_PATH)/console.c \
                     $(SELFTEST_PATH)/framework.c \
                     $(SELFTEST_PATH)/module.c \
                     $(SELFTEST_PATH)/ping_test.c \
                     $(SELFTEST_PATH)/rpc.c \
                     $(SELFTEST_PATH)/timer.c \
                     $(SELFTEST_PATH)/workitem.c

LIBCFS_SRC = $(GENERIC_LIBCFS_SRC) $(SOLARIS_LIBCFS_SRC)

LIBCFS_OBJ   = $(LIBCFS_SRC:.c=.o)
LNET_OBJ     = $(LNET_SRC:.c=.o)
KSOCKLND_OBJ = $(KSOCKLND_SRC:.c=.o)
SELFTEST_OBJ = $(SELFTEST_SRC:.c=.o)

LUSTREFS_OBJ = $(LIBCFS_OBJ) $(LNET_OBJ) $(KSOCKLND_OBJ) $(SELFTEST_OBJ)

all: lustrefs $(LUSTRE_UTILS_PATH)/cfsd

lustrefs: $(LUSTREFS_OBJ)
	$(LD) -dy -r -o $@ $(LUSTREFS_OBJ) -N misc/ksocket

%.o: $(TOP)/%.c
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $<

$(LUSTRE_UTILS_PATH)/cfsd.o: $(TOP)/$(LUSTRE_UTILS_PATH)/cfsd.c
	$(CC) -D_REENTRANT -c -o $@ -I$(TOP)/libcfs/include $(TOP)/$*.c

$(LUSTRE_UTILS_PATH)/cfsd: $(LUSTRE_UTILS_PATH)/cfsd.o
	$(CC) -o $@ $(LUSTRE_UTILS_PATH)/cfsd.o

clean:
	@rm -f lustrefs $(GENERIC_LIBCFS_PATH)/*.o $(SOLARIS_LIBCFS_PATH)/*.o \
               $(LNET_PATH)/*.o $(KSOCKLND_PATH)/*.o $(SELFTEST_PATH)/*.o\
               $(LUSTRE_UTILS_PATH)/*.o $(LUSTRE_UTILS_PATH)/cfsd

.INIT:
	@mkdir -p libcfs/libcfs/solaris
	@mkdir -p lnet/klnds/socklnd
	@mkdir -p lnet/lnet
	@mkdir -p lnet/selftest
	@mkdir -p lustre/utils/solaris

.KEEP_STATE:
