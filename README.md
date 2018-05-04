# rawspec - A CUDA-based spectroscopy package for GUPPI RAW data

rawspec reads GUPPI RAW files and produces integrated power spectra.  Up to 4
different output products with various channelization/integration combinations
can be created at one time.  Output products can either be total power (aka
Stokes I) or full cross polarization spectra.  Currently, rawspec outputs to
SIGPROC Filterbank files, see `http://sigproc.sourceforge.net`.  It can also
output the spectral data as UDP packets to a remote receiver.  Each such UDP
packet is a small, self-contained Filterbank "file".

# Usage

```
$ rawspec -h
Usage: rawspec [options] STEM [...]

Options:
  -d, --dest=DEST       Destination directory or host:port
  -f, --ffts=N1[,N2...] FFT lengths
  -H, --hdrs            Save headers to separate file
  -n, --nchan=N         Number of coarse channels to process [all]
  -p  --pols={1|4}      Number of output polarizations [1]
                        1=total power, 4=cross pols
  -r, --rate=GBPS       Desired net data rate in Gbps [6.0]
  -s, --schan=C         First coarse channel to process [0]
  -t, --ints=N1[,N2...] Spectra to integrate

  -h, --help            Show this message
  -v, --version         Show version and exit
```

# Example

Given the following 16 RAW files from a 5 minute observation:

```
$ ls -1 *.raw
guppi_58196_56989_625564_G358.87+2.42_0001.0000.raw
guppi_58196_56989_625564_G358.87+2.42_0001.0001.raw
[...]
guppi_58196_56989_625564_G358.87+2.42_0001.0014.raw
guppi_58196_56989_625564_G358.87+2.42_0001.0015.raw
```

Use the following command to create three total power output products with:

1. 1048576 fine channels per coarse channel, integrating 51 spectra per dump

2. 8 fine channels per coarse channel, integrating 128 spectra per dump

3. 1024 fine channels per coarse channel, integrating 3072 spectra per dump

```
$ time rawspec -f 1048576,8,1024 -t 51,128,3072 guppi_58196_56989_625564_G358.87+2.42_0001
working stem: guppi_58196_56989_625564_G358.87+2.42_0001
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0000.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0001.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0002.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0003.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0004.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0005.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0006.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0007.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0008.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0009.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0010.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0011.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0012.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0013.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0014.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0015.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0016.raw [No such file or directory]
output product 0: 19 spectra
output product 1: 1025024 spectra
output product 2: 333 spectra

real    2m25.119s
user    0m37.092s
sys     1m9.956s
```

This produces the following outputs:

```
$ ls -1sh *.fil
3.3G guppi_58196_56989_625564_G358.87+2.42_0001.rawspec.0000.fil
1.4G guppi_58196_56989_625564_G358.87+2.42_0001.rawspec.0001.fil
 58M guppi_58196_56989_625564_G358.87+2.42_0001.rawspec.0002.fil
```

Add `-p 4` to the command line to create three full polarization output
products with the same time and frequency resolutions as before:

```
$ time rawspec -p 4 -f 1048576,8,1024 -t 51,128,3072 guppi_58196_56989_625564_G358.87+2.42_0001
working stem: guppi_58196_56989_625564_G358.87+2.42_0001
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0000.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0001.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0002.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0003.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0004.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0005.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0006.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0007.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0008.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0009.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0010.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0011.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0012.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0013.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0014.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0015.raw
opening file: guppi_58196_56989_625564_G358.87+2.42_0001.0016.raw [No such file or directory]
output product 0: 19 spectra
output product 1: 1025024 spectra
output product 2: 333 spectra

real    3m43.689s
user    1m29.260s
sys     1m36.232s
```

This produces the following outputs:

```
$ ls -1sh *.fil
 14G guppi_58196_56989_625564_G358.87+2.42_0001.rawspec.0000.fil
5.4G guppi_58196_56989_625564_G358.87+2.42_0001.rawspec.0001.fil
229M guppi_58196_56989_625564_G358.87+2.42_0001.rawspec.0002.fil
```

While the full polarization products were being generated from RAW files
containing 44 coarse channels, `nvidia-smi` shows something like this:

```
$ nvidia-smi 
Fri May  4 15:57:16 2018       
+-----------------------------------------------------------------------------+
| NVIDIA-SMI 375.39                 Driver Version: 375.39                    |
|-------------------------------+----------------------+----------------------+
| GPU  Name        Persistence-M| Bus-Id        Disp.A | Volatile Uncorr. ECC |
| Fan  Temp  Perf  Pwr:Usage/Cap|         Memory-Usage | GPU-Util  Compute M. |
|===============================+======================+======================|
|   0  GeForce GTX 1080    Off  | 0000:82:00.0     Off |                  N/A |
| 37%   59C    P2    57W / 200W |   3515MiB /  8114MiB |     62%      Default |
+-------------------------------+----------------------+----------------------+
                                                                               
+-----------------------------------------------------------------------------+
| Processes:                                                       GPU Memory |
|  GPU       PID  Type  Process name                               Usage      |
|=============================================================================|
|    0     28709    C   rawspec                                       3511MiB |
+-----------------------------------------------------------------------------+
```

# Installation

```
$ git clone https://github.com/UCBerkeleySETI/rawspec.git

$ cd rawspec

$ make

$ sudo make install
```
