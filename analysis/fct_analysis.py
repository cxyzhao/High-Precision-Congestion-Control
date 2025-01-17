import subprocess
import argparse



def get_pctl(a, p):
	i = int(len(a) * p)
	return a[i]

if __name__=="__main__":
	parser = argparse.ArgumentParser(description='')
	parser.add_argument('-p', dest='prefix', action='store', default='fct_fat', help="Specify the prefix of the fct file. Usually like fct_<topology>_<trace>")
	parser.add_argument('-s', dest='step', action='store', default='5')
	parser.add_argument('-t', dest='type', action='store', type=int, default=0, help="0: normal, 1: incast, 2: all")
	parser.add_argument('-T', dest='time_limit', action='store', type=int, default=3000000000, help="only consider flows that finish before T")
	parser.add_argument('-b', dest='bw', action='store', type=int, default=25, help="bandwidth of edge link (Gbps)")
	parser.add_argument('-c', dest='single_cc', action='store', default='', help="single cc to parse")
	args = parser.parse_args()

	type = args.type
	time_limit = args.time_limit

	# Please list all the cc (together with parameters) that you want to compare.
	# For example, here we list two CC: 1. HPCC-PINT with utgt=95,AI=50Mbps,pint_log_base=1.05,pint_prob=1; 2. HPCC with utgt=95,ai=50Mbps.
	# For the exact naming, please check ../simulation/mix/fct_*.txt output by the simulation.
	CCs = [
		#'hpccPint95ai50log1.05p1.000',
		# 'hp95ai80',
		# 'dcqcn',
		# 'timely',
		# 'dctcp',
		# 'abc',
		# 'abc_slowUnit'

		# # For AckHigh
		# "abc_1",
		# "abc_0"


		# # For varying d_t
		# 'abcdt0dl64000_1',
		# 'abcdt32000dl64000_1',
		# 'abcdt64000dl64000_1',
		# 'abcdt0dl32000_1',
		# 'abcdt16000dl32000_1',
		# 'abcdt32000dl32000_1',

		# # For 1flow test
		# 'hp95ai80_1',
		# 'dcqcn_1',
		# 'timely_1',
		# 'dctcp_1',
		# 'abcdt32000dl32000_1',
		# 'abc_slowUnitdt32000dl32000_1'

		# For FCT test 
		# 'hp95ai80_1',
		# 'dcqcn_1',
		# 'timely_1',
		# 'dctcp_1',
		# 'abcdt32000dl32000_1',
		# 'abc_slowUnitdt32000dl32000_1'

		#"abc_slowUnitdt64000dl64000token50_1",
	
		# # For 1flow test
		# "abcdt0dl16000token50_1",
		# "abcdt8000dl16000token50_1",
		# "abcdt16000dl16000token50_1",
		# "abcdt0dl32000token50_1",
		# "abcdt16000dl32000token50_1",
		# "abcdt32000dl32000token50_1",
		# "abcdt0dl64000token50_1",
		# "abcdt32000dl64000token50_1",
		# "abcdt64000dl64000token50_1",

		# # For FCT test
		# #  
		# "abcdt0dl16000token30_1",
		# "abcdt0dl16000token50_1",
		# "abcdt0dl16000token70_1",
		# # "abc_slowUnitdt0dl16000token30_1",
		# # "abc_slowUnitdt0dl16000token50_1",
		# # "abc_slowUnitdt0dl16000token70_1"
		# "abcdt32000dl32000token50_1",
		# "abcdt0dl64000token50_1",
		# "abcdt16000dl16000token50_1",
		# "abcdt0dl32000token50_1",
		# "abcdt64000dl64000token50_1",

		#For eta
	#   "abcdt0.0dl6000.0token50eta0.85_1",
	#   "abcdt0.0dl6000.0token50eta0.9_1",
	#   "abcdt0.0dl6000.0token50eta0.95_1",
	#   "abcdt0.0dl6000.0token50eta1.0_1",
	#   "abcdt0.0dl6000.0token50eta1.05_1",
	#   "abcdt0.0dl6000.0token50eta1.1_1",
	#  "abcdt0.0dl16000.0token50eta1.05_1",
	#   "abcdt0.0dl16000.0token50eta1.1_1",
	 # "abcdt0.0dl16000.0token50eta1.0_1",
	#  "abcdt0.0dl32000.0token50eta1.0_1",
	#  "abcdt0.0dl64000.0token50eta1.0_1",
	#"abcdt0.0dl16000.0token50eta1.05_1",
	#"abcdt0.0dl16000.0token50eta1.1_1",

	#	"hp95ai80_1",
		#"abc_slowUnitdt0.0dl8000.0token50eta0.9_1",


		# "hp95ai80",
		# "dcqcn",
		# "timely",
		# # "dctcp",
		# "abcdt0.0dl12000.0token50eta0.95",
		# "aabcdt0.0dl12000.0token50eta0.95",
		# "rabcdt0.0dl12000.0token50eta0.95",

		# "aabcdt0.0dl12000.0token50eta0.95",
		# #"aabcdt0.0dl12000.0token50eta0.95rb",
		# "rabcdt0.0dl12000.0token50eta0.95",
		#"abcdt0.0dl16000.0token50eta0.95_1"

		#  "abcdt0.0dl12000.0token50eta0.7",
		#   "abcdt0.0dl12000.0token50eta0.8",
		#    "abcdt0.0dl12000.0token50eta0.9",
		#     "abcdt0.0dl12000.0token50eta0.95",
		# 	 "abcdt0.0dl12000.0token50eta1.0",
		# 	 "abcdt0.0dl12000.0token50eta1.1",
		# 	 "abcdt0.0dl12000.0token50eta1.2",
		# 	 "abcdt0.0dl12000.0token50eta1.3",

   	#  "aabcdt0.0dl8000.0token50eta0.95",
	#  "aabcdt0.0dl12000.0token50eta0.95",
	#  "aabcdt0.0dl16000.0token50eta0.95",
	#  "aabcdt0.0dl8000.0token50eta0.95rb",
	#  "aabcdt0.0dl12000.0token50eta0.95rb",
	#  "aabcdt0.0dl16000.0token50eta0.95rb",
"hp95ai80",
	"dcqcn", 
           "timely", 
            
           "abcdt0.0dl12000.0token50eta0.95", 
           "aabcdt0.0dl12000.0token50eta0.95",
          "rabcdt0.0dl12000.0token50eta0.95"





		

	]
	if len(args.single_cc) != 0 :
		CCs = [args.single_cc]

	step = int(args.step)
	res = [[i/100.] for i in range(0, 100, step)]
	for cc in CCs:
		#file = "%s_%s.txt"%(args.prefix, cc)
		file = "../simulation/mix/%s_%s.txt"%(args.prefix, cc)
		#file = "../simulation/mix/results_archiv_v1/%s_%s.txt"%(args.prefix, cc)
		if type == 0:
			cmd = "cat %s"%(file)+" | awk '{if ($4==100 && $6+$7<"+"%d"%time_limit+") {slow=$7/$8;print slow<1?1:slow, $5}}' | sort -n -k 2"
			# print cmd
			output = subprocess.check_output(cmd, shell=True)
		elif type == 1:
			cmd = "cat %s"%(file)+" | awk '{if ($4==200 && $6+$7<"+"%d"%time_limit+") {slow=$7/$8;print slow<1?1:slow, $5}}' | sort -n -k 2"
			#print cmd
			output = subprocess.check_output(cmd, shell=True)
		else:
			cmd = "cat %s"%(file)+" | awk '{$6+$7<"+"%d"%time_limit+") {slow=$7/$8;print slow<1?1:slow, $5}}' | sort -n -k 2"
			#print cmd
			output = subprocess.check_output(cmd, shell=True)

		# up to here, `output` should be a string of multiple lines, each line is: fct, size
		a = output.split('\n')[:-2]
		n = len(a)
		for i in range(0,100,step):
			l = i * n / 100
			r = (i+step) * n / 100
			d = map(lambda x: [float(x.split()[0]), int(x.split()[1])], a[l:r])
			fct=sorted(map(lambda x: x[0], d))
			#print(i, step, i/step, len(res))
			res[i/step].append(d[-1][1]) # flow size
			#res[i/step].append(sum(fct) / len(fct)) # avg fct
			res[i/step].append(get_pctl(fct, 0.5)) # mid fct
			res[i/step].append(get_pctl(fct, 0.95)) # 95-pct fct
			res[i/step].append(get_pctl(fct, 0.99)) # 99-pct fct
	
		a = output.split('\n')[:-1]
		n_1pkt_flow = 1
		fct_1pkt_flow = 1.0
		fct_longflow = 1.0
		long_flow_bytes = 125000000
		for line in a:
			if int(line.split()[1]) == 100:
				n_1pkt_flow += 1
				fct_1pkt_flow += float(line.split()[0])
			if int(line.split()[1]) == long_flow_bytes:
				fct_longflow = float(line.split()[0])
		
		q_delay  = (fct_1pkt_flow / n_1pkt_flow * 4171 - 4171) / 1000 #us
		Long_flow_t  = fct_longflow * 10484160 / 1000000000 #s
		lflow_bw = long_flow_bytes * 8 / 1000000000.0 / Long_flow_t
		print("%s %.2f %.2f" % (cc, q_delay, lflow_bw))
	
	print("FCT")
	for item in res:
		#line = "%.3f %d"%(item[0], item[1])
		line = "%d,"%(item[1])
		i = 1
		for cc in CCs:
			line += "%.3f,%.3f,%.3f,"%(item[i+1], item[i+2], item[i+3])
			#line += "%.3f,%.3f,"%(item[i+2], item[i+3])
			i += 4
		print line

	


