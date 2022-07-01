#!/bin/bash
# Test: Connection tracking table capacity measurement
# Parameters
# typical parameters:
C0=1000000 # beginning safe value for the size of the connection tracking table
alpha=1 # safety factor for determining the validation rate
beta=2/10 # Unacceptable drop of R during exponential search
gamma=4/10  # Unacceptable drop of R during final binary search
E=1 # error of the binary search for the size of the connection tracking table
max=4000000 # upper bound for connection establishment rate measurement
# iptables specific parameters:
conntrack=22 # conntrack table size: 2^${conntrack}
timeout=10000
x=1 # hash table size: 2^${conntrack}/$x
impl="p094-p095-f16c-hcp${x}-iptables-ct22-Sf1-Ep4-8000x1000-CTTC" # name of the tested implementation (used for logging)
T=500 # global timeout for preliminary phase
D=0 # delay caused by the preliminary phase (will be computed)
DUT_IP=172.16.28.95
startcmd="ssh ${DUT_IP} /root/DUT-settings/set-iptables-hcpx $conntrack $x $timeout"
stopcmd="ssh ${DUT_IP} /root/DUT-settings/del-iptables"
countcmd="ssh ${DUT_IP} cat /proc/sys/net/netfilter/nf_conntrack_count"
rebootcmd="ssh ${DUT_IP} /sbin/reboot"
rebootsleept=180
rsscmd="ssh ${DUT_IP} /root/DUT-settings/eth-sdfn4"
lladdrcmd="ssh ${DUT_IP} /root/DUT-settings/set-arp1"
# general parameters:
fs=84 # IPv6 frame size; IPv4 frame size is always 20 bytes less 
to=2000 # timeout in milliseconds
n=2 # foreground traffic, if ( frame_counter % n < m ) 
m=2 # E.g. n=m=2 is all foreground traffic; n=2,m=0 is all background traffic; n=10,m=9 is 90% fg and 10% bg
sleept=10 # sleeping time between the experiments
e=100000 # measurement error: the difference betwen the values of the higher and the lower bound of the binary search, when finishing
no_exp=10 # number of experiments
res_dir="results" # base directory for the results (they will be copied there at the end)

function restartdut() {
	echo "Restarting the DUT..."
	$rebootcmd
	sleep $rebootsleept
	ping -c 1 ${DUT_IP}
	$rsscmd
	$lladdrcmd
}

############################

# Generate log/csv file headers
echo "#############################" > ratetest.log
echo "Tested Implementation: $impl" >> ratetest.log
echo "C0: $C0" >> ratetest.log
echo "T: $T" >> ratetest.log
echo "D: $D" >> ratetest.log
echo "Frame size: $fs:" >> ratetest.log
echo "Value of n: $n" >> ratetest.log
echo "Value of m: $m" >> ratetest.log
echo "Upper bound for the R rate (QPS): $max" >> ratetest.log
echo "Value of alpha: $alpha" >> ratetest.log
echo "Value of beta: $beta" >> ratetest.log
echo "Value of gamma: $gamma" >> ratetest.log
echo "Timeout value (sec): $to"  >> ratetest.log
echo "Error for rate (e): $e"  >> ratetest.log
echo "Error for capacity (E): $E"  >> ratetest.log
echo "Sleep Time (sec): $sleept" >> ratetest.log
date +'Date&Time: %Y-%m-%d %H:%M:%S.%N' >> ratetest.log
echo "#############################" >> ratetest.log
echo "No, ExpIt, C0, R0, CS, RS, CT, RT, FinBinIt, C" > rate.csv


function binary_search_for_maximum_connection_establishment_rate() {

	# Execute a binary search in the [0, $2] interval using $1 number of connections
	# At the end, the value of 'R' contains the result

	C=$1 # The number of connections
	l=0  # Initialize lower bound to 0
	h=$2 # Initialize higher bound with maximum rate

	N=$C # set the current values for N
	M=$C # set the current values for M

	R=0 # It will be returned, if all tests fail
	goon=1; # Will be set to zero for an optimized early exit

	for (( i=1; ( $((h-l)) > $e ) && $goon; i++ ))
	do 
		# Log some information about this step
		echo ---------------------------------------------------------- >> ratetest.log
		date +'%Y-%m-%d %H:%M:%S.%N Iteration no. '"${No}"'-'"$I"'-'"$i" >> ratetest.log 
		echo ----------------------------------------------------------  >> ratetest.log
		echo "The value of stoprate is: $stoprate" >> ratetest.log
		echo "The value of goon is: $goon" >> ratetest.log
		echo "Starting iptables on DUT with command: $startcmd"
		$startcmd 
		r=$(((h+l)/2)) # initialize the test rate with (h+l)/2
                vr=$((r*$alpha)); # calculate validation rate
                duration=$(((N/vr)+1)); # there is some extra time
                echo "Testing R rate: $r fps, validation rate is: $vr fps."
                echo "Testing R rate: $r fps, validation rate is: $vr fps." >> ratetest.log
                D=$((1000*N/r+2*T))
                echo "Command line is: ./build/siitperf-tp $fs $vr $duration $to $n $m $N $M $r $T $D"
		# Execute the test program
                ./build/siitperf-tp $fs $vr $duration $to $n $m $N $M $r $T $D > temp.out 2>&1
		# Log and print out info
		cat temp.out >> ratetest.log
		cat temp.out | tail
                echo "Right after the test DUT's nf_conntrack_count is:" $($countcmd)
                echo "Stopping iptables on DUT with command: $stopcmd"
                $stopcmd
                echo "DUT's nf_conntrack_count was cleared to:" $($countcmd)
		# Check for any errors
		error=$(grep 'Error:' temp.out)
                if [ -n "$error" ]; then
			failedtofill=$(grep 'Failed to fill state table' temp.out)
			if [ -n "$failedtofill" ]; then
				echo "Failed to fill state table: therefore, this iteration is failed"
				echo "Failed to fill state table: therefore, this iteration is failed" >> ratetest.log
			else
				echo "Error occurred, testing must stop."
				exit -1;
			fi
		fi
		# Collect and evaluate the results 
                pre_rec=$(grep 'Preliminary frames received:' temp.out | awk '{print $4}')
                echo Preliminary: $fwd_rec frames were received from the required $N frames
                echo Preliminary: $fwd_rec frames were received from the required $N frames >> ratetest.log
                if [ $pre_rec -eq $N ]; then
                        echo Preliminary phase PASSED
                        echo Preliminary phase PASSED >> ratetest.log
                        # Check validation results
                        val_snt=$(grep 'Reverse frames sent:' temp.out | awk '{print $4}')
                        val_rec=$(grep 'Reverse frames received:' temp.out | awk '{print $4}')
                        if [ $val_rec -eq $val_snt ]; then
                                echo Validation PASSED
                                echo Validation PASSED >> ratetest.log
                                l=$r; # We we choose the upper half interval
				R=$r
                        else
                                echo Validation FAILED
                                echo Validation FAILED >> ratetest.log
                                h=$r; # We we choose the lower half interval
				echo "r: $r, stoprate: $stoprate" >> ratetest.log
				if [ $r -lt $stoprate ]; then
					echo "As the current $r rate is under the $stoprate stoprate, binary search will exit." >> ratetest.log
					goon=0;
				fi

                        fi
                else
                        echo Preliminary phase FAILED
                        echo Preliminary phase FAILED >> ratetest.log
                        h=$r # We we choose the lower half interval
			echo "r: $r, stoprate: $stoprate" >> ratetest.log
			if [ $r -lt $stoprate ]; then
				echo "As the current $r rate is under the $stoprate stoprate, binary search will exit." >> ratetest.log
				goon=0;
			fi
                fi
		# Some further logging (and preparing results)
		echo "New diff: $((h-l))"
		echo "New diff: $((h-l))" >> ratetest.log
		echo "Sleeping for $sleept seconds..."
		echo "Sleeping for $sleept seconds..." >> ratetest.log
		# Sleep for $sleept time to give DUT a chance to relax
		sleep $sleept 
	done # (end of the binary search)
}


# Execute $no_exp number of experiments
for (( No=1; No <= $no_exp; No++ ))
do
	restartdut # restart and pre-initialize DUT

	# Exponential search for finding the order of magnitude of the
	# connection tracking table capacity
	# Variables:
	#   C0 and R0 are beginning safe values for connection tracking table size
	#     and connection establishment rate, respectively
	#   CS and RS are their currently used safe values 
	#   CT and RT are the values for current examination
	#   beta is a factor expressing unacceptable drop of R (e.g. beta=0.1)

	# R0=binary_search_for_maximum_connection_establishment_rate(C0,max);
	echo "Executing an initial binary search for R0 using C0=$C0" >> ratetest.log
	I="measure-RS" # just for logging
	stoprate=e; # so that stoprate has some value (not really used here)
	binary_search_for_maximum_connection_establishment_rate $C0 $max
	R0=$R; 
	echo "The initial binary search is ready." >> ratetest.log
	echo "At the beginning of the exponential search:" >> ratetest.log
	echo "CS=C0: $C0" >> ratetest.log
	echo "RS=R0: $R0" >> ratetest.log

	# Do the exponential search
	for (( CS=$C0, RS=$R0, I=1; 1; CS=$CT, RS=$RT, I++ ))
	do

                # Log some information about this step
                echo ---------------------------------------------------------------------- >> ratetest.log
                date +'%Y-%m-%d %H:%M:%S.%N Exponential search iteration no. '"${No}"'-'"$I" >> ratetest.log
                echo ----------------------------------------------------------------------  >> ratetest.log

		CT=$((2*CS));
		echo "Exponential search now tests:" >> ratetest.log
		echo "CT: $CT" >> ratetest.log
		echo "RS: $RS" >> ratetest.log
		# RT=binary_search_for_maximum_connection_establishment_rate(CT,RS);
		stoprate=$(($RS*$beta)) # the lowest acceptable rate
		binary_search_for_maximum_connection_establishment_rate $CT $RS
		RT=$R;
		echo "Exponential search decision is made on the basis of:" >> ratetest.log
		echo "RT: $RT" >> ratetest.log
		echo "RS: $RS" >> ratetest.log
		echo "beta: $beta" >> ratetest.log
		echo "RS*beta: $stoprate" >> ratetest.log

		if [ $RT -lt $stoprate ]; then
			echo "Exponential search is now FINISHED."  >> ratetest.log
			break;
		else
			echo "Exponential search is to be continued on." >> ratetest.log
		fi
	done
	# here the size of the connection tracking table is between CS and CT
        echo "THE RESULT OF THE EXPONENTIAL SEARCH IS: $CS < C < $CT" >> ratetest.log

	# Collect results
	expresult=$(echo "$No, $I, $C0, $R0, $CS, $RS, $CT, $RT");

	# This is a binary search for finding the C connection tracking table
	# capacity within E error
	# Variables:
	#   CS and RS are the safe values for connection tracking table size
	#     and connection establishment rate, respectively
	#   C and R are the values for current examination
	#   gamma is a factor expressing unacceptable drop of R
	#     (e.g. gamma=0.5)
	for (( d=$((CT-CS)), J=1;  $d>$E; d=$((CT-CS)), J++ ))
	do
                # Log some information about this step
                echo ------------------------------------------------------------------------ >> ratetest.log
                date +'%Y-%m-%d %H:%M:%S.%N Final binary search iteration no. '"${No}"'-'"$J" >> ratetest.log
                echo ------------------------------------------------------------------------  >> ratetest.log

		# restartdut # restart and pre-initialize DUT

		C=$(((CS+CT)/2))
		echo "Final binary search starting values: CS: $CS, C: $C, CT: $CT, d: $d, J: $J"  >> ratetest.log
		# R=binary_search_for_maximum_connection_establishment_rate(C,RS);
		I="F-bin-$J"; # Only for logging
                stoprate=$(($RS*$gamma)) # the lowest acceptable rate
		binary_search_for_maximum_connection_establishment_rate $C $RS
                echo "Final binary search decision is made on the basis of:" >> ratetest.log
                echo "R: $R" >> ratetest.log
                echo "RS: $RS" >> ratetest.log
                echo "gamma: $gamma" >> ratetest.log
                echo "RS*gamma: $stoprate" >> ratetest.log

                if [ $R -lt $stoprate ]; then
			CT=$C; # take the lower half of the interval
			echo "Final binary search: LOWER half interval is taken." >> ratetest.log
			echo "Final biary search new values: CS: $CS, C: $C"  >> ratetest.log
		else
			CS=$C; RS=$R; # take the upper half of the interval
			echo "Final binary search: UPPER half interval is taken." >> ratetest.log
			echo "Final biary search new values: CS: $CS, C: $C"  >> ratetest.log
		fi
	done
	# here the size of the connection tracking table is CS within E error
	echo "Final binary search is finished with RESULT CS=$CS within $E error." >> ratetest.log
	# Record results
	echo "$expresult, $J, $CS" >> rate.csv
 
	rm temp*
done # (end of the $no_exp number of experiments)
# Save the results (may be commented out if not needed)
dirname="$res_dir/$(date +$impl'-'$fs'-'$n'-'$m'-'$N'-'$N'-'$T'-'$D'-'$e'-%F-%H%M')"
mkdir -p $dirname
mv ratetest.log $dirname/
mv rate.csv $dirname/
mv nohup.out $dirname/ 
cp -a siitperf.conf $dirname/
