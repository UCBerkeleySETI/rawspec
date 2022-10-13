using Printf
using Plots
using Blio: GuppiRaw

ENV["GKSwstype"]="nul" # disable plots display

dir = "/mnt/buf0/"

for filestem in ["bladetest_signal_out"]

  grheader = GuppiRaw.Header()
  fio = open(@sprintf("%s%s.0000.raw", dir, filestem), "r")
    read!(fio, grheader)
    data = Array(grheader)
    @printf("%s, %s bytes\n", size(grheader), prod(size(grheader))*2)
    @printf("%d entries, %s bytes\n", length(grheader), length(grheader)*80)
    @printf("%s\n", grheader)
    @printf("@%d, %d bytes in file (%d bytes remain)\n", position(fio), filesize(fio), filesize(fio) - position(fio))
    read!(fio, data)
    # read!(fio, grheader)
    # read!(fio, data)
    data = abs.(data)
    @printf("data [%s, %s]\n", min(data...), max(data...))
    if min(data...) != NaN

      figpol1 = heatmap(data[1, :, 1:8, 1])
      figpol2 = heatmap(data[2, :, 1:8, 1])
      # title!(fig, "New title")
      # flipxaxis!(fig, "New xlabel")
      # yaxis!(fig, "New ylabel")
      
      savefig(plot(figpol1, figpol2, layout=(2,1)), @sprintf("%s.png", filestem))
    end

  close(fio)
end