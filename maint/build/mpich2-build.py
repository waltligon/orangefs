#!/usr/bin/python
# This script gets and builds MPICH2 with PVFS2 support

import os,sys,string

# default directory to download and build in 
rootdir="/tmp/pvfs2-build-test/"

def get_build_MPICH2():

	os.chdir(rootdir)
	if os.path.exists('mpich2-beta.tar.gz'):
		os.remove('mpich2-beta.tar.gz')
	if os.path.exists('mpich2-src'):
		os.remove('mpich2-src')

	# get MPICH2 and extract it
	if os.system('wget -q --passive-ftp ftp://ftp.mcs.anl.gov/pub/mpi/mpich2-beta.tar.gz'):
		print "Failed to download mpich2; Exiting..."
		sys.exit(1)
	if os.system('tar -xzvf mpich2-beta.tar.gz >tarout'):
		print "Failed to untar mpich2; Exiting..."
		sys.exit(1)

	# find the release name
	os.system('ln -s `head -n1 tarout` mpich2-src')

	# see if there are any patches available for this mpich release
	if os.system('wget -q http://www.parl.clemson.edu/~pcarns/patches/`head -n1 tarout`/pvfs2.patch') == 0:
		if os.system('patch -s -p1 -d mpich2-src < pvfs2.patch'):
			print "Failed to patch mpich2; Exiting..."
			sys.exit(1)
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
	if os.system('./configure --enable-romio --with-file-system=pvfs2+nfs+ufs --disable-f77 --prefix='+mpichdir+' --exec-prefix='+mpichdir+' >../mpich-configure.log 2>&1'):
		print "MPICH2 configure failed; Exiting..."
		print "See " + rootdir + "mpich-configure.log for details."
		print "Perhaps you forgot to run pvfs2-build first?"
		sys.exit(1)

	if os.system('make > ../mpich-make.log 2>&1'):
		print "See " + rootdir + "mpich-make.log for details."
		print "MPICH2 build failed; Exiting..."
		sys.exit(1)
	
	if os.system('make install > ../mpich-make-install.log 2>&1'):
		print "MPICH2 make install failed; Exiting..."
		print "See " + rootdir + "mpich-make-install.log for details."
		sys.exit(1)
	
	# UNset variables not needed anymore
	if OLD_CFLAGS:
		os.environ['CFLAGS']=OLD_CFLAGS
	if OLD_LDFLAGS:
		os.environ['LDFLAGS']=OLD_LDFLAGS
	if OLD_LIBS:
		os.environ['LIBS']=OLD_LIBS

	return

if len(sys.argv)<2:
	print "MPICH2 will be built in default directory (/tmp/pvfs2-build-test)."
else:
	rootdir=sys.argv[1]+"/"
	print "MPICH2 will be built in " + rootdir + "."

pvfs2_src=rootdir+'pvfs2/'
pvfs2_build=rootdir+'BUILD-pvfs2/'
pvfs2_install=rootdir+'INSTALL-pvfs2/'
mpichdir=rootdir+'mpich2/'
mpichsrc=rootdir+'mpich2-src/'

if not os.path.exists(rootdir):
	os.mkdir(rootdir)

os.chdir(rootdir)

get_build_MPICH2()
