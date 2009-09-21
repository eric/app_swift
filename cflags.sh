#!/bin/sh

OSARCH=`uname -s`

CFLAGS="-g -Wall -D_REENTRANT -D_GNU_SOURCE -fPIC"

if [ ${OSARCH} = "Darwin" ]; then
  INCDIR=/Library/Asterisk/include
  CFLAGS="${CFLAGS} -I/Library/Asterisk/include"
else
  INCDIR=/usr/include
fi

CHANNEL_H=${INCDIR}/asterisk/channel.h
if [ "`grep 'struct ast_callerid cid' ${CHANNEL_H}`" != "" ]; then
    CFLAGS="${CFLAGS} -DCHANNEL_HAS_CID"
fi

CONFIG_H=${INCDIR}/asterisk/config.h
if [ "`grep 'ast_config_load' ${CONFIG_H}`" != "" ]; then
    CFLAGS="${CFLAGS} -DNEW_CONFIG"
fi

echo "${CFLAGS}"
