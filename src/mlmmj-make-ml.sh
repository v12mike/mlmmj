#!/bin/sh
#
# mlmmj-make-ml.sh - henne@hennevogel.de
#

VERSION="0.1"
DEFAULTDIR="/var/spool/mlmmj"

USAGE="mlmmj-make-ml "$VERSION"
"$0" [OPTIONS]

-h	display this help text
-L	the name of the mailinglist
-s	your spool directory if not "$DEFAULTDIR"
-a	create the needed entrys in your /etc/aliases file
-z	nuffn for now"

while getopts ":hL:s:az" Option
do
case "$Option" in 
	h )
	echo -e "$USAGE"
	exit 0
	;;
	z )
	echo -n "nothing"
	exit 0
	;;
	L )
	LISTNAME="$OPTARG"
	;;
	s )
	SPOOLDIR="$OPTARG"
	;;
	a )
	A_CREATE="YES"
	;;
	* )
	echo -e "$0: invalid option\nTry $0 -h for more information."
	exit 1
esac
done
let SHIFTVAL=$OPTIND-1
shift $SHIFTVAL

if [ -z "$SPOOLDIR" ]; then
	SPOOLDIR="$DEFAULTDIR"
fi

echo "Creating Directorys below $SPOOLDIR. Use '-s spooldir' to change"

if [ -z "$LISTNAME" ]; then
	echo -n "What should the name of the Mailinglist be? [mlmmj-test] : "
	read LISTNAME
	if [ -z "$LISTNAME" ]; then
	LISTNAME="mlmmj-test"
	fi
fi

LISTDIR="$SPOOLDIR/$LISTNAME"

mkdir -p $LISTDIR

for DIR in incoming queue queue/discarded archive text subconf unsubconf \
	   bounce control moderation subscribers.d requeue
do
	mkdir "$LISTDIR"/"$DIR"
done

touch "$LISTDIR"/index

echo -n "The Domain for the List? [] : "
read FQDN
if [ -z "$FQDN" ]; then
	FQDN=`domainname`
fi

echo -n "The emailaddress of the list owner? [postmaster] : "
read OWNER
if [ -z "$OWNER" ]; then
	OWNER="postmaster"
fi
echo "$OWNER" > "$LISTDIR"/"control/owner"

echo -n "The path to texts for the list? (listtexts/ dir in the src) [] : "
read TEXTPATH
if [ -z "$TEXTPATH" -o ! -d "$TEXTPATH" ]; then
	echo "**NOTE** Could not copy the texts for the list"
	echo "Please manually copy the files from the listtexts/ directory"
	echo "in the source distribution of mlmmj."
else
	cp "$TEXTPATH"/* "$LISTDIR"/"text"
fi

LISTADDRESS="$LISTNAME@$FQDN"
echo "$LISTADDRESS" > "$LISTDIR"/control/"listaddress"

MLMMJRECIEVE=`which mlmmj-recieve 2>/dev/null`
if [ -z "$MLMMJRECIEVE" ]; then
	MLMMJRECIEVE="/path/to/mlmmj-recieve"
fi

MLMMJMAINTD=`which mlmmj-maintd 2>/dev/null`
if [ -z "$MLMMJRECIEVE" ]; then
	MLMMJMAINTD="/path/to/mlmmj-maintd"
fi

ALIAS="$LISTNAME:  \"|$MLMMJRECIEVE -L $SPOOLDIR/$LISTNAME/\""
CRONENTRY="0 */2 * * * \"$MLMMJMAINTD -L $SPOOLDIR/$LISTNAME/\""

if [ -n "$A_CREATE" ]; then
	echo "I want to add the following to your /etc/aliases file:"
	echo "$ALIAS"

	echo -n "is this ok? [y/N] : "
	read OKIDOKI
	case $OKIDOKI in
		y|Y)
		echo "$ALIAS" >> /etc/aliases
		;;
		n|N)
		exit 0
		;;
		*)
		echo "Options was: y, Y, n or N"
	esac
else
	echo
	echo "Don't forget to add this to /etc/aliases:"
	echo "$ALIAS"
fi
echo
echo "If you're not starting mlmmj-maintd in daemon mode,"
echo "don't forget to add this to your crontab:"
echo "$CRONENTRY"

echo
echo " ** FINAL NOTES **
1) The mailinglist directory have to be owned by the user running the 
mailserver (i.e. starting the binaries to work the list)
2) Run newaliases"
