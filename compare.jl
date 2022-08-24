using Printf
using FFTW
using Blio: GuppiRaw

struct Result
  message::Union{String, Nothing}
  value::Bool
end

function _fft(guppidata, points)
  data = reshape(guppidata, (size(guppidata, 1), points, :, size(guppidata)[3:end]...))
  # [pol, fine_spec, time, coarse_chan, antenna]
  spectra = fft(data, 2)
  # [pol, fine_chan, time, antenna]
  spectra = cat((spectra[:, :, :, i, :] for i in 1:size(spectra,4))..., dims=2)
  # [pol, time, fine_chan, antenna]
  return permutedims(spectra, [1,3,2,4])
end

function accumulate(guppidata, samples)
  data = reshape(guppidata, (size(guppidata, 1), samples, :, size(guppidata)[3:end]...))
  acc = sum(data, dims=2)
  return reshape(acc, (size(guppidata, 1), :, size(guppidata)[3:end]...))
end

function mapToFloat(value::Integer, type::Type)
  return value < 0 ? -1.0*(value / typemin(type)) : value / typemax(type)
end

function mapToFloat(value::Complex{<:Integer}, type::Type)
  return complex(mapToFloat(real(value), real(type)), mapToFloat(imag(value), real(type)))
end

function compare(i_data, o_data, atol=0.01)::Result
  if size(i_data) != size(o_data)
    return Result(@sprintf("Shape mismatch: %s != %s", size(i_data), size(o_data)), false)
  end
  dims_correct = Array{Bool}(undef, size(i_data)[2:end])
  for i in CartesianIndices(dims_correct)
    dims_correct[i] = all(isapprox.(real(i_data[:, i]), real(o_data[:, i]), atol=atol)) && all(isapprox.(imag(i_data[:, i]), imag(o_data[:, i]), atol=atol))
    if !dims_correct[i]
      println(Result(@sprintf("Pol data mismatch @ %s: %s != %s\n\t(atol: %s)", i, i_data[:, i], o_data[:, i], i_data[:, i] - o_data[:, i]), false))
    end
  end

  Result(nothing, all(dims_correct))
end

i_grheader = GuppiRaw.Header()
o_grheader = GuppiRaw.Header()

i_fio = open("/mnt/buf0/test.0000.raw", "r")
o_fio = open("/mnt/buf0/test.rawspec.0000.raw", "r")

  read!(i_fio, i_grheader)
  i_data = Array(i_grheader)
  read!(i_fio, i_data)
  i_data = mapToFloat.(i_data, eltype(i_data))

  read!(o_fio, o_grheader)
  o_data = Array(o_grheader)
  read!(o_fio, o_data)

  fftpoints = div(size(o_data, 3), size(i_data, 3))
  accumulation = div(div(size(i_data, 2), size(o_data, 2)), fftpoints)
  println("fftpoints: ", fftpoints)
  println("accumulation: ", accumulation)

  upchannelized = fftpoints == 1 ? i_data : _fft(i_data, fftpoints)
  accum_fine = accumulation == 1 ? upchannelized : accumulate(upchannelized, accumulation)

  atol = 0.015 * max(log2(fftpoints), fftpoints) * accumulation

  println(compare(accum_fine, o_data, atol))

close(i_fio)
close(o_fio)