# RTCSyncTool

This tool is intended as a experimental solution and way to communicate with the i2c RTC chips (BQ32K or ISL1208) and set the system time or sync the RTC time, following the commandline options the same as hwclock.
(hctosys and systohc)

# Notice
I do not take any responsibility for any damaged hardware or any sort of damage caused by this code, please use this code at your own risk!
this code is marked as HIGHLY EXPERIMENTAL, please use this responsibly!

# Reasons
I wrote this code for a sample Linux gateway that had some trouble with the time sync, as it did not have any Linux kernel drivers compiled for this two types of RTC, 
and were using a SVNS RTC as the RTC, instead of these two (BQ32K or a ISL1208), and had to demo this to some DevOps engineers.

This code is not used anymore, I dumped this on here (for archival purposes), so if anyone has the same trouble with a similar setup, you can use this code, at your own risk!
