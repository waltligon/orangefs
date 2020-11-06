+++
title= "Linux Kernel Module"
weight=320
+++

The Kernel Module enables access to OrangeFS through the native Linux IO interfaces.  Starting with the Linux Kernel verwsion 4.6, OrangeFS is available natively in the kernel source.  The code that is upstream is recommended for kernel IO, the OrangeFS team has focused on improving the up stream module for several years thus it is recommended to use this version vs. the one that is in the orangefs repo.

| Linux Kernel Version | Features |
|---|---|
| 4.6 | First release of the Linux Kernel with OrangeFS kmod included |
| 4.9 | Read Cache Integrated |
| 5.2 | Full Read and Write Linux page cache integration, significantly improved small IO performance |

If you are using the Upstream Linux Kernel Module, which is highly recommended, you can enable it using the following command:

 > modprobe orangefs

Insert the Kernel Module
The kernel module (pvfs2.ko) can be found under the OrangeFS installation directory. To insert the module you can use this find/install statement, generally this should be for systems using older than the 4.6 Linux Kernel:   

 > insmod `find /opt/orangefs -name pvfs2.ko`



 

 

 

 
