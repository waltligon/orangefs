#!/bin/sh

host="localhost"
port=3334
rootdir=$1
srcdir=$rootdir/pvfs2
builddir=$rootdir/BUILD-pvfs2
installdir=$rootdir/INSTALL-pvfs2

if [ $PVFSPORT ] ; then
    port=$PVFSPORT
fi

#load the module
insmod_kmmod() {
  sudo /sbin/insmod $installdir/sbin/pvfs2.ko
  if [ $? != 0 ] ; then
    echo "Error loading the kernel module. Aborting."
    exit 1
  else
    echo "Kernel module loaded."
  fi
}
#unload the module
rmmod_kmmod() {
  sleep 3
  sudo /sbin/rmmod pvfs2
  if [ $? != 0 ] ; then
    echo "Error removing the kernel module. Aborting."
    exit 1
  else
    echo "Kernel module unloaded."
  fi
}

#mount the pvfs2 filesystem for use by the kernel module
mount_fs() {
  mkdir -p $installdir/mnt/pvfs2
  sudo /bin/mount -t pvfs2 tcp://$host:$port/pvfs2-fs $installdir/mnt/pvfs2
  if [ $? != 0 ] ; then
    echo "Error mounting the pvfs2 filesystem. Aborting."
    exit 1
  else
    echo "PVFS2 Filesystem mounted correctly on $installdir/mnt/pvfs2"
  fi 
}
#unmount the pvfs2 filesystem
umount_fs() {
  sudo umount $installdir/mnt/pvfs2
  if [ $? != 0 ] ; then
    echo "Error unmounting the pvfs2 filesystem. Aborting."
    exit 1
  else
    echo "PVFS2 Filesystem unmounted."
  fi 
}

#start the pvfs2 client
start_client() {
  echo "$installdir/sbin/pvfs2-client -p $installdir/sbin/pvfs2-client-core" > /tmp/pvfs2-client-launcher.$USER
  chmod u+x /tmp/pvfs2-client-launcher.$USER
  sudo /tmp/pvfs2-client-launcher.$USER
  if [ $? != 0 ] ; then
    echo "Error starting client. Aborting."
    exit 1
  else
    echo "PVFS2 client started."
  fi
}
#stop the pvfs2 client
stop_client() {
  pvfs2_client_pid=`ps aux | awk '/pvfs2-client/ && !/awk/ {print $2}'`
  sudo /bin/kill $pvfs2_client_pid
  if [ $? != 0 ] ; then
    echo "Error shutting down the PVFS2 client."
    exit 1
  else
    echo "PVFS2 client shutdown."
  fi
}

# See how we were called.
case "$2" in
  start)
    # load the kernel module
    insmod_kmmod || exit 1
    #start the server
    start_client || exit 1
    #mount the pvfs2 filesystem
    mount_fs || exit 1
  ;;
  stop)
    #unmount the pvfs2 filesystem
    umount_fs || exit 1
    #stop the client
    stop_client || exit 1
    #remove the kernel module
    rmmod_kmmod || exit 1
  ;;
  restart)
    $0 $1 stop
    $0 $1 start
  ;;
  *)
    echo "Usage: $0 <PVFS2 install dir> {start|stop|restart}"
    exit 1
esac
