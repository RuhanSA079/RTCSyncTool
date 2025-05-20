#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>

/*
 Changelog:
 version 0.1 -> Initial version, BQ32K GET, ISL1208 support WIP, added HCTOSYS for BQ32K.
 version 0.2 -> Added ISL1208 GET, added ISL1208 HCTOSYS, added root check.
 version 0.3 -> Added driver probe (wip), added force unbind option, added system time printout, added SYSTOHC for BQ32K.
 version 0.4 -> Added SYSTOHC for ISL1208. Improved driver bind check.
 version 0.5 -> Improve error messages, improve output and improved bind/unbind logic & filepaths.
 version 0.6 -> Add force command for the systohc and hctosys commands
 version 0.7 -> Fixed 12/24h representation according to the ISL1208 datasheet
*/

const uint8_t BQ32K = 0x68;
const uint8_t ISL1208 = 0x6f;
const int CMD_ACTION_GET = 0;
const int CMD_ACTION_SYSTOHC = 1;
const int CMD_ACTION_HCTOSYS = 2;

int write_sysfs(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) {
        //perror("ERR: FOPEN FAILED\n");
        return -1;
    }
    if (fprintf(f, "%s", value) < 0) {
        //perror("ERR: FPRINTF FAILED\n");
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

// Function to unbind a device driver
int unbind_device(const char *device, const char *driver) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/bus/i2c/drivers/%s/unbind", driver);
    return write_sysfs(path, device);
}

// Function to bind a device driver
int bind_device(const char *device, const char *driver) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/bus/i2c/drivers/%s/bind", driver);
    return write_sysfs(path, device);
}

void rootCheck(){
    uid_t uid = getuid();
    if (uid != 0) {
        printf("ERR: EXEC WITH NO ROOT\n");
        exit(1);
    }
}

void unbindDevices(uint8_t addr){
    int res = 0;
    if (addr == BQ32K){
        res = unbind_device("0-0068", "bq32k");
        if (res != 0){
            res = unbind_device("0-0068", "rtc-bq32k");
        }
    }
    if (addr == ISL1208){
        res = unbind_device("0-006f", "isl1208");
        if (res != 0){
            res = unbind_device("0-006f", "rtc-isl1208");
        }
    }
}

void rebindDevices(uint8_t addr){
    int res = 0;
    if (addr == BQ32K){
        res = bind_device("0-0068", "bq32k");
        if (res != 0){
            res = bind_device("0-0068", "rtc-bq32k");
        }
    }

    if (addr == ISL1208){
        res = bind_device("0-006f", "isl1208");
        if (res != 0){
            res = bind_device("0-006f", "rtc-isl1208");
        }
    }
}

int probeI2CDevice(int fd, uint8_t addr, uint8_t forceUnbind){
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        if (forceUnbind == 1){
            //printf("Unbinding driver...\n");
            unbindDevices(addr);

            if (ioctl(fd, I2C_SLAVE, addr) < 0) {
                printf("ERR: FAILED TO TALK TO SLAVE 0x%02x AFTER UNBIND\n", addr);
                return 1;
            }else{
                char buf[1];
                if (read(fd, buf, 1) == 1) {
                    //printf("Device found at address 0x%02x\n", addr);
                    return 0;
                } else {
                    //printf("No device at address 0x%02x\n", addr);
                    return 1;
                }
            }
        }else{
            printf("ERR: FAILED TO TALK TO SLAVE 0x%02x\n", addr);
        }
        return 1;
    }

    char buf[1];
    if (read(fd, buf, 1) == 1) {
        //printf("Device found at address 0x%02x\n", addr);
        return 0;
    } else {
        //printf("No device at address 0x%02x\n", addr);
        return 1;
    }
}

void printHelp(){
    printf("\nRTCSyncTool usage:\nReading the RTC -> ./RTCSyncTool get\nSet system time from RTC -> ./RTCSyncTool hctosys\nSet RTC Time from System -> ./RTCSyncTool systohc\nTo force read the i2c device, just add 'force' to your command.\n");
}

int i2c_reg_read_byte(int fd, uint8_t addr, uint8_t regaddr, uint8_t* content) 
{
	struct i2c_rdwr_ioctl_data iocall;    // structure pass to i2c driver
	struct i2c_msg i2c_msgs[2];

	iocall.nmsgs = 2;
	iocall.msgs = i2c_msgs;

	i2c_msgs[0].addr = addr;
	i2c_msgs[0].flags = 0; //write
	i2c_msgs[0].buf = (char*) &regaddr;
	i2c_msgs[0].len = 1;

	i2c_msgs[1].addr = addr;
	i2c_msgs[1].flags = I2C_M_RD; //READ
	i2c_msgs[1].buf = (char*) content;
	i2c_msgs[1].len = 1;

	if (ioctl(fd, I2C_RDWR, (unsigned long) &iocall) < 0) {
		printf("ERR: %s:%s \n", __func__, strerror(errno));
		return -1;
	}

	return 0;
}

int i2c_reg_write_byte(int fd, uint8_t addr, uint8_t regaddr, uint8_t content) 
{
	struct i2c_rdwr_ioctl_data iocall;    // structure pass to i2c driver
	struct i2c_msg i2c_msgs;
	uint8_t buffer[2];

	buffer[0] = regaddr;
	buffer[1] = content;

	iocall.nmsgs = 1;
	iocall.msgs = &i2c_msgs;

	i2c_msgs.addr = addr;
	i2c_msgs.flags = 0; //write
	i2c_msgs.buf = (char*) buffer;
	i2c_msgs.len = sizeof(buffer);

	if (ioctl(fd, I2C_RDWR, (unsigned long) &iocall) < 0) {
		printf("ERR: %s:%s \n", __func__, strerror(errno));
		return -1;
	}

	return 0;
}

int BCDtoInt(unsigned char bcd) {
    // Extract the high nibble (first 4 bits) and low nibble (last 4 bits)
    int highNibble = (bcd >> 4) & 0xF; // Shift right by 4 bits and mask with 0xF
    int lowNibble = bcd & 0xF;         // Mask with 0xF to get the last 4 bits

    // Combine the nibbles to get the final integer
    int val = highNibble * 10 + lowNibble;

    return val;
}

uint8_t intToBCD(int val) {
    return ((val/10*16) + (val%10));
}

int calculateDayOfWeek(int d, int m, int y)
{
    static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    y -= m < 3;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void processISL1208Time(uint8_t RTCseconds, uint8_t RTCminutes, uint8_t RTChours, uint8_t RTCweekday, uint8_t RTCday, uint8_t RTCmonth, uint8_t RTCyear, bool printTime, bool setTime, uint8_t RTC_StatusReg){
    //hwclock output: 2019-09-20 11:08:05.566357+00:00
    bool isTwentyFourHours = false;
    bool isPM = false;
    int secondsCalc;
    int minutesCalc;
    int hoursCalc;
    int dayCalc;
    int monthCalc;
    int yearCalc;
    int weekdayCalc;

    uint8_t ones;
    uint8_t tens;

    secondsCalc = BCDtoInt(RTCseconds);
    minutesCalc = BCDtoInt(RTCminutes);

    // 24-hour check fix
    if ((RTChours & 0x80) != 0){
        isTwentyFourHours = true;
    }else{
        isTwentyFourHours = false;
    }

    // Hour extraction corrected
    if (!isTwentyFourHours){
        if ((RTChours & 0x20) != 0){
            // PM
            hoursCalc = BCDtoInt(RTChours & 0x1F);
            if (hoursCalc != 12){
                hoursCalc += 12;
            }
        }else{
            // AM
            hoursCalc = BCDtoInt(RTChours & 0x1F);
            if (hoursCalc == 12){
                hoursCalc = 0;
            }
        }
    }else{
        // Corrected: Keep bits 6:0 for 24h BCD
        hoursCalc = BCDtoInt(RTChours & 0x7F);
    }

    dayCalc = BCDtoInt(RTCday);
    monthCalc = BCDtoInt(RTCmonth);
    yearCalc = BCDtoInt(RTCyear) + 2000;
    weekdayCalc = BCDtoInt(RTCweekday);

    int dayOfWeekCalc = calculateDayOfWeek(dayCalc, monthCalc, yearCalc);
    int isClockRunning = (RTCseconds & 0x10);

    if (dayOfWeekCalc != RTCweekday){
        printf("WRN: RTC Weekday out of sync!\n");
        printf("RTC Weekday: %d\n", RTCweekday);
        printf("Calculated weekday: %d\n", dayOfWeekCalc);
    }

    if (isClockRunning == 0){
        printf("WRN: RTC Oscillator has stopped!\n");
    }

    if (printTime){
        printf("RTC: %04d-%02d-%02d %02d:%02d:%02d.000000+00:00\n", yearCalc, monthCalc, dayCalc, hoursCalc, minutesCalc, secondsCalc);
        printf("TYP: ISL1208\n");
    }

    if (setTime){
        // Fixed: allocate buffer for datetime string
        char datetimeSet[32];
        struct tm tm;
        struct timeval tv;

        sprintf(datetimeSet, "%04d-%02d-%02d %02d:%02d:%02d", yearCalc, monthCalc, dayCalc, hoursCalc, minutesCalc, secondsCalc);

        if (strptime(datetimeSet, "%Y-%m-%d %H:%M:%S", &tm) == NULL) {
            fprintf(stderr, "TIME-DATE PARSE FAIL\n");
            return;
        }

        time_t t = mktime(&tm);
        if (t == -1) {
            printf("mktime() call failed!\n");
            return;
        }

        tv.tv_sec = t;
        tv.tv_usec = 0;

        if (settimeofday(&tv, NULL) < 0) {
            printf("HCTOSYS FAIL\n");
            return;
        }

        printf("HCTOSYS OK\n");
    }
}

void processBQ32KTime(uint8_t RTCseconds, uint8_t RTCminutes, uint8_t RTChours, uint8_t RTCweekday, uint8_t RTCday, uint8_t RTCmonth, uint8_t RTCyear, bool printTime, bool setTime){
    //hwclock output: 2019-09-20 11:08:05.566357+00:00

    int secondsCalc;
    int minutesCalc;
    int hoursCalc;
    int dayCalc;
    int monthCalc;
    int yearCalc;

    uint8_t ones;
    uint8_t tens;

    ones = RTCseconds & 0x0F;
    tens = (RTCseconds & 0x70) >> 4;
    secondsCalc = ((10 * tens) + ones);

    ones = RTCminutes & 0x0F;
    tens = (RTCminutes & 0x70) >> 4;
    minutesCalc = ((10 * tens) + ones);

    ones = RTChours & 0x0F;
    tens = (RTChours & 0x30) >> 4;
    hoursCalc = ((10 * tens) + ones);

    ones = RTCday & 0x0F;
    tens = (RTCday & 0x30) >> 4;
    dayCalc = ((10 * tens) + ones);

    ones = RTCmonth & 0x0F;
    tens = (RTCmonth & 0x10) >> 4;
    monthCalc = ((10 * tens) + ones);

    ones = RTCyear & 0x0F;
    tens = (RTCyear & 0xF0) >> 4;
    yearCalc = ((10 * tens) + ones) + 2000;


    // printf("Calculated hours: %d\n", hoursCalc);
    // printf("Calculated minutes: %d\n", minutesCalc);
    // printf("Calculated seconds: %d\n", secondsCalc);
    // printf("Calculated day: %d\n", dayCalc);
    // printf("Calculated month: %d\n", monthCalc);
    // printf("Calculated year: %d\n", yearCalc);


    int dayOfWeekCalc = calculateDayOfWeek(dayCalc, monthCalc, yearCalc);
    int isClockRunning = (RTCseconds & 0x80);


    dayOfWeekCalc += 1;
    if (dayOfWeekCalc != RTCweekday){
        printf("WRN: RTC Weekday out of sync!\n");
        printf("RTC Weekday: %d\n", RTCweekday);
        printf("Calculated weekday: %d\n", dayOfWeekCalc);
    }

    //hwclock output: 2019-09-20 11:08:05.566357+00:00
    if (isClockRunning == 1){
        printf("WRN: RTC Oscillator has stopped!\n");
    }

    if (printTime){
        printf("RTC: %04d-%02d-%02d %02d:%02d:%02d.000000+00:00\n", yearCalc, monthCalc, dayCalc, hoursCalc, minutesCalc, secondsCalc);
        printf("TYP: BQ32K\n");
    }

    //Set the system time from the RTC!
    if (setTime){
        // Define the new time as a string
        char* datetimeSet;
        struct tm tm;
        struct timeval tv;

        //convert the calculated RTC time to the date string.
        sprintf(datetimeSet, "%04d-%02d-%02d %02d:%02d:%02d", yearCalc, monthCalc, dayCalc, hoursCalc, minutesCalc, secondsCalc);

        // Parse the date and time string
        if (strptime(datetimeSet, "%Y-%m-%d %H:%M:%S", &tm) == NULL) {
            fprintf(stderr, "ERR: TIME-DATE PARSE FAIL\n");
            return;
        }

        // Convert the parsed time to a time_t (Unix timestamp)
        time_t t = mktime(&tm);
        if (t == -1) {
            printf("ERR: mktime() call failed!\n");
            return;
        }

        // Set the timeval structure
        tv.tv_sec = t;
        tv.tv_usec = 0;

        // Set the system time
        if (settimeofday(&tv, NULL) < 0) {
            printf("HCTOSYS FAIL\n");
            return;
        }

        printf("HCTOSYS OK\n");
    }
}

void readISL1208(int fd, bool printTime, bool setSystemTime){
    uint8_t addr = ISL1208;
    uint8_t regaddr = 0x00; // Register offset to read from

    uint8_t seconds;    // in reg 0x00
    uint8_t minutes;    // in reg 0x01
    uint8_t hours;      // in reg 0x02
    uint8_t day;        // in reg 0x03
    uint8_t month;      // in reg 0x04
    uint8_t year;       // in reg 0x05
    uint8_t weekday;    // in reg 0x06
    uint8_t rtcStatus;   // in reg 0x07

    int err = 0;

    if (i2c_reg_read_byte(fd, addr, regaddr, &seconds) == 0) {
        //printf("Read seconds: %d\n", seconds);
        regaddr += 1;

        if (i2c_reg_read_byte(fd, addr, regaddr, &minutes) == 0){
            //printf("Read minutes: %d\n", minutes);
            regaddr += 1;

            if (i2c_reg_read_byte(fd, addr, regaddr, &hours) == 0){
                //printf("Read hours: %d\n", hours);
                regaddr += 1;

                if (i2c_reg_read_byte(fd, addr, regaddr, &day) == 0){
                    //printf("Read day: %d\n", day);
                    regaddr += 1;

                    if (i2c_reg_read_byte(fd, addr, regaddr, &month) == 0){
                        //printf("Read month: %d\n", month);
                        regaddr += 1;

                        if (i2c_reg_read_byte(fd, addr, regaddr, &year) == 0){
                            //printf("Read year: %d\n", year);
                            regaddr += 1;

                            if (i2c_reg_read_byte(fd, addr, regaddr, &weekday) == 0){
                                //printf("Read weekday: %d\n", weekday);
                                regaddr += 1;
                                if (i2c_reg_read_byte(fd, addr, regaddr, &rtcStatus) == 0){
                                    //printf("Read status: %d\n", rtcStatus);
                                    processISL1208Time(seconds, minutes, hours, weekday, day, month, year, printTime, setSystemTime, rtcStatus);
                                }else{
                                    printf("ERR: Failed to read the 'status register' from the ISL1208 chip!\n");
                                }
                            }else{
                                printf("ERR: Failed to read the 'weekday' from the ISL1208 chip!\n");
                            }
                        }else{
                            printf("ERR: Failed to read the 'year' from the ISL1208 chip!\n");
                        }
                    }else{
                        printf("ERR: Failed to read the 'month' from the ISL1208 chip!\n");
                    }
                }else{
                    printf("ERR: Failed to read the 'day' from the ISL1208 chip!\n");
                }
            }else{
                printf("ERR: Failed to read the 'hours' from the ISL1208 chip!\n");
            }
        }else{
            printf("ERR: Failed to read the 'minutes' from the ISL1208 chip!\n");
        }
    } else {
        printf("ERR: Failed to read the 'seconds' from the ISL1208 chip!\n");
    }
}

void readBQ32K(int fd, bool printTime, bool setSystemTime){
    uint8_t addr = BQ32K;
    uint8_t regaddr = 0x00; // Register to read from

    uint8_t seconds;    // in reg 0x00
    uint8_t minutes;    // in reg 0x01
    uint8_t hours;      // in reg 0x02

    uint8_t weekday;    // in reg 0x03
    uint8_t day;        // in reg 0x04
    uint8_t month;      //in reg 0x05
    uint8_t year;       //in reg 0x06

    int err = 0;

    if (i2c_reg_read_byte(fd, addr, regaddr, &seconds) == 0) {
        //printf("Read seconds: %d\n", seconds);
        regaddr += 1;

        if (i2c_reg_read_byte(fd, addr, regaddr, &minutes) == 0){
            //printf("Read minutes: %d\n", minutes);
            regaddr += 1;

            if (i2c_reg_read_byte(fd, addr, regaddr, &hours) == 0){
                //printf("Read hours: %d\n", hours);
                regaddr += 1;

                if (i2c_reg_read_byte(fd, addr, regaddr, &weekday) == 0){
                    //printf("Read weekday: %d\n", weekday);
                    regaddr += 1;

                    if (i2c_reg_read_byte(fd, addr, regaddr, &day) == 0){
                        //printf("Read day: %d\n", day);
                        regaddr += 1;

                        if (i2c_reg_read_byte(fd, addr, regaddr, &month) == 0){
                            //printf("Read month: %d\n", month);
                            regaddr += 1;

                            if (i2c_reg_read_byte(fd, addr, regaddr, &year) == 0){
                                //printf("Read year: %d\n", year);
                                processBQ32KTime(seconds, minutes, hours, weekday, day, month, year, printTime, setSystemTime);
                            }else{
                                printf("ERR: Failed to read the 'year' from the BQ32K chip!\n");
                            }
                        }else{
                            printf("ERR: Failed to read the 'month' from the BQ32K chip!\n");
                        }
                    }else{
                        printf("ERR: Failed to read the 'day' from the BQ32K chip!\n");
                    }
                }else{
                    printf("ERR: Failed to read the 'weekday' from the BQ32K chip!\n");
                }
            }else{
                printf("ERR: Failed to read the 'hours' from the BQ32K chip!\n");
            }
        }else{
            printf("ERR: Failed to read the 'minutes' from the BQ32K chip!\n");
        }
    } else {
        printf("ERR: Failed to read the 'seconds' from the BQ32K chip!\n");
    }
}

void setBQ32KTime(int fd, int seconds, int minutes, int hours, int day, int month, int year, int weekday){
    uint8_t addr = BQ32K;
    uint8_t regaddr = 0x00; // Register to read from

    if (fd < 0){
        printf("i2c fd error!\n");
        return;
    }

    int ones;
    int tens;

    //Convert the times back to the appropiate format...
    int rtcSeconds;
    int rtcMinutes;
    int rtcHours;
    int rtcDay;
    int rtcMonth;
    int rtcYear;
    int rtcWeekday;

    ones = 0;
    tens = 0;
    seconds %= 60;
    ones = seconds % 10;
    tens = ( seconds / 10 ) << 4;
    rtcSeconds = tens | ones;

    ones = 0;
    tens = 0;
    minutes %= 60;
    ones = minutes % 10;
    tens = ( minutes / 10 ) << 4;
    rtcMinutes = tens | ones;

    ones = 0;
    tens = 0;
    hours %= 24;
    ones = hours % 10;
    tens = ( hours / 10 ) << 4;
    rtcHours = tens | ones;

    ones = 0;
    tens = 0;
    day %= 32;
    if (day == 0){
        day = 1;
    }
    ones = day % 10;
    tens = ( day / 10 ) << 4;
    rtcDay = tens | ones;


    ones = 0;
    tens = 0;
    month %= 13;
    if (month == 0){
        month = 1;
    }
    ones = month % 10;
    tens = ( month / 10 ) << 4;
    rtcMonth = tens | ones;

    ones = 0;
    tens = 0;
    year %= 100;
    ones = year % 10;
    tens = ( year / 10 ) << 4;
    rtcYear = tens | ones;

    weekday %= 8;
    if (weekday == 0){
        weekday = 1;
    }
    rtcWeekday = weekday + 1;

    uint8_t rtcStatus;

    //printf("Setting BQ32K time to: %02d:%02d:%02d %02d/%02d/%02d %02d\n", hours, minutes, seconds, day, month, year, weekday);
    if (i2c_reg_write_byte(fd, addr, regaddr, rtcSeconds) == 0) {
        regaddr += 1;
        if (i2c_reg_write_byte(fd, addr, regaddr, rtcMinutes) == 0){
            regaddr += 1;
            if (i2c_reg_write_byte(fd, addr, regaddr, rtcHours) == 0){
                regaddr += 1;
                if (i2c_reg_write_byte(fd, addr, regaddr, rtcWeekday) == 0){
                    regaddr += 1;
                    if (i2c_reg_write_byte(fd, addr, regaddr, rtcDay) == 0){
                        regaddr += 1;
                        if (i2c_reg_write_byte(fd, addr, regaddr, rtcMonth) == 0){
                            regaddr += 1;
                            if (i2c_reg_write_byte(fd, addr, regaddr, rtcYear) == 0){

                                regaddr = 0x00;
                                if (i2c_reg_read_byte(fd, addr, regaddr, &rtcStatus) == 0){
                                    if ((rtcStatus & 0x80) == 1){
                                        printf("WRN: RTC Oscillator has stopped, starting...\n");
                                        rtcStatus &= 0x7F;
                                        if (i2c_reg_write_byte(fd, addr, regaddr, rtcStatus) == 0){
                                            printf("SYSTOHC OK\n");
                                            //printf("BQ32K time successfully set to: %04d-%02d-%02d %02d:%02d:%02d\n", (2000 + year), month, day, hours, minutes, seconds);
                                        }else{
                                            printf("BQ32K: Failed to start the RTC oscillator!\n");
                                        }
                                    }else{
                                        printf("SYSTOHC OK\n");
                                        //printf("BQ32K time successfully set to: %04d-%02d-%02d %02d:%02d:%02d\n", (2000 +year), month, day, hours, minutes, seconds);
                                    }
                                }else{
                                    printf("ERR: Failed to read the 'status register' from the BQ32K chip!\n");
                                }
                            }else{
                                printf("ERR: Failed to write the 'year' to the BQ32K chip!\n");
                            }
                        }else{
                            printf("ERR: Failed to write the 'month' to the BQ32K chip!\n");
                        }
                    }else{
                        printf("ERR: Failed to write the 'day' to the BQ32K chip!\n");
                    }
                }else{
                    printf("ERR: Failed to write the 'weekday' to the BQ32K chip!\n");
                }
            }else{
                printf("ERR: Failed to write the 'hours' to the BQ32K chip!\n");
            }
        }else{
            printf("ERR: Failed to write the 'minutes' to the BQ32K chip!\n");
        }
    } else {
        printf("ERR: Failed to write the 'seconds' to the BQ32K chip!\n");
    }
}

void enableISL1208WRTCBit(int fd){
    uint8_t addr = ISL1208;
    uint8_t regaddr = 0x07; // Register to write to. (Seconds)

    if (fd < 0){
        printf("i2c fd error!\n");
        return;
    }

    uint8_t rtcStatus;

    if (i2c_reg_read_byte(fd, addr, regaddr, &rtcStatus) == 0) {
        rtcStatus |= 0x10; //Set the WRTC bit.
        if (i2c_reg_write_byte(fd, addr, regaddr, rtcStatus) == 0){
            //printf("ISL1208 WRTC bit successfully set!\n");
        }else{
            printf("ERR: Failed to write the 'status register' to the ISL1208 chip!\n");
        }
    }else{
        printf("ERR: Failed to read the 'status register' from the ISL1208 chip!\n");
    }

}

void setISL1208Time(int fd, int seconds, int minutes, int hours, int day, int month, int year, int weekday){
    //According to the datasheet, I will have to write a WRTC bit on register 0x07, value 0x10 -> 0001 0000.
    //This is to allow the RTC time setting.
 

    uint8_t addr = ISL1208;
    uint8_t regaddr = 0x00; // Register to write to. (Seconds)

    if (fd < 0){
        printf("i2c fd error!\n");
        return;
    }

    enableISL1208WRTCBit(fd);

    uint8_t rtcSeconds;
    uint8_t rtcMinutes;
    uint8_t rtcHours;
    uint8_t rtcDay;
    uint8_t rtcMonth;
    uint8_t rtcYear;
    uint8_t rtcWeekday;

    rtcSeconds = intToBCD(seconds);
    rtcMinutes = intToBCD(minutes);

    uint8_t rtcHoursBCD = intToBCD(hours); //Force 24h mode...
    rtcHours = rtcHoursBCD | 0x80; //Set the 24h bit.

    rtcDay = intToBCD(day);
    rtcMonth = intToBCD(month);
    rtcYear = intToBCD(year - 2000);
    rtcWeekday = intToBCD(weekday);

    if (i2c_reg_write_byte(fd, addr, regaddr, rtcSeconds) == 0) {
        regaddr += 1;
        if (i2c_reg_write_byte(fd, addr, regaddr, rtcMinutes) == 0){
            regaddr += 1;
            if (i2c_reg_write_byte(fd, addr, regaddr, rtcHours) == 0){
                regaddr += 1;
                if (i2c_reg_write_byte(fd, addr, regaddr, rtcDay) == 0){
                    regaddr += 1;
                    if (i2c_reg_write_byte(fd, addr, regaddr, rtcMonth) == 0){
                        regaddr += 1;
                        if (i2c_reg_write_byte(fd, addr, regaddr, rtcYear) == 0){
                            regaddr += 1;
                            if (i2c_reg_write_byte(fd, addr, regaddr, rtcWeekday) == 0){
                                printf("SYSTOHC OK\n");
                                //printf("ISL1208 time successfully set to: %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hours, minutes, seconds);
                            }else{
                                printf("ERR: Failed to write the 'weekday' to the ISL1208 chip!\n");
                            }
                        }else{
                            printf("ERR: Failed to write the 'year' to the ISL1208 chip!\n");
                        }
                    }else{
                        printf("ERR: Failed to write the 'month' to the ISL1208 chip!\n");
                    }
                }else{
                    printf("ERR: Failed to write the 'day' to the ISL1208 chip!\n");
                }
            }else{
                printf("ERR: Failed to write the 'hours' to the ISL1208 chip!\n");
            }
        }else{
            printf("ERR: Failed to write the 'minutes' to the ISL1208 chip!\n");
        }
    }else{
        printf("ERR: Failed to write the 'seconds' to the ISL1208 chip!\n");
    }
}

void printSysTime(){
    int sysSeconds;
    int sysMinutes;
    int sysHours;
    int sysDay;
    int sysMonth;
    int sysYear;

    //Get time system time
    time_t currentTime;
    time(&currentTime);

    // Convert to local time format
    struct tm *localTime = localtime(&currentTime);
    sysSeconds = localTime->tm_sec;
    sysMinutes = localTime->tm_min;
    sysHours = localTime->tm_hour;
    sysDay = localTime->tm_mday;
    sysMonth = localTime->tm_mon + 1;
    sysYear = localTime->tm_year + 1900;

    // Print the local time
    printf("SYS: %04d-%02d-%02d %02d:%02d:%02d.000000+00:00\n", sysYear, sysMonth, sysDay, sysHours, sysMinutes, sysSeconds);
}

int main(int argc, char *argv[]) {
    int chip = 0;
    int action = 0;
    int forceUnbindRebind = 0;

    int rtcHours;
    int rtcMinutes;
    int rtcSeconds;
    int rtcDay;
    int rtcMonth;
    int rtcYear;

    printf("RTCSyncTool v0.7 by RuhanSA079\n");
    rootCheck();

    if (argc == 1){
        printf("ERR: NO ARGS\n");
        printHelp();
        return 1;
    }
    if (argc > 3){
        printf("ERR: TOO MANY ARGS\n");
        printHelp();
        return 1;
    }

    //Process the action from the commandline:
    if (strcmp(argv[1], "get") == 0){
        action = CMD_ACTION_GET;
        if (argc > 2){
            if (strcmp(argv[2], "force") == 0){
                forceUnbindRebind = 1;
            }
        }
    }else if (strcmp(argv[1], "hctosys") == 0){
        action = CMD_ACTION_HCTOSYS;
	if (argc > 2){
            if (strcmp(argv[2], "force") == 0){
                forceUnbindRebind = 1;
            }
        }
    }else if (strcmp(argv[1], "systohc") == 0){
        action = CMD_ACTION_SYSTOHC;
	if (argc > 2){
            if (strcmp(argv[2], "force") == 0){
                forceUnbindRebind = 1;
            }
        }
    }else{
        printf("ERR: UNKNOWN COMMAND\n");
        exit(1);
    }

    //printf("Opening i2c bus: %s\n", argv[1]);

    int fd;
    fd = open("/dev/i2c-0", O_RDWR);
    if (fd < 0) {
        printf("ERR: FAILED TO OPEN I2C BUS\n");
        exit(1);
    }

    //printf("i2c bus now open, probing i2c bus for BQ32K and ISL1208...\n");

    //ISL1208
    int ret = probeI2CDevice(fd, ISL1208, forceUnbindRebind);
    if (ret == 0){
        chip = ISL1208;
    }

    //BQ32K
    ret = probeI2CDevice(fd, BQ32K, forceUnbindRebind);
    if (ret == 0){
        chip = BQ32K;
    }


    if (chip == BQ32K || chip == ISL1208){
        //printf("RTC found, reading data...\n");
        if (action == CMD_ACTION_GET){
            printSysTime();

            if (chip == ISL1208){
                readISL1208(fd, true, false);
            }

            if (chip == BQ32K){
                readBQ32K(fd, true, false);
            }
        }else if (action == CMD_ACTION_HCTOSYS){
            printSysTime();
            //Set the system date from the RTC...
            if (chip == ISL1208){
                readISL1208(fd, true, true);
            }

            if (chip == BQ32K){
                readBQ32K(fd, true, true);
            }
        }else if (action == CMD_ACTION_SYSTOHC){
            //hwclock output: 2019-09-20 11:08:05.566357+00:00

            int sysSeconds;
            int sysMinutes;
            int sysHours;
            int sysDay;
            int sysMonth;
            int sysYear;
            int sysWeekday;


            //Get time system time
            time_t currentTime;
            time(&currentTime);

            // Convert to local time format
            struct tm *localTime = localtime(&currentTime);
            sysSeconds = localTime->tm_sec;
            sysMinutes = localTime->tm_min;
            sysHours = localTime->tm_hour;
            sysDay = localTime->tm_mday;
            sysMonth = localTime->tm_mon + 1;
            sysYear = localTime->tm_year + 1900;

            sysWeekday = calculateDayOfWeek(sysDay, sysMonth, sysYear);

            // Print the local time
            printf("SYS: %04d-%02d-%02d %02d:%02d:%02d.000000+00:00\n", sysYear, sysMonth, sysDay, sysHours, sysMinutes, sysSeconds);

            //Set the RTC from the system time/
            if (chip == ISL1208){
                readISL1208(fd, true, false);
                setISL1208Time(fd, sysSeconds, sysMinutes, sysHours, sysDay, sysMonth, sysYear, sysWeekday);
            }

            if (chip == BQ32K){
                readBQ32K(fd, true, false);
                setBQ32KTime(fd, sysSeconds, sysMinutes, sysHours, sysDay, sysMonth, sysYear, sysWeekday);
            }
        }


        if (forceUnbindRebind == 1){
            //printf("Rebinding driver...\n");
            rebindDevices(chip);
        }
    } else {
        printf("ERR: FAILED TO DETECT/READ RTC\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
