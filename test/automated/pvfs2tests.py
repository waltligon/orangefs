#!/usr/bin/python

#NOTE:Command line takes 3 options: config file, PAV config, PAV machine list

import os,sys,ConfigParser,bsddb,smtplib,string

#This function gets all options from the Config file that was passed
#at the command line.
def getconfig():

	Config=bsddb.hashopen(None,'w')
	Tests=bsddb.hashopen(None,'w')

	config=ConfigParser.ConfigParser()
	config.readfp(open(sys.argv[1]))

	if not config.has_section('Config'):
		print "Config file *must* have section [Config]"
		sys.exit(1)
	for options in config.options('Config'):
		Config[options]=config.get('Config',options)
	if not Config.has_key('pvfs2tabfile'):
		print "Config section must have a \"PVFS2TABFILE\" option"
		sys.exit(1)
	if not Config.has_key('pvfs2bindir'):
		print "Config section must have a \"PVFS2BINDIR\" option"
		sys.exit(1)
	if not Config.has_key('mpichbindir'):
		print "Config section must have a \"MPICHBINDIR\" option"
		sys.exit(1)
	if not Config.has_key('pavdir'):
		print "Config section must have a \"PAVDIR\" option"
		sys.exit(1)
	if not Config.has_key('mountpoint'):
		print "Config section must have a \"MOUNTPOINT\" option"
		sys.exit(1)
	if not Config.has_key('email'):
		print "Config section must have a \"EMAIL\" option"
		sys.exit(1)

	if not config.has_section('Tests'):
		print "Config file *must* have section [Tests]"
		sys.exit(1)

	for options in config.options('Tests'):
		Tests[options]=(config.get('Tests',options))

	return Config,Tests


def start_pav(pavconfigfile,pavmachinefile,Config):
    	pav_dir=Config['pavdir']

	#if os.path.exists(Config['mountpoint']):
	#	os.system('rm -rf '+Config['mountpoint'])
	#os.mkdir(Config['mountpoint'])
    	success=os.system(pav_dir+'/pav_start -m '+pavmachinefile+' -c '+pavconfigfile+' > pav_setup.log 2>&1')

    	if success<>0:
		print 'Failed to start PAV, see pav_setup.log'
		sys.exit(1)

    	os.system(pav_dir+'/pav_info -c '+pavconfigfile+' > pav_info.log 2>&1')


def stop_pav(pavconfigfile,Config):
	pav_dir=Config['pavdir']
	cmd=pav_dir+"/pav_stop -c "+oldwd+"/"+pavconfigfile
	os.system(cmd)


#This function is responsible for running all the tests that were in the config file
def runtests(Config,Tests):
	#construct mpiexec arguments from Config
	#construct tests to run from Tests
	#run the tests contained in Tests

	for KEY in Config.keys():
		os.environ[KEY]=Config[KEY]

	os.system('rm -f total.output')
	fail = 0
	for TEST in Tests.keys():
		cmd="echo "+Tests[TEST]+" >> total.output"
		os.system(cmd)
		cmd="echo ================ >> total.output"
		os.system(cmd)
		cmd=Tests[TEST]+" >> total.output 2>&1"
		outfile="total.output"
		print cmd
		if os.system(cmd):
			fail = 1

	if fail:
		cmd="mail -s \"PVFS2 test: FAIL\" "
	else:
		cmd="mail -s \"PVFS2 test: PASS\" "
	cmd = cmd + Config['email'] + " < total.output"
	print cmd
	if os.system(cmd):
		print "Error sending email; aborting..."
		sys.exit(1)


if len(sys.argv) <> 4:
	print repr(len(sys.argv))+" arguments passed"
	print "Usage: pvfs2tests.py <Test config file> <PAV config> <PAV machine list>"
	sys.exit(1)


Config,Tests=getconfig()

oldwd=os.getcwd()
start_pav(sys.argv[2],sys.argv[3],Config)
runtests(Config,Tests)
stop_pav(sys.argv[2],Config)
