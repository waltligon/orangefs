#!/bin/bash

#arguments will come from calling program
#runalltests [-v][-m][-d][-a IPADDRESS][-s SVNBRANCH][-i IMAGE][-c EC2CONFFILE][-k KEYNAME][-f KEYFILE]

# subroutine for generating instances.

# euca create instance
# verify instance is running
# return ip address of instance.




DATE=`date -u +'%Y%m%d'`
DATETIME=`date -u +'%Y%m%d-%H.%M.%S'`
#export VMUSER=ec2-user
VMUSER=ubuntu
#export VMSYSTEM=sl6-mpi
VMSYSTEM=

NEWINSTANCE=1


while getopts i:a:hc:n:k:f:t: arguments
do
	case $arguments in
	h)
		echo "This script creates prepared instances (with all updates and prereqs) for Orange FS"
		echo "cpi.sh [-n NUMBER_INSTANCES][-i IMAGE][-t FLAVOR][-c EC2CONFFILE][-k KEYNAME][-f KEYFILE]"
		echo ""
		echo "Examples: "
		echo ""
		echo "cpi.sh -n 2 -i cloud-ubuntu-12.04 -t c1.small -c ~/ec2rc.sh -k JamesB -f ~/jamesb.pem"
		echo ""
		
		;;
	i)
		VMSYSTEM=$OPTARG
		;;
	c)
		EC2CONFFILE=$OPTARG
		;;
	k)	
		KEYNAME=$OPTARG
		;;
	f)	
		KEYFILE=$OPTARG
		;;
	n)
		NUMBER_INSTANCES=$OPTARG
		;;
	t)
		VMTYPE=$OPTARG
		;;
	esac
done
	
#the parameters will change, but the basic functionality will not
echo "DATE is ${DATE}"
echo "SVNBRANCH is ${SVNBRANCH}"
echo "NEWINSTANCE is ${NEWINSTANCE}"
echo "EC2CONFFILE is $EC2CONFFILE"
echo "KEYNAME is $KEYNAME"
echo "VMSYSTEM is $VMSYSTEM"
echo "VMTTYPE  is $VMTYPE"
echo "NUMBER_INSTANCES is $NUMBER_INSTANCES"

echo "RUN_MPI_TEST is $RUN_MPI_TEST"
echo "RUN_VFS_TEST is $RUN_VFS_TEST"
echo "VMIPADDR is $VMIPADDR"
echo "DELETE_INSTANCE is $DELETE_INSTANCE"


export SVNBRANCH
export NEWINSTANCE
export VMSYSTEM

export RUN_VFS_TEST
export RUN_MPI_TEST
export KEYFILE
export KEYNAME
export EC2CONFFILE
export DELETE_INSTANCE

#Determine VM User from system
case $VMSYSTEM in 

	cloud-*buntu*|cloud-*mint*)
		VMUSER=ubuntu
		;;
	cloud-rhel*|cloud-centos*|cloud-sl6*|cloud-fedora*|cloud-*suse)
		VMUSER=ec2-user
		;;
	*)
		echo "System $VMSYSTEM not supported."
		exit 1
esac

echo "VMUSER is $VMUSER"
export VMUSER


SVNBRANCH_SHORT=`echo $SVNBRANCH | awk -F"/" '{print $NF}'`

#Look for configuration file
if [ ! -f ${EC2CONFFILE} ] ; then 
	echo "EC2 Configuration file $EC2CONFFILE not found."
	exit 1
fi

#Look for Keyfile
if [ ! -f ${KEYFILE} ] ; then 
	echo "KEYFILE $KEYFILE not found."
	exit 1
fi

#test keypairs
euca-describe-keypairs --config=${EC2CONFFILE} --filter key-name=$KEYNAME

if [ $? != 0 ]
then
	echo "Keyname $KEYNAME not found."
	exit 1
fi

#here are the parameters. Hard coded for now, but will change

#
. ./instancefunctions.sh

#create the VM
#exit 0
if [ ${NEWINSTANCE} != 0 ]
then
	generate_instances $NUMBER_INSTANCES $VMTYPE
	
fi
#Now grab the IP Address of the new instance
VMIPADDRARR=( $(for i in ${VMINSTANCEARR[@]}; do euca-describe-instances instance-id=$i --config ${EC2CONFFILE}; done | grep INSTANCE | awk '{ print $13 }') )
VMINSTANCENAMEARR=( $(for i in ${VMINSTANCEARR[@]}; do euca-describe-instances instance-id=$i --config ${EC2CONFFILE}; done | grep INSTANCE | awk '{ print $4 }') ) 

VMIPADDR=${VMIPADDRARR[0]}
	
echo "VM IP ADDRESS is ${VMIPADDR}"

VFS_HOSTS=`echo ${VMINSTANCENAMEARR[@]}`

echo "VFS_HOSTS are $VFS_HOSTS"

for i in ${VMIPADDRARR[@]}
do
	echo "Removing obsolete ssh host records at ~/.ssh/known_hosts for $i."
	ssh-keygen -f ~/.ssh/known_hosts -R $i
	
	# prepare each instance on a separate process.
	prepare_instance $i &

done
wait

for i in ${VMIPADDRARR[@]}
do

	#Verify all instances are running before continuing.
	check_instance $i

done
