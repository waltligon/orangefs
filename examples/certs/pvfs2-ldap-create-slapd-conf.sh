#!/bin/bash
# pvfs2-ldap-create-slapd-ldif.sh
# Create a new slapd conf file in ldif format, add it to the sldapd server, and then
# start it.

mv=`which mv`
cp=`which cp`
slapadd=`which slapadd`
groups=`which groups`


usage ()
{
    echo "USAGE: $0 [-h] [-c] -e <etc dir> -i <slapd.d dir> -p <pid dir> -a <arguments dir> -s <schema dir> -d <data dir> -x <suffix dn>"
    echo "    -h: help"
    echo "    -c: only output ldif server config file (to ./slapd.ldif)"
    echo "    -e: location of openldap under an etc directroy (EX: /etc/openldap)"
    echo "    -i: location of the slapd.d directory (EX: /etc/openldap/slapd.d)"
    echo "    -p: location where pid file should be created (EX: /var/run/openldap)"
    echo "    -a: location where argument file will be found (EX: /var/run/openldap)"
    echo "    -s: location of schema ldif files installed with ldap (EX: /etc/openldap/schema)"
    echo "    -d: location where user data directory should be created (EX: /var/run/openldap)"
    echo "    -x: ldap suffix for your directory tree (EX: 'dc=clemson,dc=edu')"
}

required_parameters="e:i:p:a:s:d:x:"
optional_parameters="ch"
all_parameters="$optional_parameters$required_parameters"

while getopts "$all_parameters" option
do
    case $option in
        c)
            confonly=yes
        ;;
        e)
            etcdir=$OPTARG	
        ;;
        i)
            slapddir=$OPTARG
        ;;
        p)
            piddir=$OPTARG
        ;;
	a)
	    argdir=$OPTARG
	;;
	s)
	    schemadir=$OPTARG
	;;
        d)
            datadir=$OPTARG
        ;;
        x)
            #lowercase
            suffix=${OPTARG,}
        ;;
        *)
            usage
            exit 1
        ;;
    esac
done

#Were ANY options enterd on the command line?
if [[ $OPTIND == 1 ]]
then
   usage
   exit 1
fi

#do we have missing required parameters?
if (( OPTIND < ${#required_parameters} ))
then
   echo "Missing parameters:"
   if [[ ! $etcdir ]]
   then
      echo "   etc directory"
   fi
   if [[ ! $slapddir ]]
   then
      echo "   slapd.d directory"
   fi
   if [[ ! $piddir ]]
   then
      echo "   pid directory"
   fi
   if [[ ! $argdir ]]
   then
      echo "   arg directory"
   fi
   if [[ ! $schemadir ]]
   then
      echo "   schema directory"
   fi
   if [[ ! $datadir ]]
   then
      echo "   user data directory"
   fi
   if [[ ! $suffix ]]
   then
      echo "   suffix definition"
   fi
   echo
   usage
   exit 1
fi

#check validity of parameters
if [[ ! -d $etcdir ]]
then
   echo "etc directory ($etcdir) is invalid"
   validity_err=1
fi
if [[ ! -d $slapddir ]]
then
   echo "slapd.d directory ($slapddir) is invalid"
   validity_err=1
fi
if [[ ! -d $piddir ]]
then
   echo "pid directory ($piddir) is invalid"
   validity_err=1
fi
if [[ ! -d $argdir ]]
then
   echo "arg directory ($argdir) is invalid"
   validity_err=1
fi
if [[ ! -d $schemadir ]]
then
   echo "schema directory ($schemadir) is invalid"
   validity_err=1
fi
if [[ ! -d $datadir ]]
then
   echo "user data directory ($datadir) is invalid"
   validity_err=1
fi

#parse the suffix, checking to see if the format is correct.
#looking for dc=xxxxx[,dc=xxxxx]*
#
beforeComma="x"
afterComma=$suffix
while [ $afterComma != $beforeComma ]
do
   beforeComma=${afterComma%%,*}
   if [ ! `echo $beforeComma | grep  ^dc=*` ]
   then
      echo "suffix($suffix) format is invalid ($beforeComma)"
      afterComma=$beforeComma
      validity_err=1
   else
      afterComma=${afterComma#$beforeComma,*}
   fi
done

if [ $validity_err ]
then
   exit 1
fi


#Define the root DN and its password
olcRootDN="cn=Manager,${suffix}"
olcRootPW="secret"


# write the slapd.conf file
echo -n "Writing slapd.ldif ... "
sed "s%__ARGSDIR__%${argdir}%;s%__PIDDIR__%${piddir}%;s%__SCHEMADIR__%${schemadir}%;s%__SUFFIX__%${suffix}%;s%__DBDIR__%${datadir}%" slapd.ldif.in > slapd.ldif

if [ $? -eq 0 ]; then
    echo "[ok]"
else
    echo "[Error ($?) generating file]"
    exit 1
fi

if [ $confonly ]; then
   exit 0
fi

#Should we check the slapd service using systemctl(rhel7) or /sbin/service(<rhel7)?
#Then, shutdown the service using the appropriate command ....
echo -n "Stopping slapd service ... "
if [ `which systemctl 2> /dev/null` ]
then
    use_systemctl=1
    slapd_service=`systemctl list-units --all --type=service | grep slapd`
    if [[ ! "$slapd_service" ]]
    then
       echo "[Service not defined]"
    else
       if `systemctl stop slapd.service > /dev/null`
       then
          echo "[ok]"
       else
          echo "[Unrecoverable error ($?)]"
          exit 1
       fi
    fi
else
   use_service=1
   slapd_service=`/sbin/service slapd status`
   if [[ "$slapd_service" == *unrecognized* ]]
   then
      echo "[Service not defined]"
   else
      if [[ "$slapd_service" == *running* ]]
      then 
         if `/sbin/service slapd stop > /dev/null`
         then
            echo "[ok]"
         else
            echo "[Unrecoverable error($?)]"
            exit 1
         fi
      else
         echo "[ok]"
      fi
   fi
fi
   
#Moving slapd config file to the slapd.d directory.  But first, save
#a copy of slapd.ldif, if it already exists.
if [ -f ${etcdir}/slapd.ldif ]
then
   backupLDIF=`mktemp ${etcdir}/slapd.ldif.XXX-backup`
   echo -n "Creating backup ($backupLDIF) of ${etcdir}/slapd.ldif ... "
   if `$mv ${etcdir}/slapd.ldif ${backupLDIF} &> /dev/null`
   then
      echo "[ok]"
   else
      echo "[Unrecoverable error ($?)]"
      exit 1
   fi
fi

echo -n "Copying slapd.ldif to ${etcdir} ... "
if `$cp slapd.ldif ${etcdir} &> /dev/null`
then
   echo "[ok]"
else
   echo "[Unrecoverable error ($?)]"
   exit 1
fi



#Saving contents of slapd.d
backupDir=`mktemp -d ${slapddir}.XXX-backup`
echo -n "Creating backup($backupDir) of slapd.d directory ... "
if `$mv ${slapddir} ${backupDir} &> /dev/null`
then
   echo "[ok]"
else
   echo "[Unrecoverabel error ($?)]"
   exit 1
fi
 
#Recreating slapd.d directory
echo -n "Creating ${slapddir} with ldap as user and group ... "
if `mkdir ${slapddir} &> /dev/null`
then
   if `chown ldap:ldap ${slapddir}`
   then
      echo "[ok]"
   else
      echo "[Unrecoverable error ($?)]"
      exit 1
   fi
else
   echo "[Unrecoverable error ($?)]"
   exit 1
fi


#Add configuration to slapd server
echo -n "Using slapadd to create ldif structure in $slapddir ... "
if `$slapadd -n 0 -F $slapddir -l ${etcdir}/slapd.ldif &> /dev/null`
then
   echo "[ok]"
else
   echo "[Unrecoverable error ($?)]"
   exit 1
fi

#if using systemctl, the owner of the slapd.d directory and its 
#contents must be ldap with group ldap
if [ $use_systemctl ]
then
   echo -n "Checking owner and group of slapd.d directory ... "
   if `${groups} ldap | grep ldap.*ldap &> /dev/null`
   then
      if `chown -R ldap:ldap ${etcdir}/slapd.d`
      then
          echo "[ok]"
      else
          echo "[Unrecoverable error ($?)]"
          exit 1
      fi
   else
      echo "[ldap user and/or group does not exist]"
      exit 1
   fi
fi

#Start the slapd server, using the new configuration file, slapd.ldif
if [ $use_systemctl ]
then
   echo -n "Starting slapd service ... "
   if `systemctl start slapd.service &> /dev/null`
   then
      echo "[ok]"
   else
      echo "[Unrecoverable error ... try systemctl status slapd]"
      exit 1
   fi
fi

   
#Adding the suffix dn
dname=${suffix#*=}
dname=${dname%%,*}
ldapadd -D "$olcRootDN" -w "$olcRootPW" -x <<_EOF
dn: $suffix
dc: $dname
objectClass: domain
_EOF
if [ $? -ne 0 ]; then
    echo "Error: could not create ${suffix}... exiting"
    exit 1
fi

# create user and group containers
ldapadd -D "$olcRootDN" -w "$olcRootPW" -x <<_EOF
dn: ou=Users,$suffix
ou: Users
objectClass: organizationalUnit

dn: ou=Groups,$suffix
ou: Groups
objectClass: organizationalUnit
_EOF
if [ $? -ne 0 ]; then
    echo "Error: could not create dn=Users,${suffix}... exiting"
    exit 1
fi


# create a user to represent root (not LDAP database manager) 
ldapadd -D "$olcRootDN" -w "$olcRootPW" -x <<_EOF
dn: cn=root,ou=Users,$suffix
cn: root
uid: root
objectClass: inetOrgPerson
objectClass: posixAccount
displayName: root
sn: root
uidNumber: 0
gidNumber: 0
homeDirectory: /root
userPassword: root
_EOF
if [ $? -ne 0 ]; then
    echo "Error: could not create cn=root,ou=Users,${suffix}... exiting"
    exit 1
fi

exit 0
