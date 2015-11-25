#!/bin/sh

HEADER=$1
sed -e "/^%HEADER%$/ {
    r ../$HEADER
    d
}" ${HEADER}.in |
sed -E -e 's/[[:<:]]uchar[[:>:]]/unsigned char/g' \
       -e 's/[[:<:]]ushort[[:>:]]/unsigned short/g' \
       -e 's/[[:<:]]uint[[:>:]]/unsigned int/g' \
       -e 's/[[:<:]]ulong[[:>:]]/unsigned long/g' \
       -e 's/[[:<:]]uvlong[[:>:]]/unsigned long long/g' \
       -e 's/[[:<:]]vlong[[:>:]]/long long/g' \
       -e 's/[[:<:]]byte[[:>:]]/uint8_t/g' \
       -e 's/[[:<:]]rune[[:>:]]/uint32_t/g' \
       -e 's/[[:<:]](u?)int(8|16|32|64|ptr)[[:>:]]/\1int\2_t/g' \
       -e 's/[[:<:]]noreturn[[:>:]]/_Noreturn/g' \
       -e 's/[[:<:]]AUTOLINK([A-Za-z0-9]\+)//g' \
       -e 's/"(stdatomic.h)"/<\1>/'
