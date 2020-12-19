#!/bin/bash
PREFIX_EXEC=/opt/mnt/bin/

# -f specifies the number of fine channels, equalling the number of timesamples used per output.
# -t specifies the number of time integrations
# Samples/
# The output has a shape equal (|SAMPLES|/(|FINE|*|INTEG|), POLSOUT, |COARSE|*|FINE|)

# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-random
# ./rawspec -f 2 -t 4 -d ./outputs_4bit ./inputs_4bit/guppiSigFile4bit-random
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-random ./outputs_8bit/guppiSigFile8bit-random

# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-smallrandom
# ./rawspec -f 2 -t 4 -d ./outputs_4bit ./inputs_4bit/guppiSigFile4bit-4ant-smallrandom
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-smallrandom ./outputs_8bit/guppiSigFile8bit-4ant-smallrandom

# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-smallrandom "$@"
# ./rawspec -f 2 -t 4 -d ./outputs_4bit ./inputs_4bit/guppiSigFile4bit-4ant-smallrandom "$@"
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-smallrandom-ant000 ./outputs_8bit/guppiSigFile8bit-4ant-smallrandom-ant000
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-smallrandom-ant001 ./outputs_8bit/guppiSigFile8bit-4ant-smallrandom-ant001
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-smallrandom-ant002 ./outputs_8bit/guppiSigFile8bit-4ant-smallrandom-ant002
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-smallrandom-ant003 ./outputs_8bit/guppiSigFile8bit-4ant-smallrandom-ant003


# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-smallrandom "$@"

# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-ant2nonzero "$@"
# ./rawspec -f 2 -t 4 -d ./outputs_4bit ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero "$@"

# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-ant2nonzero-ant000 ./outputs_8bit/guppiSigFile8bit-4ant-ant2nonzero-ant000
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-ant2nonzero-ant001 ./outputs_8bit/guppiSigFile8bit-4ant-ant2nonzero-ant001
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-ant2nonzero-ant002 ./outputs_8bit/guppiSigFile8bit-4ant-ant2nonzero-ant002
# python3 analyse.py ./outputs_4bit/guppiSigFile4bit-4ant-ant2nonzero-ant003 ./outputs_8bit/guppiSigFile8bit-4ant-ant2nonzero-ant003


time ./rawspec -f 2 -t 2  -d ./outputs_8bit/per_ant_out ./inputs/guppiSigFile8bit-4ant-ant2nonzero
time ./rawspec -f 2 -t 2 -S -d ./outputs_8bit/per_ant_out ./inputs/guppiSigFile8bit-4ant-ant2nonzero

# python3 analyse.py ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant000
# python3 analyse.py ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant001
# python3 analyse.py ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant002
# python3 analyse.py ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant003

time ./rawspec -f 2 -t 2 -a 0 -d ./outputs_8bit/ant_0 ./inputs/guppiSigFile8bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_8bit/ant_0/guppiSigFile8bit-4ant-ant2nonzero ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant000

time ./rawspec -f 2 -t 2 -a 1 -d ./outputs_8bit/ant_1 ./inputs/guppiSigFile8bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_8bit/ant_1/guppiSigFile8bit-4ant-ant2nonzero ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant001

time ./rawspec -f 2 -t 2 -a 2 -d ./outputs_8bit/ant_2 ./inputs/guppiSigFile8bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_8bit/ant_2/guppiSigFile8bit-4ant-ant2nonzero ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant002

time ./rawspec -f 2 -t 2 -a 3 -d ./outputs_8bit/ant_3 ./inputs/guppiSigFile8bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_8bit/ant_3/guppiSigFile8bit-4ant-ant2nonzero ./outputs_8bit/per_ant_out/guppiSigFile8bit-4ant-ant2nonzero-ant003


#########################

time ./rawspec -f 2 -t 2  -d ./outputs_4bit/per_ant_out ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero
time ./rawspec -f 2 -t 2 -S -d ./outputs_4bit/per_ant_out ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero

# python3 analyse.py ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant000
# python3 analyse.py ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant001
# python3 analyse.py ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant002
# python3 analyse.py ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant003

time ./rawspec -f 2 -t 2 -a 0 -d ./outputs_4bit/ant_0 ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_4bit/ant_0/guppiSigFile4bit-4ant-ant2nonzero ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant000

time ./rawspec -f 2 -t 2 -a 1 -d ./outputs_4bit/ant_1 ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_4bit/ant_1/guppiSigFile4bit-4ant-ant2nonzero ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant001

time ./rawspec -f 2 -t 2 -a 2 -d ./outputs_4bit/ant_2 ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_4bit/ant_2/guppiSigFile4bit-4ant-ant2nonzero ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant002

time ./rawspec -f 2 -t 2 -a 3 -d ./outputs_4bit/ant_3 ./inputs_4bit/guppiSigFile4bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_4bit/ant_3/guppiSigFile4bit-4ant-ant2nonzero ./outputs_4bit/per_ant_out/guppiSigFile4bit-4ant-ant2nonzero-ant003
