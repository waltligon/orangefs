#!/bin/bash

check_instance() {
	ssh -i $KEYFILE ${VMUSER}@${VMIPADDR} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no true 2> /dev/null

	#is SSH responding?
	until [ $? -eq 0 ]
	do
		sleep 10
		ssh -i $KEYFILE ${VMUSER}@${VMIPADDR} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no true 2> /dev/null
	done
}

generate_instances() {

	# Parameter $1 number of instances
	echo "Generating new instance of $VMSYSTEM"
	echo "euca-run-instances -n $1 --config ${EC2CONFFILE} --debug -k $KEYNAME -t c1.small $VMSYSTEM > newinstance.out 2> newinstance.err"

	euca-run-instances -n $1 --config ${EC2CONFFILE} --debug -k $KEYNAME -t c1.small $VMSYSTEM > newinstance.out 2> newinstance.err 

	if [ $? -ne 0 ]
	then
		echo "Could not create instance of $VMSYSTEM. Exiting"
		exit 1
	fi
	
	cat newinstance.out | grep INSTANCE | awk '{print $2}' > instanceids.txt
	
	
	# for each instance in instance id
	
	VMINSTANCEID=`cat newinstance.out | grep INSTANCE | awk '{print $2}'`

	echo "VM Instance of $VMSYSTEM created. Instance id is ${VMINSTANCEID}"
	#wait 20 seconds to start instance
	sleep 20

	RUNNING=`euca-describe-instances instance-id=${VMINSTANCEID} --config ${EC2CONFFILE} | grep INSTANCE | awk '{ print $6 }'`
	
	until [ "$RUNNING" == "running" ]
	do
		RUNNING=`euca-describe-instances instance-id=${VMINSTANCEID} --config ${EC2CONFFILE} | grep INSTANCE | awk '{ print $6 }'`
		sleep 10
		echo "Instance ${VMINSTANCEID} is ${RUNNING}"
		if [ $RUNNING == "error" ]
		then
			echo "Error in creating ${VMINSTANCEID}. Bailing out."
			exit 1
		fi
	done
	
	#Now grab the IP Address of the new instance
	VMIPADDR=`euca-describe-instances instance-id=${VMINSTANCEID} --config ${EC2CONFFILE} | grep INSTANCE | awk '{ print $13 }'`


	echo "VM IP ADDRESS is ${VMIPADDR}"

	VFS_HOSTS=$VMIPADDR

	echo "Removing obsolete ssh host records at ~/.ssh/known_hosts for $VMIPADDR."
	ssh-keygen -f ~/.ssh/known_hosts -R ${VMIPADDR}
	
	check_instance
}



prepare_instance() {
	# install all system updates and reboot
	ssh -i ${KEYFILE} ${VMUSER}@${VMIPADDR} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "VMSYSTEM=${VMSYSTEM} bash -s" < update-cloud.sh 

	check_instance

	ssh -i ${KEYFILE} ${VMUSER}@${VMIPADDR} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no 'echo System rebooted. Test system is `uname -a`' 

	echo "Preparing the image for testing..."
	ssh -i ${KEYFILE} ${VMUSER}@${VMIPADDR} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "VMSYSTEM=${VMSYSTEM} bash -s" < prepare-cloud.sh 
}