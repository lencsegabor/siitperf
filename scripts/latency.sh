#!/bin/bash
#Parameters
impl="p094-10G-p095-Jool-SF-latency" # name of the tested implementation (used for logging)
dir="b" # valid values: b,f,r; b: bidirectional, f: forward (Left to Right, 6 --> 4), r: reverse (Right to Left, 6 <-- 4) 
r=426576 # frame rate determined by thropughput measurement
fs=84 # IPv6 frame size; IPv4 frame size is always 20 bytes less 
xpts=120 # duration (in seconds) of an experiment instance
to=2000 # timeout in milliseconds
n=2 # foreground traffic, if ( frame_counter % n < m ) 
m=2 # E.g. n=m=2 is all foreground traffic; n=2,m=0 is all background traffic; n=10,m=9 is 90% fg and 10% bg
delay=60 # delay before the insertion of the first identifying tag
tags=50000 # number of identifying tags
sleept=10 # sleeping time between the experiments
no_exp=20 # number of experiments
res_dir="results" # base directory for the results (they will be copied there at the end)

############################

# Generate log/csv file headers
echo "#############################" > ratetest.log
echo "Tested Implementation: $impl" >> ratetest.log
echo "Frame size: $fs:" >> ratetest.log
echo "Direction $dir:" >> ratetest.log
echo "Value of n: $n" >> ratetest.log
echo "Value of m: $m" >> ratetest.log
echo "Duration (sec): $xpts" >> ratetest.log
echo "Frame rate (fps): $r" >> ratetest.log
echo "Timeout value (sec): $to"  >> ratetest.log
echo "Delay before the first tag: $delay"  >> ratetest.log
echo "Number of identifying tags: $tags"  >> ratetest.log
echo "Sleep Time (sec): $sleept" >> ratetest.log
date +'Date&Time: %Y-%m-%d %H:%M:%S.%N' >> ratetest.log
echo "#############################" >> ratetest.log

if [ "$dir" == "b" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Delay, Tags, Fwd-TL, Fwd-WCL, Rev-TL, Rev-WCL" > rate.csv
fi
if [ "$dir" == "f" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Delay, Tags, Fwd-TL, Fwd-WCL" > rate.csv
fi
if [ "$dir" == "r" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Delay, Tags, Rev-TL, Rev-WCL" > rate.csv
fi


# Execute $no_exp number of experiments
for (( N=1; N <= $no_exp; N++ ))
do
	echo "Exectuting experiment #$N..."
	echo "Exectuting experiment #$N..." >> ratetest.log
	echo "Command line is: ./build/siitperf-lat $fs $r $xpts $to $n $m $delay $tags"
	echo "Command line is: ./build/siitperf-lat $fs $r $xpts $to $n $m $delay $tags" >> ratetest.log
	# Execute the test program
	./build/siitperf-lat $fs $r $xpts $to $n $m $delay $tags > temp.out 2>&1
	# Log and print out info
	cat temp.out >> ratetest.log
	cat temp.out | tail
	# Check for any errors
	error=$(grep 'Error:' temp.out)
        if [ -n "$error" ]; then
		echo "Error occurred, testing must stop."
		exit -1;
	fi
	# Collect and evaluate the results (depending on the direction of the test)
	if [ "$dir" == "b" ]; then
		fwd_TL=$(grep 'Forward TL' temp.out | awk '{print $3}')
		fwd_WCL=$(grep 'Forward WCL' temp.out | awk '{print $3}')
		rev_TL=$(grep 'Reverse TL' temp.out | awk '{print $3}')
		rev_WCL=$(grep 'Reverse WCL' temp.out | awk '{print $3}')
	echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $delay, $tags, $fwd_TL, $fwd_WCL, $rev_TL, $rev_WCL" >> rate.csv
	fi
	if [ "$dir" == "f" ]; then
                fwd_TL=$(grep 'Forward TL' temp.out | awk '{print $3}')
                fwd_WCL=$(grep 'Forward WCL' temp.out | awk '{print $3}')
        echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $delay, $tags, $fwd_TL, $fwd_WCL" >> rate.csv
	fi
	if [ "$dir" == "r" ]; then
                rev_TL=$(grep 'Reverse TL' temp.out | awk '{print $3}')
                rev_WCL=$(grep 'Reverse WCL' temp.out | awk '{print $3}')
        echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $delay, $tags, $rev_TL, $rev_WCL" >> rate.csv
	fi
	echo "Sleeping for $sleept seconds..."
	echo "Sleeping for $sleept seconds..." >> ratetest.log
	# Sleep for $sleept time to give DUT a chance to relax
	sleep $sleept 
	rm temp*
done # (end of the $no_exp number of experiments)
# Save the results (may be commented out if not needed)
dirname="$res_dir/$(date +$impl'-'$dir'-'$fs'-'$n'-'$m'-'$xpts'-'$r'-'$to'-'$delay'-'$tags'-%F-%H%M')"
mkdir -p $dirname
mv ratetest.log $dirname/
mv rate.csv $dirname/
mv nohup.out $dirname/ 
cp -a siitperf.conf $dirname/
