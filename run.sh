#!/bin/bash

# -f specifies the number of fine channels, equalling the number of timesamples used per output.
# -t specifies the number of time integrations
# Samples/
# The output has a shape equal (|SAMPLES|/(|FINE|*|INTEG|), 1, |COARSE|*|FINE|)


# echo "8bit GUPPI RAW (random values)"

# rm ./outputs_8bit/guppiSigFile8bit-random.rawspec.0000.fil
# time rawspec -f 1024 -t 32 -d ./outputs_8bit ./inputs/guppiSigFile8bit-random
# # time bash ./run_repeat.sh "rawspec -f 1024 -t 32 -d ./outputs_8bit ./inputs/guppiSigFile8bit-random"
# # python3 analyse.py ./outputs_8bit/guppiSigFile8bit-random

# echo "4bit GUPPI RAW (random values)"

# rm ./outputs_4bit/guppiSigFile4bit-random.rawspec.0000.fil
time rawspec -f 1024 -t 32 -d ./outputs_4bit ./inputs_4bit/guppiSigFile4bit-random
# # time bash ./run_repeat.sh "rawspec -f 1024 -t 32 -d ./outputs_4bit ./inputs_4bit/guppiSigFile4bit-random"

python3 analyse.py ./outputs_4bit/guppiSigFile4bit-random ./outputs_8bit/guppiSigFile8bit-random


# time rawspec -f 1024 -t 32 -a 1 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-ant2nonzero
# python3 analyse.py ./outputs_8bit/guppiSigFile8bit-4ant-ant2nonzero

# time ./rawspec -f 1024 -t 1 -p 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-8chan
# python3 analyse.py ./outputs_8bit/guppiSigFile8bit-4ant-8chan

# time ./rawspec -f 1 -t 512 -d ./outputs_4bit /mnt/buf0/dmpauto_wael/GUPPI/guppi_59179_74555_005786_Unknown_0001
# python3 analyse.py ./outputs_4bit/guppi_59179_74555_005786_Unknown_0001