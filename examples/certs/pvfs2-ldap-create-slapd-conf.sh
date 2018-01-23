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

randpw ()
{
    chars="abcdefghijklmnopqrstuvwxyz0123456789"
    for ((i=0; $i < 10; i++))
    do        
        pass+=${chars:$(($RANDOM % 36)):1}
    done
    
    echo $pass
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

#No options were entered on the command line
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
#a copy of slapd.ldif, if one already exists.
if [ -f ${etcdir}/slapd.ldif ]
then
   echo -n "Saving a copy of ${etcdir}/slapd.ldif ... "
   if `$mv ${etcdir}/slapd.ldif ${etcdir}/slapd.ldif.save &> /dev/null`
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

   
exit 0
# locate slappasswd
slappasswd=`which slappasswd 2> /dev/null`
if [ ! $slappasswd ]; then
    slappasswd=/usr/sbin/slappasswd
    if [ ! -f $slappasswd ]; then
        echo "Error: could not locate slappasswd; ensure on PATH... exiting"
        exit 1
    fi
fi

# get encrypted root password
encpwd=`${slappasswd} -s $adminpw`
if [ $? -ne 0 ]; then
    echo "Error: could not get password hash... exiting"
    exit 1
fi


chmod 644 slapd.conf
if [ $? -ne 0 ]; then
   echo "Warning: could not chmod slapd.conf"
fi

# shut down slapd if necessary
ps -e | grep -q slapd &> /dev/null
if [ $? -eq 0 ]; then
    echo "Shutting down slapd"
    $initscript stop
    sleep 2
fi

# copy configuration file
if [ -f $conffile ]; then
    echo "Backing up existing $conffile to ${conffile}.bak"
    cp -p $conffile ${conffile}.bak
fi
cp -p slapd.conf $conffile
if [ $? -ne 0 ]; then
    echo "Error copying to $conffile... exiting"
    exit 1
fi

# back up configuration dir
if [ -d $confdir/slapd.d ]; then
    echo "Renaming $confdir/slapd.d to $confdir/slapd.d.bak"
    mv $confdir/slapd.d $confdir/slapd.d.bak
fi

# start slapd using init script
$initscript start

sleep 2

#containers=(`echo $suffix | sed 's/,/ /g'`)
#for ((i = ${#containers[*]}-1; i >= 0; i--)); do
#    cname=${containers[$i]#*=}
#    ctype=${containers[$i]%=*}

    # echo "${containers[$i]: $ctype $cname"

#    case $ctype in
#        dc) 
#            class="domain"
#        ;;
#        l)
#            class="locality"
#        ;;
#        o)
#            class="organization"
#        ;;
#        ou)
#            class="organizationalUnit"
#        ;;
#        *)
#            echo "Error: cannot create top level container; use only dc, l, o and ou... exiting"
#            exit 1
#        ;;
#    esac  

# create topmost base containers
dname=${suffix#*=}
dname=${dname%%,*}
ldapadd -D "$admindn" -w "$adminpw" -x <<_EOF
dn: $suffix
dc: $dname
objectClass: domain
_EOF
if [ $? -ne 0 ]; then
    echo "Error: could not create ${suffix}... exiting"
    exit 1
fi

# create user and group containers
ldapadd -D "$admindn" -w "$adminpw" -x <<_EOF
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

# create a user to represent root (not LDAP admin) 
# use random password
ldapadd -D "$admindn" -w "$adminpw" -x <<_EOF
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
userPassword: `randpw`
_EOF
if [ $? -ne 0 ]; then
    echo "Error: could not create cn=root,ou=Users,${suffix}... exiting"
    exit 1
fi

