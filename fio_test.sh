#!/bin/bash
# Copyright (c) 2025 MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.


GDS_TEST_DIR=/path/to/tmp
TEST_LOG_DIR=/path/to/tmp


cmd_lines=(
"fio --name=test --ioengine=psync --direct=1 --bs=1M --size=100M --readwrite=write --ramp_time=4 --numjobs=1 --end_fsync=1 --group_reporting"
#"fio --name=test --ioengine=psync --direct=1 --bs=1M --size=4G --readwrite=read --ramp_time=4 --numjobs=1 --end_fsync=1  --group_reporting"
)


function change_to_MiB() {
	local bth=$1
	
	if [[ $bth == *"GiB/s"* ]]; then
		bth_gi_str=${bth::-5}
		bth=$(echo "$bth_gi_str * 1024" | bc -l)
	else
		bth=${bth::-5}
	fi

	echo $bth
}

function get_avg() {
	local num_arr=("$@")
	count=${#num_arr[@]}
	cmd_line="(0"
	for i in "${num_arr[@]}"; do
		cmd_line="${cmd_line}+${i}"
	done

	cmd_line="${cmd_line}) / ${count}"
	#echo "+++++++++："$cmd_line
	
	ret=$(echo "$cmd_line" | bc -l)
	echo $ret
}

function avg_run() {
	local cmd=$1

	lat_arr=(0 0 0 0 0)
	iops_arr=(0 0 0 0 0)
	bth_arr=(0 0 0 0 0)
	for i in {0..4}; do
		eval "$cmd"
		cat ${TEST_LOG_DIR}log.txt
		lat=$(grep -v 'clat' log.txt | grep 'lat (' | grep 'avg=' | awk -F ',' '{print $3}' | awk -F '=' '{print $2}')
		iops=$(grep 'IOPS=' log.txt | awk -F ' ' '{print $2}' | awk -F '=' '{print $2}')
		iops=${iops::-1}
		bth=$(grep 'bw='    log.txt | awk -F ' ' '{print $2}' | awk -F '=' '{print $2}')
		bth_mb=$(change_to_MiB $bth)
		echo "get fio result:${bth}, ${lat}, ${iops}"

		if [ "$lat" == "" ];then
			lat=0
		fi
		lat_arr[$i]=$lat
		iops_arr[$i]=$iops
		bth_arr[$i]=$bth_mb
	done
	bth_ret=$(get_avg ${bth_arr[@]})
	lat_ret=$(get_avg ${lat_arr[@]})
	iops_ret=$(get_avg ${iops_arr[@]})

	echo ${bth_arr[@]}
	echo ${lat_arr[@]}
	echo ${iops_arr[@]}
	
	
	echo "[AVAERAGE_RESULT:]${bth_ret}, ${lat_ret}, ${iops_ret}"
}


for cmd in "${cmd_lines[@]}";
do
	cmd="${cmd} --directory=${GDS_TEST_DIR} > ${TEST_LOG_DIR}log.txt"
	eval "$cmd"  # warm up

	avg_run "$cmd"
	echo "++++"

done



