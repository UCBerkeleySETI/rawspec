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

all: mygpuspec fileiotest

# TODO Replace with auto-generated dependencies
mygpuspec.o: mygpuspec.h
mygpuspec_gpu.o: mygpuspec.h
fileiotest.o: mygpuspec.h

%.o: %.cu
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) -dc $(GENCODE_FLAGS) -o $@ -c $<
	
libgpuspec.so: mygpuspec_gpu.o
	$(VERBOSE) $(NVCC) -shared $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ $(CUDA_STATIC_LIBS)

mygpuspec: libgpuspec.so
mygpuspec: mygpuspec.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lgpuspec

fileiotest: libgpuspec.so
fileiotest: fileiotest.o
	$(VERBOSE) $(NVCC) $(NVCC_FLAGS) $(GENCODE_FLAGS) -o $@ $^ -L. -lgpuspec

install: mygpuspec.h libgpuspec.so
	mkdir -p $(INCDIR)
	cp -p mygpuspec.h $(INCDIR)
	mkdir -p $(LIBDIR)
	cp -p libgpuspec.so $(LIBDIR)

clean:
	rm -f *.o *.so mygpuspec fileiotest tags

tags:
	ctags -R . $(CUDA_PATH)/samples/common/inc

.PHONY: all clean tags
