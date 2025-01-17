import sys
import random
import math
import heapq
from optparse import OptionParser
from custom_rand import CustomRand

class Flow:
	def __init__(self, src, dst, size, t):
		self.src, self.dst, self.size, self.t = src, dst, size, t
	def __str__(self):
		return "%d %d 3 100 %d %.9f"%(self.src, self.dst, self.size, self.t)

def translate_bandwidth(b):
	if b == None:
		return None
	if type(b)!=str:
		return None
	if b[-1] == 'G':
		return float(b[:-1])*1e9
	if b[-1] == 'M':
		return float(b[:-1])*1e6
	if b[-1] == 'K':
		return float(b[:-1])*1e3
	return float(b)

def poisson(lam):
	return -math.log(1-random.random())*lam

if __name__ == "__main__":
	port = 80
	parser = OptionParser()
	parser.add_option("-c", "--cdf", dest = "cdf_file", help = "the file of the traffic size cdf", default = "uniform_distribution.txt")
	parser.add_option("-n", "--nhost", dest = "nhost", help = "number of hosts")
	parser.add_option("-l", "--load", dest = "load", help = "the percentage of the traffic load to the network capacity, by default 0.3", default = "0.3")
	parser.add_option("-b", "--bandwidth", dest = "bandwidth", help = "the bandwidth of host link (G/M/K), by default 10G", default = "10G")
	parser.add_option("-t", "--time", dest = "time", help = "the total run time (s), by default 10", default = "10")
	parser.add_option("-o", "--output", dest = "output", help = "the output file", default = "tmp_traffic.txt")
	parser.add_option("-i", "--incast", dest = "incast", help = "add incast or not", default = "0")
	parser.add_option("-p", "--pattern", dest = "pattern", help = "traffic pattern, 0(normal), 1(only_cross_pod), 2(only_inner_pod)", default = "0")
	parser.add_option("-j", "--inject", dest = "inject", help = "inject long flow and 1pkt flow", default = "0")
	parser.add_option("-r", "--repeat", dest = "repeat", help = "Number of repeat genereatin non-incast flows", default="1")
	parser.add_option("-y", "--priority", dest = "priority", help = "If 1, prioritize short flows (less than 10KB) with priority_group 1", default = "0")
	options,args = parser.parse_args()

	base_t = 2000000000

	if not options.nhost:
		print "please use -n to enter number of hosts"
		sys.exit(0)
	nhost = int(options.nhost)
	inject = int(options.inject)
	repeat = int(options.repeat)
	pattern = int(options.pattern)
	load = float(options.load)
	bandwidth = translate_bandwidth(options.bandwidth)
	time = float(options.time)*1e9 # translates to ns
	priority = int(options.priority)
	low_prio = 3
	high_prio = 1
	
	

	output = options.output
	if(output.find("tmp_traffic.txt")!=-1):
		output = "{}_n{}_load{}_t{}_inject{}_repeat{}.txt".format(options.cdf_file.replace(".txt", ""), nhost, load, options.time, inject, repeat)

	if bandwidth == None:
		print "bandwidth format incorrect"
		sys.exit(0)

	fileName = options.cdf_file
	file = open(fileName,"r")
	lines = file.readlines()
	# read the cdf, save in cdf as [[x_i, cdf_i] ...]
	cdf = []
	for line in lines:
		x,y = map(float, line.strip().split(' '))
		cdf.append([x,y])

	# create a custom random generator, which takes a cdf, and generate number according to the cdf
	customRand = CustomRand()
	if not customRand.setCdf(cdf):
		print "Error: Not valid cdf"
		sys.exit(0)

	ofile = open(output, "w")

	# Store results to outputlater
	output_flow_lst = []
	n_flow = 0
	if (inject):
		n_flow += 1
		output_flow_lst.append((base_t * 1e-9,"%d %d %d 100 %d %.9f\n"%(0, 1, low_prio, 10000000, base_t * 1e-9)))
	
	# generate non-incast flows
	avg = customRand.getAvg()
	avg_inter_arrival = 1/(bandwidth*load/8./avg)*1000000000
	n_flow_estimate = int(time / avg_inter_arrival * nhost) * repeat
	ofile.write("%d \n" % n_flow_estimate)

	for round in range(repeat):
		#base_t is different for different repeated round
		round_interval = time * 2.0 #interval is the same as duration
		base_t_curRound = base_t + round * round_interval
		print(round, base_t_curRound, n_flow)
		host_list = [(base_t_curRound + int(poisson(avg_inter_arrival)), i) for i in range(nhost)]
		heapq.heapify(host_list)
			
		while len(host_list) > 0:
			t,src = host_list[0]
			inter_t = int(poisson(avg_inter_arrival))
			new_tuple = (src, t + inter_t)
			dst = random.randint(0, nhost-1)
			if (pattern == 0):
				while (dst == src):
					dst = random.randint(0, nhost-1)
			elif (pattern == 1):
				#only cross pod
				while (dst % 16 == src % 16):
					dst = random.randint(0, nhost-1)
			elif (pattern == 2):
				#only cross pod
				while (dst % 16 != src % 16 or dst == src):
					dst = random.randint(0, nhost-1)
			if (t + inter_t > time + base_t_curRound):
				heapq.heappop(host_list)
			else:
				size = int(customRand.rand())
				if size <= 0:
					size = 1
				

				# Inject 1pkt flow to detect qDelay
				if (inject and n_flow > 100 and n_flow % 1000 == 0):
					n_flow += 1
					if(priority):
						output_flow_lst.append(( (t-1) * 1e-9,"%d %d %d 100 %d %.9f\n"%(0, 1, high_prio, 100, (t-1) * 1e-9)))
					else:
						output_flow_lst.append(( (t-1) * 1e-9,"%d %d %d 100 %d %.9f\n"%(0, 1, low_prio, 100, (t-1) * 1e-9)))

				n_flow += 1
				#Append flow_tuple (start_time, flow_info)
				if(priority and size < 10000):
					output_flow_lst.append((t * 1e-9,"%d %d %d 100 %d %.9f\n"%(src, dst, high_prio, size, t * 1e-9)))
				else:
					output_flow_lst.append((t * 1e-9,"%d %d %d 100 %d %.9f\n"%(src, dst, low_prio, size, t * 1e-9)))
				#ofile.write("%d %d 3 100 %d %.9f\n"%(src, dst, size, t * 1e-9))
				heapq.heapreplace(host_list, (t + inter_t, src))

	print("without incast: %d" % n_flow)

	#Generate incast
	if int(options.incast):
		#incast size
		avg = 500000
		#sender_num 
		sender_num = 50
		#receiver_num 
		receiver_num = 1
		#nhost_num
		nhost = 64 
		load = 0.02
		#core_link_num
		core_link_num = 32
		avg_inter_arrival = 1/( bandwidth* core_link_num *load/ 8./ (avg* sender_num) )*1000000000
		t = base_t + avg_inter_arrival
		#host_list = [(base_t + int(poisson(avg_inter_arrival)), i) for i in range(receiver_num)]
		#heapq.heapify(host_list)

		while t < time + base_t:
			dst = random.randint(0, nhost-1)
			src_set = set()
			while len(src_set) < sender_num:
				src = random.randint(0, nhost-1)
				if src != dst:
					src_set.add(src)
			size = avg
			if size <= 0:
				size = 1
			for src in src_set:
				n_flow += 1
				#Append flow_tuple (start_time, flow_info)
				if(priority and size < 10000):
					output_flow_lst.append((t * 1e-9,"%d %d %d 200 %d %.9f\n"%(src, dst, high_prio, size, t * 1e-9)))
				else:
					output_flow_lst.append((t * 1e-9,"%d %d %d 200 %d %.9f\n"%(src, dst, low_prio, size, t * 1e-9)))
				#ofile.write("%d %d 3 100 %d %.9f\n"%(src, dst, size, t * 1e-9))

			t += avg_inter_arrival
		
	print("with incast: %d" % n_flow)
	#Ascending sort flows according to start_time
	output_flow_lst = sorted(output_flow_lst, key=lambda tup: tup[0])
	for flow_tuple in output_flow_lst:
		ofile.write(flow_tuple[1])
	
	ofile.seek(0)
	ofile.write("%d"%n_flow)
	ofile.close()

'''
	f_list = []
	avg = customRand.getAvg()
	avg_inter_arrival = 1/(bandwidth*load/8./avg)*1000000000
	# print avg_inter_arrival
	for i in range(nhost):
		t = base_t
		while True:
			inter_t = int(poisson(avg_inter_arrival))
			t += inter_t
			dst = random.randint(0, nhost-1)
			while (dst == i):
				dst = random.randint(0, nhost-1)
			if (t > time + base_t):
				break
			size = int(customRand.rand())
			if size <= 0:
				size = 1
			f_list.append(Flow(i, dst, size, t * 1e-9))

	f_list.sort(key = lambda x: x.t)

	print len(f_list)
	for f in f_list:
		print f
'''
