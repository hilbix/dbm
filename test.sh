#!/bin/bash
#
# $Header$
#
# Little test for dbm to check some commands.
# I am just too clumsy.
#
# $Log$
# Revision 1.1  2006-08-09 23:11:08  tino
# Test script added.  Well it's very small for now.
#


echo "Please ignore error messages.  They must show up."
echo "If [ALL OK} is printed, everything is fine."
echo "Else the script just stops and sets an error code."
echo

set -e

# protect against shell stealing LFs
d()
{
od -tx1z
}

x()
{
echo -n "run:" >&2
for arg
do
	echo -n " '$arg'" >&2
done
echo >&2
"$@"
}

r()
{
ret="$1"
shift
val=0
"$@" || val="$?"
if [ ".$val" != ".$ret" ]
then
	echo "retured $val, expected was $ret"
	false
fi
}

qq()
{
first="$1"
shift
x ./dbm "$first" TEST.db "$@"
}

q()
{
r 0 qq "$@"
}

c()
{
get="`q get "$1" | d`"

if [ 1 = $# ]
then
	cmp="`d`"
else
	cmp="`echo -n "$2" | d`"
fi

if [ ".$cmp" != ".$get" ]
then
	echo "failed: key '$1' gave '$get', not '$cmp'"
	false
fi
}


#################################################################
### HERE THE TEST STARTS
#################################################################

rm -f TEST.db
q create
q insert t1 d1
c t1 d1
q alter t1 d1 d2
c t1 d2
echo d3 | q alter t1 d2
c t1 'd3
'
echo t1 | q balter 'd3
' d4
c t1 d4
r 2 qq delete t1 d3
q delete t1 d4
q kill
[ ! -f TEST.db ]

echo
echo "[ALL OK]"
