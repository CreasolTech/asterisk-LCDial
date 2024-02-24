# asterisk-LCDial

LCDial is a [Asterisk](https://www.asterisk.org) dial application that implements the **Least Cost Routing / FailOver**

It reads from a MySQL / MariaDB table the available phone providers and tariffs, getting a list of providers that can be used for the current outbound call.
It try using the best provider, and if it fails, choose the next one until the call is made successfully.

It's experimental software, that have to be compiled for your asterisk version.

Written by Paolo Subiaco https://www.creasol.it

Based on LCDial written by Wolfgang Pichler (wp@commoveo.com)

## LCDial working principles	

All data for routing is taken from two tables in the database specified into
lcdial.conf file, named 

* ***lcdial_providers*** which contains the provider name, the dialstring, and a boolean field to enable/disable it

* ***lcdial_rates*** which contains a regular expression to match one or more prefixes, the provider name (need to be equal to that defined in ***lcdial_providers*** table),
	rate (expressed in currency/minutes, for example EUR/min or USD/min) and a note field to describe the record, if you want.


## LCDial: How to install it?

A bash script is provided to install LCDial. You need MySQL client/server and libs, make/gcc compiler, asterisk include files or sources, ...

Type **./install.sh** to start installing.


## LCDial: How to use it?

In ***extensions.conf***, just replace "Dial" with "LCDial" to make a call using 
Least Cost Routing/FailOver, specifying only the number to CALL and, optionally,
the dial options separated by a ',', i.e.

```
LCDial(043812345678,m)  ; dial 043812345678 with music until the 
                        ; called party answer this call

LCDial(043812345678,U)  ; this is a feature introduced at 2008-09-14 in LCDial:
                        ; if the first option is 'U' LCDial just make the call
                        ; with only one attempt to dial, using the best provider
                        ; Note: IT'S IMPORTANTE THAT 'U' options is the first one

LCDial(043812345678)    ; Use the default dial options												
```

## LCDial: How to report bugs or suggest new features?
Use GitHub issue to send bugs report of feature requests, or open a pull request to include function or adapt the sources to new version of Asterisk


## Credits
Sebastian Gutierrez (scgm@adinet.com.uy)
	Porting to asterisk 1.6.

Wolfgang Pichler (wp@commoveo.com)
	Who started this project.
