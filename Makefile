PREFIX = /usr/local
INCDIR = $(PREFIX)/include
LIBDIR = $(PREFIX)/lib

CUDA_PATH ?= /usr/local/cuda-8.0

CC=gcc
HOST_COMPILER ?= $(CC)
NVCC          := $(CUDA_PATH)/bin/nvcc -ccbin $(HOST_COMPILER)

CFLAGS = -ggdb
NVCC_FLAGS = -m64 -g -Xcompiler=-fPIC -I$(CUDA_PATH)/samples/common/inc

CUDA_SHARED_LIBS = -lcufft
CUDA_STATIC_LIBS = -lcufft_static -lculibos

ifeq ($(GENCODE_FLAGS),) # GENCODE_FLAGS {
# Gencode arguments
SMS ?= 30 35 50 60

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

all: rawspec rawspectest fileiotest

# TODO Replace with auto-generated dependencies
rawspec.o: rawspec.h fitshead.h rawutils.h rawspec_callback.h \
	         rawspec_file.h rawspec_socket.h
rawutils.o: rawutils.h
rawspectest.o: rawspec.h
rawspec_gpu.o: rawspec.h
fileiotest.o: rawspec.h
rawspec_file.o: rawspec_file.h
rawspec_socket.o: rawspec_socket.h

%.o: %.cu
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) -dc $(GENCODE_FLAGS) -o $@ -c $<
	
librawspec.so: rawspec_gpu.o
	$(VERBOSE) $(NVCC) -shared $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ $(CUDA_STATIC_LIBS)

rawspec: librawspec.so
rawspec: rawspec.o rawutils.o fbutils.o hget.o rawspec_file.o rawspec_socket.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lrawspec

rawspectest: librawspec.so
rawspectest: rawspectest.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lrawspec

fileiotest: librawspec.so
fileiotest: fileiotest.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lrawspec

install: rawspec.h librawspec.so
	mkdir -p $(INCDIR)
	cp -p rawspec.h $(INCDIR)
	mkdir -p $(LIBDIR)
	cp -p librawspec.so $(LIBDIR)

clean:
	rm -f *.o *.so rawspec rawspectest fileiotest tags

tags:
	ctags -R . $(CUDA_PATH)/samples/common/inc

.PHONY: all clean tags
