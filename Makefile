PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
INCDIR = $(PREFIX)/include
LIBDIR = $(PREFIX)/lib
DATADIR = $(PREFIX)/share

CUDA_DIR ?= /usr/local/cuda-8.0
CUDA_PATH ?= $(CUDA_DIR)

CC=gcc
HOST_COMPILER ?= $(CC)
NVCC          := $(CUDA_PATH)/bin/nvcc -ccbin $(HOST_COMPILER)

CFLAGS = -ggdb -fPIC
ifdef DEBUG_CALLBACKS
CFLAGS += -DDEBUG_CALLBACKS=$(DEBUG_CALLBACKS)
endif
NVCC_FLAGS = -m64 -g -Xcompiler=-fPIC -I$(CUDA_PATH)/samples/common/inc
ifdef VERBOSE_ALLOC
NVCC_FLAGS += -DVERBOSE_ALLOC=$(VERBOSE_ALLOC)
endif

CUDA_SHARED_LIBS = -lcufft
CUDA_STATIC_LIBS = -lcufft_static -lculibos

ifeq ($(GENCODE_FLAGS),) # GENCODE_FLAGS {
# Gencode arguments
SMS ?= 35 50 52 60 61 70 75

ifeq ($(SMS),) # SMS {
$(error no SM architectures have been specified)
endif # } SMS

# Generate SASS code for each SM architecture listed in $(SMS)
$(foreach sm,$(SMS),$(eval GENCODE_FLAGS += -gencode arch=compute_$(sm),code=sm_$(sm)))

# Generate PTX code from the highest SM architecture in $(SMS) to guarantee forward-compatibility
HIGHEST_SM := $(lastword $(sort $(SMS)))
ifneq ($(HIGHEST_SM),) # HIGHEST_SM {
GENCODE_FLAGS += -gencode arch=compute_$(HIGHEST_SM),code=compute_$(HIGHEST_SM)
endif # } HIGHEST_SM
endif # } GENCODE_FLAGS

ifeq ($(Q),1)
VERBOSE = @
endif

# Possibly (re-)build rawspec_version.h
$(shell $(SHELL) gen_version.sh)

all: rawspec rawspectest fileiotest

# Dependencoes are simple enough to manage manually (for now)
fileiotest.o: rawspec.h
rawspec.o: rawspec.h rawspec_rawutils.h rawspec_callback.h \
           rawspec_file.h rawspec_socket.h rawspec_version.h \
           rawspec_fbutils.h
rawspec_fbutils.o: rawspec_fbutils.h
rawspec_file.o: rawspec_file.h rawspec.h \
                rawspec_callback.h rawspec_fbutils.h
rawspec_gpu.o: rawspec.h rawspec_version.h
rawspec_socket.o: rawspec_socket.h rawspec.h \
                  rawspec_callback.h rawspec_fbutils.h
rawspectest.o: rawspec.h
rawspec_rawutils.o: rawspec_rawutils.h hget.h

%.o: %.cu
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) -dc $(GENCODE_FLAGS) -o $@ -c $<
	
librawspec.so: rawspec_gpu.o rawspec_fbutils.o rawspec_rawutils.o
	$(VERBOSE) $(NVCC) -shared $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ $(CUDA_STATIC_LIBS)

rawspec: librawspec.so
rawspec: rawspec.o rawspec_file.o rawspec_socket.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lrawspec

rawspectest: librawspec.so
rawspectest: rawspectest.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lrawspec

fileiotest: librawspec.so
fileiotest: fileiotest.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lrawspec

rawspec_fbutils: rawspec_fbutils.c rawspec_fbutils.h
	$(CC) -o $@ -DFBUTILS_TEST -ggdb -O0 $< -lm

install: rawspec.h librawspec.so
	mkdir -p $(BINDIR)
	cp -p rawspec $(BINDIR)
	mkdir -p $(INCDIR)
	cp -p rawspec.h $(INCDIR)
	cp -p rawspec_fbutils.h $(INCDIR)
	cp -p rawspec_rawutils.h $(INCDIR)
	mkdir -p $(LIBDIR)
	cp -p librawspec.so $(LIBDIR)
	mkdir -p $(DATADIR)/aclocal
	cp -p m4/rawspec.m4 $(DATADIR)/aclocal

clean:
	rm -f *.o *.so rawspec rawspectest fileiotest tags rawspec_version.h

tags:
	ctags -R .

.PHONY: all install clean tags tags
