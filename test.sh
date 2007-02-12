#!/bin/bash
#
# $Header$
#
# Little test for dbm to check some commands.
# I am just too clumsy.
#
# $Log$
# Revision 1.3  2007-02-12 19:15:17  tino
# Preparing dist: See ChangeLog
#
# Revision 1.2  2006/08/09 23:13:17  tino
# Typo corrected.
#
# Revision 1.1  2006/08/09 23:11:08  tino
# Test script added.  Well it's very small for now.

echo
echo "Please ignore error messages.  These must show up."
echo "If [ALL OK] is printed, everything is fine."
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

q1()
{
r 1 qq "$@"
}

q2()
{
r 2 qq "$@"
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

w()
{
tof="$1"
shift
q "$@" > "TEST.db.$tof.tmp"
}

t()
{
sed 's/ utcdate=".*"//' "TEST.db.$1.tmp" |
r 0 x cmp - "TEST.db.$1" &&
rm -f "TEST.db.$1.tmp"
}

#################################################################
### HERE THE TEST STARTS
#################################################################

rm -f TEST.db TEST.db.xml TEST.db.xml.tmp
cat > TEST.db.xml <<EOF
<?xml version="1.0" encoding="ISO-8859-1" standalone="yes" ?>
<dbm name="TEST.db">
 <row n="1">
  <key>t1</key>
  <data>d3&#10;</data>
 </row>
</dbm>
<!-- 1 rows -->
EOF

q create
q insert t1 d0
q2 insert t1 d1
q replace t1 d1
c t1 d1
q alter t1 d1 d2
c t1 d2
echo d3 | q alter t1 d2
c t1 'd3
'
w xml export
t xml
echo t1 | q balter 'd3
' d4
c t1 d4
q2 delete t1 d3
q2 import < TEST.db.xml
q delete t1 d4
q import < TEST.db.xml
w xml export
t xml
q delete t1 'd3
'
q kill
[ ! -f TEST.db ]
rm -f TEST.db.xml

echo
echo "[ALL OK]"
