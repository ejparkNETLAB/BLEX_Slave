#!/usr/bin/expect -f 

spawn west flash
expect "Please select one with desired serial number (1-4):"
send "2\n";
interact
