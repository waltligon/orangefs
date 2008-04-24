#!/usr/bin/python
# This script gets and builds MPICH2 with PVFS2 support

import os,sys,string

from optparse import OptionParser

parser = OptionParser()
parser.add_option("-v", "--cvstag", dest="cvs_tag", default="HEAD",
		  help="pvfs2 cvs tag", metavar="TAG")
parser.add_option("-r", "--dir", dest="rootdir", 
                  default="/tmp/pvfs2-build-test/",
		  help="build directory", metavar="DIR")

(options, args) = parser.parse_args()

def get_build_MPICH2():

	os.chdir(options.rootdir)
	if os.path.exists('mpich2.tar.gz'):
		os.remove('mpich2.tar.gz')
	if os.path.exists('mpich2-src'):
		os.remove('mpich2-src')

	# get MPICH2 and extract it
	if os.system('wget -q --passive-ftp ftp://ftp.mcs.anl.gov/pub/mpi/mpich2.tar.gz'):
		print "Failed to download mpich2 Exiting..."
		sys.exit(1)
	if os.system('tar -xzvf mpich2.tar.gz >tarout'):
		print "Failed to untar mpich2; Exiting..."
		sys.exit(1)

	
	# find the release name
	os.system('ln -s `head -n1 tarout` mpich2-src')

	# look for a patch that matches it- don't error out if this fails; 
	# the mpich2 version we downloaded may not need a patch

	# turn this off for now since it doesn't seem to be working
	#os.system('ls pvfs2/doc/coding/romio-MPICH2-`head -n1 tarout | cut -d "-" -f 2 | cut -d "/" -f 1`-PVFS2* > target_patch')
	#os.system('patch -s -p1 -d mpich2-src/src/mpi/romio < `cat target_patch`')

	os.remove('tarout')

	# set the necessary variables for compiling with PVFS2 support and install it
	OLD_CFLAGS = os.getenv("CFLAGS")
	OLD_LDFLAGS = os.getenv("LDFLAGS")
	OLD_LIBS = os.getenv("LIBS")
	os.environ['CFLAGS']="-I"+pvfs2_install+"include"
	os.environ['LDFLAGS']="-L"+pvfs2_install+"lib"
	os.environ['LIBS']="-lpvfs2 -lpthread"

	if os.path.exists(mpichdir):
		os.system('rm -rf '+mpichdir)
	os.mkdir(mpichdir)

	os.chdir(mpichsrc)
	os.chdir("src/mpi/romio")
	if os.system('autoconf > ../mpich-romio-autoconf.log 2>&1'):
		print "MPICH2 autoconf failed; Exiting..."
		print "See "+options.rootdir+"/mpich-romio-autoconf.log for deatils."
		sys.exit(1)
	os.chdir("../../../")

	if os.system('./configure --enable-romio --with-file-system=ufs+nfs+pvfs2 --disable-f77 --prefix='+mpichdir+' --exec-prefix='+mpichdir+' >../mpich-configure.log 2>&1'):
		print "MPICH2 configure failed; Exiting..."
		print "See " + options.rootdir + "/mpich-configure.log for details."
		print "Perhaps you forgot to run pvfs2-build first?"
		sys.exit(1)

	if os.system('make > ../mpich-make.log 2>&1'):
		print "See " + options.rootdir + "mpich-make.log for details."
		print "MPICH2 build failed; Exiting..."
		sys.exit(1)
	
	if os.system('make install > ../mpich-make-install.log 2>&1'):
		print "MPICH2 make install failed; Exiting..."
		print "See " + options.rootdir + "mpich-make-install.log for details."
		sys.exit(1)
	
	# UNset variables not needed anymore
	if OLD_CFLAGS:
		os.environ['CFLAGS']=OLD_CFLAGS
	if OLD_LDFLAGS:
		os.environ['LDFLAGS']=OLD_LDFLAGS
	if OLD_LIBS:
		os.environ['LIBS']=OLD_LIBS

	return

	print "MPICH2 will be built in "+options.rootdir+"."

pvfs2_src=options.rootdir+'/pvfs2-'+options.cvs_tag
pvfs2_build=options.rootdir+'/BUILD-pvfs2'+options.cvs_tag
pvfs2_install=options.rootdir+'/INSTALL-pvfs2'+options.cvs_tag
mpichdir=options.rootdir+'/mpich2/'
mpichsrc=options.rootdir+'/mpich2-src/'

if not os.path.exists(options.rootdir):
	os.mkdir(options.rootdir)
	

os.chdir(options.rootdir)

get_build_MPICH2()
