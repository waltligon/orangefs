#!/bin/bash

check_instance() {

	#$1 = ipaddress
	
	ssh -i $KEYFILE ${VMUSER}@$1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no true 2> /dev/null

	#is SSH responding?
	until [ $? -eq 0 ]
	do
		sleep 10
		ssh -i $KEYFILE ${VMUSER}@$1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no true 2> /dev/null
	done
}

generate_instances() {

	# Parameter $1 number of instances
	echo "Generating $1 new instances of $VMSYSTEM"
	echo "euca-run-instances -n $1 --config ${EC2CONFFILE} --debug -k $KEYNAME -t c1.small $VMSYSTEM > newinstance.out 2> newinstance.err"

	euca-run-instances -n $1 --config ${EC2CONFFILE} --debug -k $KEYNAME -t c1.small $VMSYSTEM > newinstance.out 2> newinstance.err 

	if [ $? -ne 0 ]
	then
		echo "Could not create instance of $VMSYSTEM. Exiting"
		exit 1
	fi
	
	VMINSTANCEARR=( $(cat newinstance.out | grep INSTANCE | awk '{print $2}') )
	
	
	# primary instance is first instance
	VMINSTANCEID=${VMINSTANCEARR[0]}
	
	#wait 20 seconds to start instance
	sleep 20

	# now verify the instance is running
	for i in ${VMINSTANCEARR[@]}
	do
		echo "VM Instance of $VMSYSTEM requested. Instance id is ${i}"
	
		RUNNING=`euca-describe-instances instance-id=${i} --config ${EC2CONFFILE} | grep INSTANCE | awk '{ print $6 }'`
	
		until [ "$RUNNING" == "running" ]
		do
			RUNNING=`euca-describe-instances instance-id=${i} --config ${EC2CONFFILE} | grep INSTANCE | awk '{ print $6 }'`
			sleep 10
			echo "Instance ${i} is ${RUNNING}"
			if [ $RUNNING == "error" ]
			then
				echo "Error in creating ${i}. Bailing out."
				exit 1
			fi
		done
	done
	

}



prepare_instance() {
	
	# $1 = ipaddress
	# install all system updates and reboot
	check_instance $i
	
	#upload keyfile to instance - will need later
	rsync -a -e "ssh -i ${KEYFILE} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no" ${KEYFILE} ${VMUSER}@${i}:/home/${VMUSER}/
	
	#update the instance
	ssh -i ${KEYFILE} ${VMUSER}@$1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "VMSYSTEM=${VMSYSTEM} bash -s" < update-cloud.sh 

	check_instance $i

	ssh -i ${KEYFILE} ${VMUSER}@$1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no 'echo System rebooted. Test system is `uname -a`' 

	echo "Preparing the image for testing..."
	echo "ssh -i ${KEYFILE} ${VMUSER}@$1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \"VMSYSTEM=${VMSYSTEM} bash -s\" < prepare-cloud.sh "
	
	ssh -i ${KEYFILE} ${VMUSER}@$1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "VMSYSTEM=${VMSYSTEM} bash -s" < prepare-cloud.sh 
	
}