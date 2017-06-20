#!/bin/bash
# Set a user password, in encrypted format

usage () 
{
    echo "USAGE: $0 [-H hosturi] [-D <admin dn>] [-w <admin password>] <user dn> <password>"
    echo "       hosturi: uri of remote LDAP server (ldap://ldap-server)"
    echo "       admin dn: dn of LDAP administrator"
    echo "       admin password: password of LDAP admin, leave blank for prompt"
    echo "       user dn: user whose password will be changed"
    echo "       password: new password"
}

while getopts "D:w:H:" option
do
    case $option in
    D)
        admindn=$OPTARG
        ;;
	w)
	    adminpw=$OPTARG
        ;;
    H)	
    	hosturi=$OPTARG
	;;

    *)
        usage
	exit 1
	;;
    esac
done

# get parameters
userdn=${!OPTIND}
OPTIND=$((OPTIND + 1))
password=${!OPTIND}

if [ "x$userdn" = "x" -o "x$password" = "x" ]; then
    echo "Error: Missing user DN or password"
    usage
    exit 1
fi

# bind option
if [ $admindn ]; then
    bindopt="-D $admindn"
fi

# password option (-W to prompt)
if [ $adminpw ]; then
    pwdopt="-w"
else
    pwdopt="-W"
fi

# bind option
if [ $hosturi ]; then
    hostopt="-H $hosturi"
fi


encpw=`slappasswd -s $password`
if [ ! $encpw ]; then
    echo "Error: could not encrypt password"
    exit 1
fi

ldapmodify $hostopt $bindopt $pwdopt $adminpw -x <<_EOF
dn: $userdn
changetype: modify
replace: userPassword
userPassword: $encpw
-
_EOF

