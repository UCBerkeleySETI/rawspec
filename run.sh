#!/bin/bash
PREFIX_EXEC=/opt/mnt/bin/

# -f specifies the number of fine channels, equalling the number of timesamples used per output.
# -t specifies the number of time integrations
# Samples/
# The output has a shape equal (|SAMPLES|/(|FINE|*|INTEG|), 1, |COARSE|*|FINE|)

# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-random
# ./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-smallrandom "$@"
./rawspec -f 2 -t 4 -d ./outputs_8bit ./inputs/guppiSigFile8bit-4ant-ant2nonzero "$@"