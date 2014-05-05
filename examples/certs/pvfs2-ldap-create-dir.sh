#!/bin/bash
# pvfs2-new-ldap.sh
# Create a new LDAP directory using OpenLDAP

usage ()
{
    echo "USAGE: $0 [-h] [-p prefix] [-c conf dir] [-r run dir] [-d data dir] [-m mod dir] [-a admin dn] [-s suffix dn] [-w admin pwd]"
    echo "    prefix: directory where OpenLDAP is installed, default /usr/local"
    echo "    config dir: directory containing OpenLDAP server config file (sldap.conf); overrides prefix"
    echo "    run dir: directory containing pid file etc.; overrides prefix"
    echo "    data dir: directory containing database files; overrides prefix"
    echo "    mod dir: directory containing modules (optional); overrides prefix"
    echo "    admin dn: dn of admin user, should be in suffix dn, default cn=admin,{suffix}"
    echo "    suffix dn: base (topmost) dn of tree, default based on hostname, e.g. acme.com => dc=acme,dc=com"
    echo "    admin pwd: admin password, default ldappwd (CHANGE!)"  
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

while getopts "p:c:r:d:m:a:s:w:" option
do
    case $option in
        p)
            prefix=$OPTARG
        ;;
        c)
            confdir=$OPTARG
        ;;
        r)
            rundir=$OPTARG
        ;;
        d)
            datadir=$OPTARG
        ;;
        m)
            moddir=$OPTARG
        ;;
        a)
            admindn=$OPTARG
        ;;
        s)
            suffix=$OPTARG
        ;;
        w)
            adminpw=$OPTARG
        ;;
        *)
            usage
            exit 1
        ;;
    esac
done

# defaults
if [ ! $prefix ]; then
    prefix=/usr/local
fi

if [ ! $confdir ]; then
    confdir=${prefix}/etc/openldap
fi

if [ ! $rundir ]; then
    rundir=${prefix}/var/run
fi

if [ ! $datadir ]; then
    datadir=${prefix}/var/openldap-data
fi

if [ ! $moddir ]; then
    moddir=${prefix}/lib/openldap/modules
fi

if [ ! $suffix ]; then
    # generate based on hostname
    hn=`hostname -f 2> /dev/null`
    if [ ! $hn ]; then
         hn=`hostname 2> /dev/null`
         if [ ! $hn ]; then
             echo "Error: could not retrieve hostname... exiting"
             exit 1
         fi
    fi
    suffix="dc=${hn/./,dc=}"
fi

if [ ! $admindn ]; then
    admindn="cn=admin,$suffix"
fi

if [ ! $adminpw ]; then
    adminpw="ldappwd"
fi

# locate slappasswd
slappasswd=`which slappasswd 2> /dev/null`
if [ ! $slappasswd ]; then
    slappasswd=${prefix}/sbin/slappasswd
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

# write the slapd.conf file
echo -n "Writing slapd.conf... "
sed "s%__PREFIX__%${prefix}%;s%__CONFDIR__%${confdir}%;s%__RUNDIR__%${rundir}%;s%__DATADIR__%${datadir}%;s%__MODDIR__%${moddir}%;s%__ADMINDN__%${admindn}%;s%__ADMINPW__%${encpwd}%;s%__SUFFIX__%${suffix}%" slapd.conf.in > slapd.conf
if [ $? -eq 0 ]; then
    echo "ok"
fi

chmod 600 slapd.conf
if [ $? -ne 0 ]; then
   echo "Warning: could not chmod slapd.conf"
fi

# look for slapd init script
ldapinit=/etc/init.d/ldap
if [ ! -f ${ldapinit} ]; then
    ldapinit=/etc/init.d/slapd
    if [ ! -f ${ldapinit} ]; then
        unset ldapinit
    fi
fi

# shut down slapd if necessary
ps -e | grep -q slapd &> /dev/null
if [ $? -eq 0 ]; then
    echo "Shutting down slapd"
    if [ $ldapinit ]; then
        $ldapinit stop
    else
        killall slapd
    fi
    sleep 2
fi

# copy configuration file
conffile=${confdir}/slapd.conf
if [ -f $conffile ]; then
    echo "Backing up existing $conffile to ${conffile}.bak"
    cp -p $conffile ${conffile}.bak
fi

cp -p slapd.conf $conffile

# start slapd using init script
if [ $ldapinit ]; then
    $ldapinit start
else
    # locate slapd
    slapd=`which slapd 2> /dev/null`
    if [ ! $slapd ]; then
        slapd=${prefix}/libexec/slapd
        if [ ! -f $slapd ]; then
            echo "Error: could not locate slapd; ensure on PATH... exiting"
            exit 1
        fi
    fi
fi

# start slapd
$slapd
if [ $? -ne 0 ]; then
    echo "Error: could not execute slapd... exiting"
    exit 1
fi
sleep 2

# create topmost base container
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

