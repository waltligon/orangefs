#!/bin/bash
# pvfs2-new-ldap.sh
# Create a new LDAP directory using OpenLDAP

usage ()
{
    echo "USAGE: $0 [-h] [-p prefix] [-a admin dn] [-s suffix dn] [-w admin pwd]"
    echo "    prefix: directory where OpenLDAP is installed, default /usr/local"
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

while getopts "p:a:s:w:" option
do
    case $option in
        p)
            prefix=$OPTARG
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

if [ ! $suffix ]; then
    # generate based on hostname
    hn=`hostname -f`
    suffix="dc=${hn/./,dc=}"
fi

if [ ! $admindn ]; then
    admindn="cn=admin,$suffix"
fi

if [ ! $adminpw ]; then
    adminpw="ldappwd"
fi

# get encrypted root password
encpwd=`${prefix}/sbin/slappasswd -s $adminpw`
if [ $? -ne 0 ]; then
    echo "Error: could not get password hash... exiting"
    exit 1
fi

# write the slapd.conf file
echo -n "Writing slapd.conf... "
sed "s%__PREFIX__%${prefix}%;s%__ADMINDN__%${admindn}%;s%__ADMINPW__%${encpwd}%;s%__SUFFIX__%${suffix}%" slapd.conf.in > slapd.conf
if [ $? -eq 0 ]; then
    echo "ok"
else
    echo "Error: could not create slapd.conf"
    exit 1
fi

chmod 600 slapd.conf
if [ $? -ne 0 ]; then
   echo "Warning: could not chmod slapd.conf"
fi

# shut down slapd if necessary
ps -e | grep -q slapd &> /dev/null
if [ $? -eq 0 ]; then
    echo "Shutting down slapd"
    killall slapd
    sleep 2
fi

# copy configuration file
conffile=${prefix}/etc/openldap/slapd.conf
if [ -f $conffile ]; then
    echo "Backing up existing $conffile to ${conffile}.bak"
    cp -p $conffile ${conffile}.bak
fi

cp -p slapd.conf $conffile

# locate slapd
slapd=`which slapd`
if [ ! $slapd ]; then
    slapd=${prefix}/libexec/slapd
    if [ ! -f $slapd ]; then
        echo "Error: could not locate ${slapd}... exiting"
        exit 1
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

