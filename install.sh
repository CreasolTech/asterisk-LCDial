#!/bin/bash
function getAnswer () {
	local x
	echo -e  "\n\n$*  (Y/n) ?"
	read x
	if [ "$x" == 'n' -o "$x" == 'N' ]; then
		echo "No"
		return 1
	else
		echo "Yes"
		return 0
	fi
}




echo "This script is used to compile and install LCDial application in asterisk"
echo "Any suggestion or bug report to psubiaco@creasol.it"
echo "Press a key to continue..."; read x

if [ -x /usr/bin/vim ]; then
	EDITOR=vim
elif [ -x /usr/bin/nano ]; then
	EDITOR=nano
elif [ -x /usr/bin/pico ]; then
	EDITOR=pico
else
	EDITOR=vi
fi

#while [ ! -d /usr/src/asterisk -o -s /usr/src/asterisk ]; do
#	echo "/usr/src/asterisk not found: please type the path of your asterisk sources..."
#	read astsourcepath
#	if [ -d "$astsourcepath" -o -s "$astsourcepath" ]; then
#		if [ "$astsourcepath" != "/usr/src/asterisk" ]; then
#			echo "Create symlink..."
#			ln -s "$astsourcepath" "/usr/src/asterisk"
#		fi
#	fi
#done	

if [ ! -d /usr/lib/asterisk ]; then
	echo "Asterisk sources should be installed: 
I don't find the include files into /usr/include/asterisk directory"
	exit
fi

echo "Start compiling (make clean && make && make install)..."
make clean && make && make install

if [ ! -f /etc/asterisk/lcdial.conf ]; then
	echo "Copy sample lcdial.conf to /etc/asterisk..."
	cp doc/lcdial.conf /etc/asterisk/
fi
getAnswer "Do you want to import database data from doc/sample-data.sql ?"
if [ $? -eq 0 ]; then
	egrep '^[a-zA-Z].*' </etc/asterisk/lcdial.conf |grep -v getproviders >/tmp/lcdial.sh
	. /tmp/lcdial.sh
	myhost=''
	if [ $hostname != localhost ]; then
		myhost="-h $hostname"
	fi
		
	echo "Import MySQL database from doc/sample-data.sql"
	mysql $myhost -u $user --password=$password $dbname <doc/sample-data.sql
fi

echo "Terminated: check, modify extensions.conf using LCDial instead of Dial,
and restart asterisk. Thank you for using LCDial() application!"

	
