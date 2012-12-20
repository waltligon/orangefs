#!/bin/bash
# Add user to LDAP using info from /etc/passwd

randpw ()
{
    chars="abcdefghijklmnopqrstuvwxyz0123456789"
    for ((i=0; $i < 10; i++))
    do        
        pass+=${chars:$(($RANDOM % 36)):1}
    done
    
    echo $pass
}

usage () 
{
    echo "USAGE: $0 [-D <admin dn>] [-w <admin password>] <logon name> <container dn>"
    echo "       admin dn: dn of LDAP administrator"
    echo "       admin password: password of LDAP admin, leave blank for prompt"
    echo "       logon name: logon name of user in /etc/passwd that will be created"
    echo "       container dn: dn of container object for new user"
}

while getopts "D:w:" option
do
    case $option in
        D)
            admindn=$OPTARG
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

# get parameters
username=${!OPTIND}
OPTIND=$((OPTIND + 1))
container=${!OPTIND}

if [ "x$username" = "x" -o "x$container" = "x" ]; then
    echo "Error: Missing logon name or container"
    usage
    exit 1
fi

# bind option
if [ $admindn ]; then
    bindopt="-D"
fi

# password option (-W to prompt)
if [ $adminpw ]; then
    pwdopt="-w"
else
    pwdopt="-W"
fi

# get fields from /etc/passwd
# replace spaces with ~ and then :'s with spaces
fields=(`grep ^$username: /etc/passwd | sed 's/ /~/g ; s/:/ /g'`)
if [ ! $fields ]; then
    echo "Error: could not locate $username in /etc/passwd"
    exit 1
fi

ldapuid=${fields[2]}
ldapgid=${fields[3]}
desc=${fields[4]}
# remove commas from description (e.g. Ubuntu)
desc=${desc%%,*}
# replace ~ with space
desc=${desc//\~/ }
homedir=${fields[5]}
ldapshell=${fields[6]}

# compute sn as last space-separated element
sn=`awk '{ print $NF }' <<<"$desc"`
if [ ! $sn ]; then
    sn=$username
fi
# compute given name as first space-separated element
# (might be same as sn)
givenname=`awk '{ print $1 }' <<<"$desc"`
if [ ! $givenname ]; then
    givenname=$username
fi

# execute the ldapadd using localhost
ldapadd $bindopt $admindn $pwdopt $adminpw -x <<_EOF
dn: cn=${username},${container}
cn: $username
uid: $username
objectClass: inetOrgPerson
objectClass: posixAccount
displayName: $desc
givenName: $givenname
sn: $sn
uidNumber: $ldapuid
gidNumber: $ldapgid
homeDirectory: $homedir
loginShell: $ldapshell
userPassword: `randpw`
_EOF

