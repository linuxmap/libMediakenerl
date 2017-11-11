#############################################################################
# Makefile for building: gessctrl
#############################################################################
PREFIX          = /usr/local
INSTALL_ROOT    = $(PREFIX)
CC              = gcc 
AR              = ar 
LIB_DIR         = $(PREFIX)/lib
FFMPEG_DIR      = $(PREFIX)/include/
FFMPEG_LIB      = $(PREFIX)/lib
TARGET_LIB      = ./lib/libMediaKenerl.a
TARGET_DLL      = ./lib/libMediaKenerl.so
SRC_DIR         = ../../mediakenerl/
LIBS            =""

CFLAGS    += -pipe -g -w -fPIC -O0 -m64 -DENV_LINUX
ARFLAGS   += 

ifneq (${prefix},"")
    INSTALL_ROOT=${prefix}
endif

ifneq (${with-ffmepg},"")
    FFMPEG_DIR=${with-ffmpeg}/include/
    FFMPEG_LIB=$(with-ffmpeg)/lib
endif

LIBS        = -shared -m64 -fPIC -lm -lpthread 
LIBS       += -Wl,-dy -L$(FFMPEG_LIB) -lavformat -lavcodec -lavdevice -lavfilter -lavutil -lswresample -lswscale -lx264

INCPATH   += -I./ -I$(SRC_DIR) -I$(FFMPEG_DIR)




CFILES    += $(SRC_DIR)mk_media_cmdutils.c $(SRC_DIR)mk_media_worker.c $(SRC_DIR)mk_media_worker_opt.c 
CFILES    += $(SRC_DIR)mk_media_worker_filter.c $(SRC_DIR)mk_thread.c 
CFILES    += $(SRC_DIR)mk_task_monitor.c $(SRC_DIR)mk_mutex.c $(SRC_DIR)libMediaKenerl.c
HEADFILES += $(SRC_DIR)mk_config.h $(SRC_DIR)mk_common.h $(SRC_DIR)mk_def.h $(SRC_DIR)mk_media_cmdutils.h
HEADFILES += $(SRC_DIR)mk_media_cmdutils_opts.h $(SRC_DIR)mk_media_worker.h $(SRC_DIR)mk_thread.h 
HEADFILES += $(SRC_DIR)mk_task_monitor.h $(SRC_DIR)mk_mutex.h $(SRC_DIR)libMediaKenerl.h

ifdef OPENCL
CFILES    += $(SRC_DIR)mk_media_cmdutils_opencl.c
CFLAGS    += -D CONFIG_OPENCL=1
endif

ifdef DXVA
CFILES    += $(SRC_DIR)mk_media_dxva2.c
CFLAGS    += -D HAVE_DXVA2_LIB=1
endif

ifdef VDPAU
CFILES    += $(SRC_DIR)mk_media_vdpau.c
CFLAGS    += -D HAVE_VDPAU_X11=1
endif

ifdef VIDEOTOOL
CFILES    += $(SRC_DIR)mk_media_videotoolbox.c
CFLAGS    += -D CONFIG_VDA=1 -D CONFIG_VIDEOTOOLBOX=1
endif

   
COBJS=$(CFILES:.c=.o)


all: $(TARGET_LIB) $(TARGET_DLL)
$(TARGET_DLL): $(COBJS)
	$(CC) -o $@ $(COBJS) $(LIBS)

$(TARGET_LIB): $(COBJS)
	$(AR) rcs $@ $(COBJS) 

%.o : %.c $(HEADFILES)
	$(CC) -c  $(CFLAGS) $(INCPATH) $< -o $@

clean:
	rm -f $(TARGET) $(COBJS)
install:
	cp ${SRC_DIR}/libMediaKenerl.h ${INSTALL_ROOT}/include/
	cp ${SRC_DIR}//mk_def.h ${INSTALL_ROOT}/include/
	cp ${TARGET_LIB} ${INSTALL_ROOT}/lib/
	cp ${TARGET_DLL} ${INSTALL_ROOT}/lib/
