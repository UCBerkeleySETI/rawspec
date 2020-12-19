 
# import matplotlib.pyplot as plt
import sys
import numpy as np
# import pylab as plt
from blimpy import Waterfall

stem = sys.argv[1]

obs = Waterfall(stem+ '.rawspec.0000.fil')
obs.info()

print(obs.data)
print(obs.data.shape)

if len(sys.argv) == 3:
	comparisonstem = sys.argv[2]
	print("\n\n Compared to: ", comparisonstem, "\n\n")
	comp_obs = Waterfall(comparisonstem+ '.rawspec.0000.fil')
	# comp_obs.info()

	print(comp_obs.data)
	print(comp_obs.data.shape)
	delta_sum = np.sum(comp_obs.data - obs.data)
	if delta_sum == 0:
		print("Data is the same, delta sum == 0.")
	else:
		print("Sum of differences is", delta_sum, " != 0.")

# else:
# 	fig = plt.figure()
# 	# plot(obs.data)
# 	# fig.savefig(stem+'-waterfall.png')


# 	obs.plot_waterfall()
# 	fig.savefig(stem+'-waterfall.png')
# 	obs.plot_spectrum()
# 	fig.savefig(stem+'-spectra.png')