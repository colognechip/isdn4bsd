/*-
 * Copyright (c) 2005 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: $
 */

/* system includes */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/endian.h>
#include <i4b/include/capi20.h>
#include <errno.h>

/*---------------------------------------------------------------------------*
 *	usage display and exit
 *---------------------------------------------------------------------------*/
static void
usage(void)
{
    fprintf
      (stderr, 
       "\n"
       "\n" "capitest - CAPI selftest, version %d.%02d, compiled %s %s"
       "\n" "usage: capitest [-u controller] [-d level] [-i telno] [-o telno] [-p value]"
       "\n" "                [-n dialouts]"
       "\n" "       -u <unit>     specify controller unit to use"
       "\n" "       -d <level>    set debug level"	
       "\n" "       -i <telno>    incoming telephone number"
       "\n" "       -o <telno>    outgoing telephone number"
       "\n" "       -p <value>    set CIP value"
       "\n" "       -n <value>    number of dialouts"
       "\n" "       -s            write received data to stdout"
       "\n"
       "\n"

       , CAPI_STACK_VERSION/100, CAPI_STACK_VERSION % 100, __DATE__, __TIME__);

    exit(1);
}

#define TELNO_MAX 128 /* including terminating zero */

static u_int8_t src_telno[TELNO_MAX];
static u_int8_t dst_telno[TELNO_MAX];
static u_int8_t controller = 0;
static u_int8_t verbose_level = 0;
static u_int8_t write_data_to_stdout = 0;
static u_int8_t cip_value = 2; /* default: unrestricted data */
static u_int16_t num_calls_curr = 0;
static u_int16_t num_calls_max = 1;

/*---------------------------------------------------------------------------*
 *      A-law to u-law conversion
 *---------------------------------------------------------------------------*/
static u_int8_t a2u_tab[256] = {
/* 00 */        0x2a, 0x2b, 0x28, 0x29, 0x2e, 0x2f, 0x2c, 0x2d, 
/* 08 */        0x22, 0x23, 0x20, 0x21, 0x26, 0x27, 0x24, 0x25, 
/* 10 */        0x39, 0x3a, 0x37, 0x38, 0x3d, 0x3e, 0x3b, 0x3c, 
/* 18 */        0x31, 0x32, 0x30, 0x30, 0x35, 0x36, 0x33, 0x34, 
/* 20 */        0x0a, 0x0b, 0x08, 0x09, 0x0e, 0x0f, 0x0c, 0x0d, 
/* 28 */        0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05, 
/* 30 */        0x1a, 0x1b, 0x18, 0x19, 0x1e, 0x1f, 0x1c, 0x1d, 
/* 38 */        0x12, 0x13, 0x10, 0x11, 0x16, 0x17, 0x14, 0x15, 
/* 40 */        0x62, 0x63, 0x60, 0x61, 0x66, 0x67, 0x64, 0x65, 
/* 48 */        0x5d, 0x5d, 0x5c, 0x5c, 0x5f, 0x5f, 0x5e, 0x5e, 
/* 50 */        0x74, 0x76, 0x70, 0x72, 0x7c, 0x7e, 0x78, 0x7a, 
/* 58 */        0x6a, 0x6b, 0x68, 0x69, 0x6e, 0x6f, 0x6c, 0x6d, 
/* 60 */        0x48, 0x49, 0x46, 0x47, 0x4c, 0x4d, 0x4a, 0x4b, 
/* 68 */        0x40, 0x41, 0x3f, 0x3f, 0x44, 0x45, 0x42, 0x43, 
/* 70 */        0x56, 0x57, 0x54, 0x55, 0x5a, 0x5b, 0x58, 0x59, 
/* 78 */        0x4f, 0x4f, 0x4e, 0x4e, 0x52, 0x53, 0x50, 0x51, 
/* 80 */        0xaa, 0xab, 0xa8, 0xa9, 0xae, 0xaf, 0xac, 0xad, 
/* 88 */        0xa2, 0xa3, 0xa0, 0xa1, 0xa6, 0xa7, 0xa4, 0xa5, 
/* 90 */        0xb9, 0xba, 0xb7, 0xb8, 0xbd, 0xbe, 0xbb, 0xbc, 
/* 98 */        0xb1, 0xb2, 0xb0, 0xb0, 0xb5, 0xb6, 0xb3, 0xb4, 
/* a0 */        0x8a, 0x8b, 0x88, 0x89, 0x8e, 0x8f, 0x8c, 0x8d, 
/* a8 */        0x82, 0x83, 0x80, 0x81, 0x86, 0x87, 0x84, 0x85, 
/* b0 */        0x9a, 0x9b, 0x98, 0x99, 0x9e, 0x9f, 0x9c, 0x9d, 
/* b8 */        0x92, 0x93, 0x90, 0x91, 0x96, 0x97, 0x94, 0x95, 
/* c0 */        0xe2, 0xe3, 0xe0, 0xe1, 0xe6, 0xe7, 0xe4, 0xe5, 
/* c8 */        0xdd, 0xdd, 0xdc, 0xdc, 0xdf, 0xdf, 0xde, 0xde, 
/* d0 */        0xf4, 0xf6, 0xf0, 0xf2, 0xfc, 0xfe, 0xf8, 0xfa, 
/* d8 */        0xea, 0xeb, 0xe8, 0xe9, 0xee, 0xef, 0xec, 0xed, 
/* e0 */        0xc8, 0xc9, 0xc6, 0xc7, 0xcc, 0xcd, 0xca, 0xcb, 
/* e8 */        0xc0, 0xc1, 0xbf, 0xbf, 0xc4, 0xc5, 0xc2, 0xc3, 
/* f0 */        0xd6, 0xd7, 0xd4, 0xd5, 0xda, 0xdb, 0xd8, 0xd9, 
/* f8 */        0xcf, 0xcf, 0xce, 0xce, 0xd2, 0xd3, 0xd0, 0xd1
};

#define MAX_BDATA_LEN 1024

#ifndef MAKE_ENUM
#define __MAKE_ENUM(enum, value, args...) enum value,
#define MAKE_ENUM(what, end) \
enum { what(__MAKE_ENUM) end };
#endif

#ifndef MAKE_TABLE
#define __MAKE_TABLE(a...) a    /* double pass to expand all macros */
#define _MAKE_TABLE(a...) (a),  /* add comma */
#define MAKE_TABLE(m,field,p,a...) m##_##field p = { __MAKE_TABLE(m(m##_##field _MAKE_TABLE)) a }
#endif

#define STATES_MESSAGE(enum,value,desc,message) message
#define STATES_DESC(   enum,value,desc,message) desc
#define STATES(m)\
m(ST_IDLE      ,, "IDLE",\
  "press ENTER to dial number ...."\
)\
m(ST_INCOMING  ,, "INCOMING CALL",\
  "press ENTER to connect "\
  "or ESCAPE to ignore ...."\
)\
m(ST_OUTGOING  ,, "DIALING",\
  "press ESCAPE to disconnect ...."\
)\
m(ST_ACTIVE    ,, "CONNECTED",\
  "press ESCAPE to disconnect ...."\
)\
m(ST_ON_HOLD   ,, "CALL ON HOLD",\
  ""\
)\
/**/

#define EVENTS_DESC(enum,value,desc) #enum
#define EVENTS(m)\
m(EV_CALL_IN     ,,)\
m(EV_DISCONNECT  ,,)\
m(EV_CONNECTED   ,,)\
m(EV_DATA_IND    ,,)\
m(EV_DATA_CONF   ,,)\
/**/

MAKE_ENUM(STATES,
	  N_STATES);

MAKE_ENUM(EVENTS,
        N_EVENTS);

static const char *
capi_event_to_string(u_int8_t event)
{
  static const char *
    MAKE_TABLE(EVENTS,DESC,[N_EVENTS]);

  return 
    (event >= N_EVENTS) ? "unknown event" : EVENTS_DESC[event];
}

static const char *
capi_state_to_string(u_int8_t state)
{
  static const char *
    MAKE_TABLE(STATES,DESC,[N_STATES]);

  return
    (state >= N_STATES) ? "unknown state" : STATES_DESC[state];
}

struct call_desc {
  u_int8_t state;

  u_int8_t no_disconnect_req;

  u_int16_t num;
  u_int32_t cid;

  void * data_ptr;
  u_int16_t data_len;
  u_int16_t data_handle;

  u_int16_t wReason;

  u_int16_t wCIP;

  char dst_telno[TELNO_MAX];
  char src_telno[TELNO_MAX];

  void *priv_sc;
  void *priv_fifo;

  struct call_desc *next;
};

static struct call_desc *cd_root = NULL;
static u_int16_t message_number = 1;
static u_int32_t app_id = 0;
static u_int16_t receive_count = 0;

static struct call_desc *
cd_by_cid(u_int32_t cid)
{
    struct call_desc *cd;

    /* we just match the PLCI, hence there
     * is only one active B-channel
     */

    cd = cd_root;
    while(cd)
    {
        if((cd->cid != CAPI_CID_UNUSED) &&
	   (((cd->cid ^ cid) & 0xFFFF) == 0))
	{
	    return cd;
	}
	cd = cd->next;
    }
    return cd;
}

static struct call_desc *
cd_by_num(u_int16_t num)
{
    struct call_desc *cd;

    cd = cd_root;
    while(cd)
    {
        if((cd->num != 0) &&
	   (cd->num == num))
	{
	    return cd;
	}
	cd = cd->next;
    }
    return cd;
}

static void
cd_free(struct call_desc *cd)
{
    struct call_desc *next = cd->next;

    bzero(cd, sizeof(*cd));

    cd->next = next;
    return;
}

static struct call_desc *
cd_alloc(u_int16_t cid)
{
    struct call_desc *cd;

    cd = cd_root;
    while(cd)
    {
        if((cd->num == 0) &&
	   (cd->cid == 0) &&
	   (cd->state == 0))
	{
	    break;
	}
	cd = cd->next;
    }

    if(cd == NULL)
    {
        cd = (struct call_desc *)malloc(sizeof(*cd));

	if(cd)
	{
	  bzero(cd, sizeof(*cd));

	  cd->next = cd_root;
	  cd_root = cd;
	}
    }

    if(cd)
    {
      if(cid < 0x100)
	cd->state = ST_OUTGOING;
      else
	cd->state = ST_INCOMING;

      cd->cid = cid;
    }

    return cd;
}

static u_int8_t test_message[] = { "a small test message ....\n" };

static u_int16_t
capi_send_data_b3_req(struct call_desc *cd, 
		      void *data, u_int16_t len);

static u_int16_t
capi_send_connect_resp(struct call_desc *cd, u_int16_t wReject);

static u_int16_t
capi_send_disconnect_req(struct call_desc *cd);

static void
bit_reverse(u_int8_t *ptr, u_int16_t len)
{
  u_int8_t bit;
  u_int8_t temp;

  while(len--)
  {
    temp = 0;
    for(bit=0; bit<8; bit++)
      if(ptr[0] & (1<<bit)) temp |= 1 << (7-bit);

   *ptr++ = a2u_tab[temp];
  }
  return;
}

static void
make_call();

static void
cd_event(struct call_desc *cd, u_int8_t event)
{
    if(verbose_level > 1)
      fprintf(stderr, "%s: %s: got event=%d\n",
	      __FILE__, __FUNCTION__, event);

    switch(event) {
    case EV_DISCONNECT:
      fprintf(stderr, "%s: %s: disconnected: %s\n",
	      __FILE__, __FUNCTION__, capi_get_error_string(cd->wReason));

      if(cd->no_disconnect_req == 0)
      {	
	  capi_send_disconnect_req(cd);
      }
      cd_free(cd);
      break;

    case EV_DATA_IND:
      if(verbose_level > 1)
	fprintf(stderr, "received: %d bytes\n",
		cd->data_len);

      if(write_data_to_stdout)
      {
	  if(cip_value == 1)
	    bit_reverse((u_int8_t *)cd->data_ptr, cd->data_len);

	  fwrite(cd->data_ptr, cd->data_len, 1, stdout);
      }
      break;


    case EV_CALL_IN:

      if(src_telno[0] &&
	 (strcmp(&cd->dst_telno[0], &src_telno[0]) == 0) &&
	 (cd->wCIP == cip_value))
      {
	  fprintf(stderr, "%s: %s: answering call\n",
		 __FILE__, __FUNCTION__);

	  capi_send_connect_resp(cd, 0);
      }
      break;

    case EV_CONNECTED:

      make_call();

    case EV_DATA_CONF:

      capi_send_data_b3_req(cd, &test_message, 
			    sizeof(test_message));
      break;

    default:
      break;
    }
    return;
}

static __inline void
capi_decode_struct(void *ptr, struct capi_struct *mp)
{
	u_int16_t len;

	if(((u_int8_t *)(ptr))[0] == 0xFF)
	{
	  len = 
	    ((u_int8_t *)(ptr))[1] |
	    (((u_int8_t *)(ptr))[2] << 8);
	  ptr = ADD_BYTES(ptr, 3);
	}
	else
	{
	  len = 
	    ((u_int8_t *)(ptr))[0];
	  ptr = ADD_BYTES(ptr, 1);
	}
	mp->ptr = ptr;
	mp->len = len;
	return;
}

static u_int16_t
__capi_put_message_decoded(struct capi_message_decoded *mp, const char *where)
{
    u_int16_t error;

    error = capi_put_message_decoded(mp);

    if(error)
    {
	fprintf(stderr, "%s: ERROR: %s, wCmd=%s\n",
		where,
		capi_get_error_string(error),
		capi_get_command_string(mp->head.wCmd));
    }
    return error;
}

#define capi_put_message_decoded(mp) __capi_put_message_decoded(mp, __FUNCTION__)

static u_int16_t
capi_send_listen_request(unsigned controller, u_int8_t incoming_calls)
{
    struct capi_message_decoded msg;

    bzero(&msg, sizeof(msg));

    fprintf(stderr, "%s: %s: sending listen request for %s\n",
	    __FILE__, __FUNCTION__, 
	    incoming_calls ? "incoming_calls" : "network events");
   
    msg.head.wApp  = app_id;
    msg.head.wCmd  = CAPI_P_REQ(LISTEN);
    msg.head.dwCid = controller;

    msg.data.LISTEN_REQ.dwInfoMask = 
      CAPI_INFO_MASK_CAUSE |
      CAPI_INFO_MASK_DATE_TIME |
      CAPI_INFO_MASK_DISPLAY |
      CAPI_INFO_MASK_USER_USER |
      CAPI_INFO_MASK_CALL_PROGRESSION |
      CAPI_INFO_MASK_FACILITY |
      CAPI_INFO_MASK_CHARGING |
      CAPI_INFO_MASK_CALLED_PARTY_NUMBER |
      CAPI_INFO_MASK_CHANNEL_ID |
      CAPI_INFO_MASK_REDIRECTION_INFO |
      CAPI_INFO_MASK_SENDING_COMPLETE;

    if(incoming_calls)
    {
        msg.data.LISTEN_REQ.dwCipMask1 = 
	  1 |
	  CAPI_CIP_MASK1(SPEECH) |
	  CAPI_CIP_MASK1(UNRESTRICTED_DATA) |
	  CAPI_CIP_MASK1(3100Hz_AUDIO) |
	  CAPI_CIP_MASK1(7kHz_AUDIO) |
	  CAPI_CIP_MASK1(UNRESTRICTED_DATA_TONES) |
	  CAPI_CIP_MASK1(TELEPHONY) |
	  CAPI_CIP_MASK1(FAX_G2_G3) |
	  CAPI_CIP_MASK1(7kHz_TELEPHONY);
    }
    return capi_put_message_decoded(&msg);
}

static u_int16_t data_handle = 0;

static u_int16_t
capi_send_data_b3_req(struct call_desc *cd, 
		      void *data, u_int16_t len)
{
    struct capi_message_decoded msg;

    bzero(&msg, sizeof(msg));

    msg.head.wApp = app_id;
    msg.head.wCmd = CAPI_P_REQ(DATA_B3);
    msg.head.dwCid = cd->cid;

    DATA_B3_REQ_DATA(&msg) = (u_int8_t *)data;
    DATA_B3_REQ_DATALENGTH(&msg) = len;
    DATA_B3_REQ_DATAHANDLE(&msg) = data_handle++;

    return capi_put_message_decoded(&msg);
}

static u_int16_t
capi_send_alert_req(struct call_desc *cd)
{
    struct capi_message_decoded msg;

    bzero(&msg, sizeof(msg));

    msg.head.wApp = app_id;
    msg.head.wCmd = CAPI_P_REQ(ALERT);
    msg.head.dwCid = cd->cid;

    return capi_put_message_decoded(&msg);
}

static u_int16_t
capi_send_connect_resp(struct call_desc *cd, u_int16_t wReject)
{
    struct capi_message_decoded msg;

    bzero(&msg, sizeof(msg));

    msg.head.wApp = app_id;
    msg.head.wCmd = CAPI_P_RESP(CONNECT);
    msg.head.dwCid = cd->cid;

    msg.data.CONNECT_RESP.wReject = wReject;

    CONNECT_RESP_BPROTOCOL(&msg) = CAPI_DEFAULT;
    CONNECT_RESP_ADDITIONALINFO(&msg) = CAPI_DEFAULT;

    return capi_put_message_decoded(&msg);
}

static u_int16_t
capi_send_disconnect_req(struct call_desc *cd)
{
    struct capi_message_decoded msg;

    bzero(&msg, sizeof(msg));

    msg.head.wApp = app_id;
    msg.head.wCmd = CAPI_P_REQ(DISCONNECT);
    msg.head.dwCid = cd->cid;

    return capi_put_message_decoded(&msg);
}

static u_int16_t
capi_send_connect_req(struct call_desc *cd)
{
    struct capi_message_decoded msg;
    u_int8_t dst_telno[TELNO_MAX+3];
    u_int8_t src_telno[TELNO_MAX+3];
    u_int16_t len;

    bzero(&msg, sizeof(msg));

    msg.head.wApp = app_id;
    msg.head.wCmd = CAPI_P_REQ(CONNECT);
    msg.head.dwCid = cd->cid;

    msg.head.wNum = message_number;

    cd->num = message_number;

    message_number += 2;

    msg.data.CONNECT_REQ.wCIP = cd->wCIP;

    len = strlen(&cd->dst_telno[0]);

    if(len == 0)
    {
        dst_telno[0] = 0; /* overlap sending */
    }
    else
    {
        if(len > TELNO_MAX)
	   len = TELNO_MAX;

	dst_telno[0] = len + 1;
	dst_telno[1] = 0x80;
	bcopy(&cd->dst_telno[0], &dst_telno[2], len);

	if(dst_telno[0] == 0xFF)
	  dst_telno[0] = 0xFE; /* avoid escape code */

	msg.data.CONNECT_REQ.dst_telno.ptr = &dst_telno[0];
    }

    len = strlen(&cd->src_telno[0]);

    if(len == 0)
    {
        src_telno[0] = 0;
    }
    else
    {
        if(len > TELNO_MAX)
	  len = TELNO_MAX;

	src_telno[0] = len + 2;
	src_telno[1] = 0x00;
	src_telno[2] = 0x80; /* not restricted */
	bcopy(&cd->src_telno[0], &src_telno[3], len);

	if(src_telno[0] == 0xFF)
	  src_telno[0] = 0xFE; /* avoid escape code */

	msg.data.CONNECT_REQ.src_telno.ptr = &src_telno[0];
    }

    CONNECT_REQ_ADDITIONALINFO(&msg) = CAPI_DEFAULT;
    CONNECT_REQ_BPROTOCOL(&msg) = CAPI_DEFAULT;

    return capi_put_message_decoded(&msg);
}

static void
make_call()
{
    struct call_desc *cd;

    if(num_calls_curr < num_calls_max)
    {
        num_calls_curr ++;
	printf("dialing out, %d / %d ...\n",
	       num_calls_curr, num_calls_max);

	cd = cd_alloc(controller);

	if(cd)
	{
	    snprintf(&cd->dst_telno[0],
		     sizeof(cd->dst_telno),
		     "%s", &dst_telno[0]);

	    cd->src_telno[0] = 0;

	    cd->wCIP = cip_value; 

	    capi_send_connect_req(cd);
	}
    }
    return;
}

/*---------------------------------------------------------------------------*
 *	handle an incoming CAPI message
 *---------------------------------------------------------------------------*/
static void
capi_message_handler(const struct capi_message_decoded *mp)
{
    struct capi_message_decoded msg;
    struct call_desc *cd;

    u_int8_t buffer[2048];

    if(verbose_level > 4)
    {
        capi_message_decoded_to_string(&buffer[0], sizeof(buffer), mp);

	fprintf(stderr, "%s: got message\n" "%s\n",
		__FUNCTION__, &buffer[0]);
    }

    msg.head.wCmd = 0;

    switch(mp->head.wCmd) {

      /*
       * CAPI confirmations
       */
    case CAPI_P_CONF(DATA_B3):
        cd = cd_by_cid(mp->head.dwCid);
        if(cd)
	{
	    cd->data_ptr = NULL;
	    cd->data_len = 0;
	    cd->data_handle = mp->data.DATA_B3_CONF.wHandle;

	    cd_event(cd, EV_DATA_CONF);
	}
        break;

    case CAPI_P_CONF(CONNECT):
        cd = cd_by_num(mp->head.wNum);
        if(cd)
	{
	    cd->wReason = 0;

	    if(mp->data.CONNECT_CONF.wInfo)
	    {
	      cd->wReason = mp->data.CONNECT_CONF.wInfo;

	      cd->no_disconnect_req = 1;
	      cd_event(cd, EV_DISCONNECT);
	    }
	    else
	      cd->cid = mp->head.dwCid;
	}
	break;

    case CAPI_P_CONF(CONNECT_B3):
        cd = cd_by_cid(mp->head.dwCid);
        if(cd && mp->data.CONNECT_B3_CONF.wInfo)
	{
	    cd->wReason = 0;

	    cd_event(cd, EV_DISCONNECT);
	}
	break;

    case CAPI_P_CONF(DISCONNECT):
    case CAPI_P_CONF(DISCONNECT_B3):
    case CAPI_P_CONF(ALERT):
    case CAPI_P_CONF(INFO):
    case CAPI_P_CONF(MANUFACTURER):
        break;

    case CAPI_P_CONF(SELECT_B_PROTOCOL):
        cd = cd_by_cid(mp->head.dwCid);
        if(cd && mp->data.SELECT_B_PROTOCOL_CONF.wInfo)
	{
	    cd->wReason = 0;
	    cd_event(cd, EV_DISCONNECT);
	}
	break;

    case CAPI_P_CONF(FACILITY):
        break;

    case CAPI_P_CONF(RESET_B3):
        cd = cd_by_cid(mp->head.dwCid);
        if(cd && mp->data.RESET_B3_CONF.wInfo)
	{
	    cd->wReason = 0;
	    cd_event(cd, EV_DISCONNECT);
	}
	break;

    case CAPI_P_CONF(LISTEN):
        if(mp->data.LISTEN_CONF.wInfo)
	{
	    fprintf(stderr, "cannot listen, wInfo=0x%04x\n",
		    mp->data.LISTEN_CONF.wInfo);
	}
        break;

      /*
       * CAPI indications
       */

    case CAPI_P_IND(CONNECT):

        cd = cd_by_cid(mp->head.dwCid);

	if(cd == NULL)
	  cd = cd_alloc(mp->head.dwCid);

	if(cd)
	{
	  struct capi_struct str;

	  capi_decode_struct(mp->data.CONNECT_IND.dst_telno.ptr, &str);

	  if(str.len >= TELNO_MAX)
	    str.len = TELNO_MAX-1;

	  if(str.len >= 1)
	    str.len -= 1;

	  str.ptr = ADD_BYTES(str.ptr, 1);

	  bcopy(str.ptr, &cd->dst_telno[0], str.len);
	  cd->dst_telno[str.len] = 0;

	  capi_decode_struct(mp->data.CONNECT_IND.src_telno.ptr, &str);

	  if(str.len >= TELNO_MAX)
	    str.len = TELNO_MAX-1;

	  if(str.len >= 1)
	    str.len -= 1;

	  if(str.len >= 1)
	    str.len -= 1;

	  str.ptr = ADD_BYTES(str.ptr, 2);

	  bcopy(str.ptr, &cd->src_telno[0], str.len);
	  cd->src_telno[str.len] = 0;

	  cd->wCIP = mp->data.CONNECT_IND.wCIP;

	  cd_event(cd, EV_CALL_IN);
	}
	break;

    case CAPI_P_IND(FACILITY):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(FACILITY);
	msg.data.FACILITY_RESP.wSelector =
	  mp->data.FACILITY_IND.wSelector;
	break;

    case CAPI_P_IND(DATA_B3):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        /* make sure that memory referenced
	 * by "wHandle" gets freed
	 */
        msg.head.wCmd = CAPI_P_RESP(DATA_B3);
        msg.data.DATA_B3_RESP.wHandle =
	  mp->data.DATA_B3_IND.wHandle;

        cd = cd_by_cid(mp->head.dwCid);
        if(cd)
	{
	    cd->data_ptr = DATA_B3_IND_DATA(mp);
	    cd->data_len = DATA_B3_IND_DATALENGTH(mp);
	    cd->data_handle = mp->data.DATA_B3_IND.wHandle;

	    cd_event(cd, EV_DATA_IND);
	}
	break;

    case CAPI_P_IND(CONNECT_ACTIVE):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(CONNECT_ACTIVE);

        cd = cd_by_cid(mp->head.dwCid);
        if(cd && (cd->state == ST_OUTGOING))
	{
	    (void) capi_put_message_decoded(&msg);

	    bzero(&msg, sizeof(msg));

	    msg.head.wApp = app_id;
	    msg.head.wCmd = CAPI_P_REQ(CONNECT_B3);
	    msg.head.dwCid = cd->cid;
	}
        break;

    case CAPI_P_IND(CONNECT_B3):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(CONNECT_B3);
        break;

    case CAPI_P_IND(CONNECT_B3_ACTIVE):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(CONNECT_B3_ACTIVE);

        cd = cd_by_cid(mp->head.dwCid);
	if(cd)
	{
	    cd_event(cd, EV_CONNECTED);
	}
        break;

    case CAPI_P_IND(CONNECT_B3_T90_ACTIVE):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(CONNECT_B3_T90_ACTIVE);
	/* XXX not supported */
        break;

    case CAPI_P_IND(DISCONNECT):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(DISCONNECT);

        cd = cd_by_cid(mp->head.dwCid);
	if(cd)
	{
	    cd->wReason = mp->data.DISCONNECT_IND.wReason;
	    cd->no_disconnect_req = 1;
	    cd_event(cd, EV_DISCONNECT);
	}
        break;

    case CAPI_P_IND(DISCONNECT_B3):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(DISCONNECT_B3);
	/* XXX ignore */
        break;

    case CAPI_P_IND(ALERT):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(ALERT);
        break;

    case CAPI_P_IND(INFO):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(INFO);
        break;

    case CAPI_P_IND(RESET_B3):
        bzero(&msg, sizeof(msg));
        msg.head = mp->head;
        msg.head.wCmd = CAPI_P_RESP(RESET_B3);
        break;

    default:
        /* nothing to do */
        return;
    }

    if(msg.head.wCmd)
    {
        (void) capi_put_message_decoded(&msg);
    }
    return;
}

static void
loop()
{
    int error;

    while(1)
    {
        struct pollfd pfd[2];

	bzero(&pfd, sizeof(pfd));

	pfd[0].fd = capi20_fileno(app_id);
	pfd[0].events = POLLIN|POLLRDNORM;

	pfd[1].fd = 0;
	pfd[1].events = POLLIN|POLLRDNORM;

	error = poll(&pfd[0], 2, -1);

	if(error == -1)
	{
		fprintf(stderr, "%s: %s: poll error: %s\n",
			__FILE__, __FUNCTION__, strerror(errno));
	    break;
	}

	if(error > 0)
	{
	    if(pfd[0].revents & (POLLIN|POLLRDNORM))
	    {
	        struct capi_message_decoded msg;

		while(1)
		{
		    if(capi_get_message_decoded(&msg, app_id))
		        break;

		    capi_message_handler(&msg);
		}
	    }

	    if(pfd[1].revents & (POLLIN|POLLRDNORM))
	    {
	      int c;
	      while(1)
	      {
		c = getchar();

		if(c == -1) break;

		if(verbose_level > 3)
		  fprintf(stderr, "%s: keypress: %c\n",
			  __FUNCTION__, c);
	      }
	    }
	}
    }
    return;
}

/*---------------------------------------------------------------------------*
 *	list all controllers installed
 *---------------------------------------------------------------------------*/
static u_int16_t
capi_init_all_controllers(u_int8_t listen)
{
   struct CAPI_PROFILE_DATA_ENCODED profile;
   u_int16_t error;
   u_int16_t unit;

   /* get number of installed controllers */

   error = capi20_get_profile(0, &profile, sizeof(profile));
   if(error)
   {
       fprintf(stderr, "%s: %s: get profile returned error: %s\n",
	       __FILE__, __FUNCTION__, 
	       capi_get_error_string(error));

       return error;
   }

   unit = le16toh(profile.wNumCtlr);
   if(unit == 0)
   {
       fprintf(stderr, "%s: %s: no controllers installed\n",
	       __FILE__, __FUNCTION__);
       return 0;
   }

   while(unit--)
   {
       error = capi20_get_profile(unit+1, &profile, sizeof(profile));
       if(error == 0)
       {
	 if(verbose_level > 1)
	   fprintf(stderr,
		   "controller           =0x%02x\n\n"
		   "  wNumCtlr           =0x%04x\n"
		   "  wNumBChannels      =0x%04x\n"
		   "  dwGlobalOptions    =0x%08x\n"
		   "  dwB1ProtocolSupport=0x%08x\n"
		   "  dwB2ProtocolSupport=0x%08x\n"
		   "  dwB3ProtocolSupport=0x%08x\n"
		   "\n",
		   unit+1,
		   profile.wNumCtlr,
		   profile.wNumBChannels,
		   profile.dwGlobalOptions,
		   profile.dwB1ProtocolSupport,
		   profile.dwB2ProtocolSupport,
		   profile.dwB3ProtocolSupport);
       }

       error = capi_send_listen_request(unit+1, listen);
       if(error)
	 fprintf(stderr,
		 "%s: %s: capi send listen request failed, controller=0x%02x, error=%s\n",
		 __FILE__, __FUNCTION__, unit+1, capi_get_error_string(error));
   }
   return 0;
}

int
main(int argc, char **argv)
{
    u_int32_t temp;
    u_int16_t error;
    int c;

    while ((c = getopt(argc, argv, "c:u:d:i:o:p:sn:")) != -1)
    {
	switch(c) {
	case 'c':
	case 'u':
		controller = atoi(optarg);
		break;

	case 'd':
		verbose_level = atoi(optarg);
		break;

	case 'o':
		snprintf(&dst_telno[0], sizeof(dst_telno), "%s", optarg);
		break;

	case 'i':
		snprintf(&src_telno[0], sizeof(src_telno), "%s", optarg);
		break;

	case 'p':
		cip_value = atoi(optarg);
		break;

	case 's':
		write_data_to_stdout = 1;
		break;

	case 'n':
		num_calls_max = atoi(optarg);
		break;

	case '?':
	default:
		usage();
		break;
	}
    }

    if((dst_telno[0] == 0) && (src_telno[0] == 0)) usage();

    error = capi20_is_installed();
    if(error)
    {
        fprintf(stderr, "CAPI 2.0 not installed!\n");
	return -1;
    }

    /* register at CAPI, only two connections will be established */
    error = capi20_register (2, 7, MAX_BDATA_LEN, &temp);
    if(error)
    {
        fprintf(stderr, "%s: %s: could not register by CAPI, error=%s\n",
		__FILE__, __FUNCTION__, capi_get_error_string(error));
	return -1;
    }

    app_id = temp;

    (void) capi_init_all_controllers(1);

    if(dst_telno[0])
    {
        make_call();
    }

    loop();

    /* release CAPI application */
    error = capi20_release (app_id);
    if(error)
    {
        fprintf(stderr, "%s: %s: could not release CAPI application! error=%s\n",
		__FILE__, __FUNCTION__,  capi_get_error_string(error));
	return -1;
    }
    return 0;
}