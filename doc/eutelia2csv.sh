#!/bin/bash
# Syntax: $0 infilename outfile providername

PREFIXFILE=country_internationprefix_mobile.csv
LOGLEVEL=1	#0=None, 1=PREFIX ERRORS

function printsyntax {
	echo "Syntax: $0 INTEXTFILE OUTCSVFILE PROVIDERNAME"
}


if [ $# -ne 3 ]; then
	printsyntax
	exit
fi

if [ ! -r $1 ]; then
	echo "Textfile $1 does not exist or not readable"
	printsyntax
	exit
fi
if [ -f $2 ]; then
	echo "Output csv file $2 already exists: trunc it!"
	> $2
fi

nl='
'
tab=' '
space=' '
IFS="$nl"
for line in `cat $1|grep -v 'A B C D' |sed 's/SuperDeal[\!\*]*//gi' |sed 's/FREE\*/0.00/gi'`; do
	IFS="$tab$space"
	#country=`echo ${line}|awk '{print $1}'`
	country=`echo ${line}|awk -F ';' '{print $1}'|awk -F ',' '{print $1}'|sed 's/ *$//'`
	landline=1
	if [ -n "`echo ${line}|grep -i mobile`" ]; then
		landline=0
	fi
	# NF=number of field 
	rate=`echo "${line}"|awk -F ';' '{print $2}'`

	IFS="$nl"
	c=0
	# Is there "country" inside the country international prefix file?
	grep -i "$country" ${PREFIXFILE} >/dev/null
	if [ $? -ne 0 ]; then
		#Is $country formed by two words separated by - or ' ' ?
		if [ `echo $country|grep '-'` ]; then
			country="`echo $country|awk -F - '{print $1}'`"
		elif [ `echo $country|grep '_'` ]; then
			country="`echo $country|awk -F _ '{print $1}'`"
		elif [ `echo $country|grep ' '` ]; then
			country="`echo $country|awk '{print $1}'`"
		fi
	fi
	for prefix in `grep -i "$country" ${PREFIXFILE}`; do
		c=$(( $c + 1 ))
		pcountry=`echo $prefix|awk -F ';' '{print $1}'`
		pland=`echo $prefix|awk -F ';' '{print $2}'`
		pmobile=`echo $prefix|awk -F ';' '{print $3}'`
		if [ $landline -eq 1 ]; then
			# landline call: report just the international prefix
			p=^00${pland}
			if [ $LOGLEVEL -ge 2 ]; then echo "Country:$pcountry; landline=$landline; prefix=$p; rate=$rate"; fi
			echo "${p};${3};${rate};${pcountry}" >>$2
			break
		else
			# mobile call: report all mobile prefixes
			p=^00${pland}${pmobile}
			if [ $LOGLEVEL -ge 2 ]; then echo "Country:$pcountry; landline=$landline; prefix=$p; rate=$rate"; fi
			echo "${p};${3};${rate};${pcountry} Mobile" >>$2
		fi
	done
	if [ $c -eq 0 ]; then

		if [ $LOGLEVEL -ge 1 ]; then 	echo "ERROR: COUNTRY NOT INSIDE countryfile:$country"; fi
	fi

	IFS="$nl"
done

