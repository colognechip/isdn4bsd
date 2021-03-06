/*-
 * Copyright (c) 1997, 2002 Hellmuth Michaelis. All rights reserved.
 *
 * Copyright (c) 2004-2006 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
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
 *---------------------------------------------------------------------------
 *
 *	dss1_l3encoder.h - layer 3 encoder
 *	----------------------------------
 *
 * $FreeBSD: $
 *
 *---------------------------------------------------------------------------*/

static const uint8_t MAKE_TABLE(L3_STATES,Q931_CONV,[]);

static uint8_t *
IEI_channelid(struct call_desc *cd, uint8_t *ptr)
{
    DSS1_TCP_pipe_t *pipe = cd->pipe;
    l2softc_t *sc = pipe->L5_sc;
    uint8_t *ptr_old = ptr;

    /* only accept this channel */
    enum { ext_exclusive = 0x80 | 0x08 }; 

    *ptr = IEI_CHANNELID;  /* channel id */

    ptr += 2;

    switch(cd->channel_id) {
    default:
        *ptr = IE_CHAN_ID_ANY | ext_exclusive;
	break;
    case CHAN_NOT_ANY:
        *ptr = IE_CHAN_ID_NO | ext_exclusive;
	break;
    case CHAN_D1:
        *ptr = 0x04 | ext_exclusive;
	break;
    case CHAN_B1:
        *ptr = IE_CHAN_ID_B1 | ext_exclusive;
	break;
    case CHAN_B2:
        *ptr = IE_CHAN_ID_B2 | ext_exclusive;
	break;
    }

    if(IS_PRIMARY_RATE(sc))
    {
        /* Primary Rate */

        *ptr |= 0x20;

	if(cd->channel_id >= CHAN_B1)
	{
	    int32_t channel_id = cd->channel_id;

	    L1_COMMAND_REQ(sc->sc_cntl,CMR_ENCODE_CHANNEL,&channel_id);

	    /* channel is indicated in the following octets */
	    *ptr &= ~0x03;
	    *ptr |= 0x01;

	    ptr++;

	    *ptr = 0x80 | 0x03; /* B-channel */

	    ptr++;

	    *ptr = channel_id | 0x80;
	}
    }
    ptr++;

    ptr_old[1] = ptr - ptr_old - 2;

    return ptr;
}

static uint32_t
get_callreference(uint8_t *ptr)
{
    uint8_t len = ptr[0] & CRLENGTH_MASK;
    uint32_t cr = 0;

    ptr++;

    while(len)
    {
        len--;
	cr <<= 8;
	cr |= ptr[len];
    }
    return cr;
}

/*---------------------------------------------------------------------------*
 *	make_callreference - generate the DSS1 call reference value
 *---------------------------------------------------------------------------*/
static uint8_t *
make_callreference(DSS1_TCP_pipe_t *pipe, uint32_t cr, uint8_t *ptr)
{
    l2softc_t *sc = pipe->L5_sc;
    uint8_t len = 
      (cr & 0xFF000000) ? 4 :
      (cr & 0x00FF0000) ? 3 :
      (cr & 0x0000FF00) ? 2 : 1;

    if(IS_PRIMARY_RATE(sc) && (len < 2))
    {
        /* minimum 2 bytes are required by
	 * primary rate:
	 */
        len = 2;
    }

    *ptr++ = len; /* call reference length */

    while(len)
    {
        ptr[0] = cr;

	cr >>= 8;
	ptr++;
	len--;
    }
    return ptr;
}

/*---------------------------------------------------------------------------*
 *	send SETUP message
 *---------------------------------------------------------------------------*/
static void
dss1_l3_tx_setup(call_desc_t *cd)
{
	struct mbuf *m;
	uint8_t *ptr;
	char *str;
	struct i4b_src_telno *p_src;
	int len;

	NDBGL3(L3_PRIM, "cdid=%d, cr=%d",
	       cd->cdid, cd->cr);

	m = i4b_getmbuf(BCH_MAX_DATALEN, M_NOWAIT);

	if(m)
	{
	  ptr = mtod(m, uint8_t *) + I_HEADER_LEN;
	
	  *ptr++ = PD_Q931;		/* protocol discriminator */
	   ptr   = make_callreference(cd->pipe,cd->cr,ptr);
	  *ptr++ = SETUP;		/* message type = setup */

	  if(cd->sending_complete)
	  {
	      *ptr++ = IEI_SENDCOMPL;	/* sending complete */
	  }

	  *ptr++ = IEI_BEARERCAP;	/* bearer capability */

	  switch(cd->channel_bprot) {
	  case BPROT_NONE_3_1_KHZ:        /* 3.1Khz FAX */
	      *ptr++ = IEI_BEARERCAP_LEN+1;
	      *ptr++ = IT_CAP_AUDIO_3100Hz;
	      *ptr++ = IT_RATE_64K;
	      switch(cd->channel_bsubprot) {
	      case BSUBPROT_G711_ALAW:
		  *ptr++ = IT_UL1_G711A;
		  break;
	      case BSUBPROT_G711_ULAW:
		  *ptr++ = IT_UL1_G711U;
		  break;
	      default:
		  *ptr++ = 0xA0; /* reserved */
		  break;
	      }
	      break;

	  case BPROT_NONE:        /* telephony */
	  case BPROT_RHDLC_DOV:   /* Data over Voice */
	      *ptr++ = IEI_BEARERCAP_LEN+1;
	      *ptr++ = IT_CAP_SPEECH;
	      *ptr++ = IT_RATE_64K;
	      switch(cd->channel_bsubprot) {
	      case BSUBPROT_G711_ALAW:
		  *ptr++ = IT_UL1_G711A;
		  break;
	      case BSUBPROT_G711_ULAW:
		  *ptr++ = IT_UL1_G711U;
		  break;
	      default:
		  *ptr++ = 0xA0; /* reserved */
		  break;
	      }
	      break;

	  case BPROT_RHDLC:       /* raw HDLC */
	  case BPROT_NONE_VOD:    /* Voice over Data */
	  default:
	      *ptr++ = IEI_BEARERCAP_LEN;
	      *ptr++ = IT_CAP_UNR_DIG_INFO;
	      *ptr++ = IT_RATE_64K;
	      break;
	  }

	  if(cd->channel_allocated)
	  {
		ptr = IEI_channelid(cd, ptr);
	  }

	  str = &(cd->keypad[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_KEYPAD;		/* keypad facility */
		*ptr++ = len;			/* keypad facility length */

		bcopy(str, ptr, len);
		ptr += len;
	  }

	  p_src = &(cd->src[0]);

	repeat_src_telno:
	
	  str = &(p_src->telno[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_CALLINGPN;	/* calling party no */
		ptr[0] = len; /* calling party no length */

		/* type of number, number plan id */

		ptr[1] = 
		  (p_src->ton == TON_INTERNAT) ? (NUMBER_TYPE_PLAN | 0x10) :
		  (p_src->ton == TON_NATIONAL) ? (NUMBER_TYPE_PLAN | 0x20) :
		  NUMBER_TYPE_PLAN;

		if ((p_src->prs_ind != PRS_NONE) ||
		    (p_src->scr_ind != SCR_NONE))
		{
		    uint8_t temp;

		    /* clear extension bit */
		    ptr[1] &= ~0x80;

		    temp = 0x80;

		    /* add resentation indicator */
		    if (p_src->prs_ind == PRS_RESTRICT)
			temp |= 0x20;

		    /* add screening indicator */
		    switch (p_src->scr_ind) {
		    case SCR_USR_PASS:
			temp |= 0x01;
			break;
		    case SCR_USR_FAIL:
			temp |= 0x02;
			break;
		    case SCR_NET:
			temp |= 0x03;
			break;
		    default:
			break;
		    }

		    ptr[2] = temp;
		    ptr[0] += 2;
		    ptr += 3;
		}
		else
		{
		    ptr[0] += 1;
		    ptr += 2;
		}

		bcopy(str, ptr, len);
		ptr += len;
	  }

	  str = &(p_src->subaddr[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_CALLINGPS;		/* calling subaddr */
		*ptr++ = NUMBER_TYPE_LEN+len;	/* calling subaddr len */
		*ptr++ = NUMBER_TYPE_NSAP;	/* type = NSAP */

		bcopy(str, ptr, len);
		ptr += len;
	  }

	  p_src++;

	  if((p_src >= &(cd->src[0])) &&
	     (p_src < &(cd->src[2])))
	  {
	      goto repeat_src_telno;
	  }

	  /*
	   * re-transmit the complete destination telephone
	   * number, hence in NT-mode the SETUP message may
	   * be repeated:
	   */
	  str = &(cd->dst_telno[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_CALLEDPN;		/* called party no */
		*ptr++ = NUMBER_TYPE_LEN+len;	/* called party no length */
		*ptr++ = NUMBER_TYPE_PLAN;	/* type of number, number plan id */

		bcopy(str, ptr, len);
		ptr += len;
		str += len;

		cd->dst_telno_ptr = str;
	  }

	  str = &(cd->dst_subaddr[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_CALLEDPS;		/* calling party subaddr */
		*ptr++ = NUMBER_TYPE_LEN+len;	/* calling party subaddr len */
		*ptr++ = NUMBER_TYPE_NSAP;	/* type = NSAP */

		bcopy(str, ptr, len);
		ptr += len;
	  }

	  str = &(cd->user_user[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_USERUSER;		/* user-user */
		*ptr++ = IEI_USERUSER_LEN+len;	/* user-user length */

		bcopy(str, ptr, len);
		ptr += len;
	  }

	  str = &(cd->display[0]);

	  if(str[0] != 0)
	  {
		len = strlen(str);
		*ptr++ = IEI_DISPLAY; /* display */
		*ptr++ = len;         /* display string length */

		bcopy(str, ptr, len);
		ptr += len;
	  }

	/* check length */
#if (I_HEADER_LEN   +\
    3               +\
    4               +\
                \
    1               +\
    1               +\
                \
    4               +\
                \
    5               +\
                \
    2               +\
    KEYPAD_MAX      +\
                \
    (2*3)           +\
    (2*TELNO_MAX)   +\
                \
    (2*3)           +\
    (2*SUBADDR_MAX) +\
                \
    3               +\
    1               +\
    TELNO_MAX       +\
                \
    3               +\
    SUBADDR_MAX     +\
                \
    2               +\
    USER_USER_MAX   +\
		\
    2               +\
    DISPLAY_MAX     +\
                \
    0) > BCH_MAX_DATALEN
#error " > BCH_MAX_DATALEN"
#endif

	  /* update length */
	  m->m_len = ptr - ((__typeof(ptr))m->m_data);

	  dss1_pipe_data_req(cd->pipe,m);
	}
	else
	{
	  NDBGL3(L3_ERR, "out of mbufs!");
	}
	return;
}

#define dss1_l3_tx_alert(cd)			\
	dss1_l3_tx_message(cd,ALERT,		\
			   L3_TX_HEADER)
/*
 * NOTE: if one sends the channel-ID 
 * to some PBXs while in TE-mode, and
 * the call is incoming, the PBX might
 * reject the message and send an
 * error to the "up-link"
 */
#define dss1_l3_tx_setup_acknowledge(cd,send_chan_id,send_progress)	\
	dss1_l3_tx_message(cd,SETUP_ACKNOWLEDGE,			\
			   (L3_TX_HEADER|				\
			    ((send_progress) ? (L3_TX_PROGRESSI) : 0)|	\
			    ((send_chan_id) ? (L3_TX_CHANNELID) : 0)))

#define dss1_l3_tx_call_proceeding(cd,send_chan_id,send_progress)	\
	dss1_l3_tx_message(cd,CALL_PROCEEDING,				\
			   (L3_TX_HEADER|				\
			    ((send_progress) ? (L3_TX_PROGRESSI) : 0)|	\
			    ((send_chan_id) ? (L3_TX_CHANNELID) : 0)))

#define dss1_l3_tx_information(cd)			\
	dss1_l3_tx_message(cd,INFORMATION,		\
			   L3_TX_HEADER|L3_TX_CALLEDPN)

#define dss1_l3_tx_connect(cd, send_date_time)				\
	dss1_l3_tx_message(cd,CONNECT,					\
			   L3_TX_HEADER|				\
			   ((send_date_time) ? L3_TX_DATE_TIME : 0))

#define dss1_l3_tx_connect_acknowledge(cd)		\
	dss1_l3_tx_message(cd,CONNECT_ACKNOWLEDGE,	\
			   L3_TX_HEADER)

#define dss1_l3_tx_disconnect(cd)			\
	dss1_l3_tx_message(cd,DISCONNECT,		\
			   L3_TX_HEADER|L3_TX_CAUSE)

#define dss1_l3_tx_release(cd,send_cause_flag)				\
	dss1_l3_tx_message(cd,RELEASE,(send_cause_flag) ? 		\
			   L3_TX_HEADER|L3_TX_CAUSE : L3_TX_HEADER)

/* NOTE: some ISDN phones require
 * the "cause" information element
 * when sending RELEASE_COMPLETE
 */
#define dss1_l3_tx_release_complete(cd,send_cause_flag)			\
	dss1_l3_tx_message(cd,RELEASE_COMPLETE,(send_cause_flag) ? 	\
			   L3_TX_HEADER|L3_TX_CAUSE : L3_TX_HEADER)

#define dss1_l3_tx_release_complete_complement(cd,p1,p2)		\
	dss1_l3_tx_message_complement(cd,p1,p2,RELEASE_COMPLETE,	\
				      L3_TX_HEADER|L3_TX_CAUSE)

#define dss1_l3_tx_status(cd,q850cause)					\
{									\
	(cd)->cause_out = (q850cause);					\
	dss1_l3_tx_message(cd,STATUS,					\
			   L3_TX_HEADER|L3_TX_CAUSE|L3_TX_CALLSTATE);	\
	(cd)->cause_out = 0;						\
}

#define dss1_l3_tx_status_enquiry(cd)		\
	dss1_l3_tx_message(cd,STATUS_ENQUIRY,	\
			   L3_TX_HEADER)

#define dss1_l3_tx_progress(cd)		\
	dss1_l3_tx_message(cd,PROGRESS,	\
			   L3_TX_HEADER|L3_TX_PROGRESSI)

#define dss1_l3_tx_hold(cd)			\
	dss1_l3_tx_message(cd,HOLD,		\
			   L3_TX_HEADER)

#define dss1_l3_tx_hold_acknowledge(cd)		\
	dss1_l3_tx_message(cd,HOLD_ACKNOWLEDGE,	\
			   L3_TX_HEADER)

#define dss1_l3_tx_hold_reject(cd,q850cause)		\
{							\
	(cd)->cause_out = (q850cause);			\
	dss1_l3_tx_message(cd,HOLD_REJECT,		\
			   L3_TX_HEADER|L3_TX_CAUSE);	\
	(cd)->cause_out = 0;				\
}

#define dss1_l3_tx_retrieve(cd)			\
	dss1_l3_tx_message(cd,RETRIEVE,		\
			   L3_TX_HEADER)

#define dss1_l3_tx_retrieve_acknowledge(cd)		\
	dss1_l3_tx_message(cd,RETRIEVE_ACKNOWLEDGE,	\
			   L3_TX_HEADER|L3_TX_CHANNELID)

#define dss1_l3_tx_deflect_call(cd)			\
	dss1_l3_tx_message(cd,FACILITY,			\
			   L3_TX_HEADER|L3_TX_DEFLECT)

#define dss1_l3_tx_mcid_call(cd)			\
	dss1_l3_tx_message(cd,FACILITY,			\
			   L3_TX_HEADER|L3_TX_MCID_REQ)

#define dss1_l3_tx_retrieve_reject(cd,q850cause)	\
{							\
	(cd)->cause_out = (q850cause);			\
	dss1_l3_tx_message(cd,RETRIEVE_REJECT,		\
			  L3_TX_HEADER|L3_TX_CAUSE);	\
	(cd)->cause_out = 0;				\
}

#define L3_TX_HEADER     0x0000
#define L3_TX_CAUSE      0x0001
#define L3_TX_CALLSTATE  0x0002
#define L3_TX_CHANNELID  0x0004
#define L3_TX_CALLEDPN   0x0008
#define L3_TX_PROGRESSI  0x0010
#define L3_TX_RESTARTI   0x0020
#define L3_TX_DEFLECT    0x0040
#define L3_TX_MCID_REQ   0x0080
#define L3_TX_DATE_TIME  0x0100

/*---------------------------------------------------------------------------*
 *	send message
 *---------------------------------------------------------------------------*/
static void
dss1_l3_tx_message(call_desc_t *cd, uint8_t message_type, uint16_t flag)
{
	DSS1_TCP_pipe_t *pipe = cd->pipe;
	l2softc_t *sc = pipe->L5_sc;
	struct mbuf *m;
	uint8_t *ptr;
	char *str;
	size_t len;

	NDBGL3(L3_PRIM, "cdid=%d, cr=%d, cause=0x%02x, "
	       "state=0x%x, channel_id=0x%x",
	       cd->cdid, cd->cr,
	       cd->cause_out, cd->state, cd->channel_id);

	m = i4b_getmbuf(DCH_MAX_DATALEN, M_NOWAIT);

	if(m)
	{
	  ptr = mtod(m, uint8_t *) + I_HEADER_LEN;
	
	  *ptr++ = PD_Q931;               /* protocol discriminator */
	   ptr   = make_callreference(cd->pipe,cd->cr,ptr);
	  *ptr++ = message_type;          /* message type */

	  if(flag & L3_TX_CAUSE)
	  {
	    *ptr++ = IEI_CAUSE;                      /* cause ie */
	    *ptr++ = IEI_CAUSE_LEN;
	    *ptr++ = NT_MODE(sc) ? CAUSE_STD_LOC_PUBLIC : CAUSE_STD_LOC_OUT;
	    *ptr++ = i4b_make_q850_cause(cd->cause_out)|EXT_LAST;
	  }

	  if(flag & L3_TX_CALLSTATE)
	  {
	    *ptr++ = IEI_CALLSTATE;             /* call state ie */
	    *ptr++ = IEI_CALLSTATE_LEN;
	    *ptr++ = L3_STATES_Q931_CONV[cd->state];
	  }

	  if(flag & L3_TX_CHANNELID)
	  {
	    if(cd->channel_allocated)
	    {
	      ptr = IEI_channelid(cd, ptr);
	    }
	  }

	  /* NOTE: the progress indicator
	   * must be sent after the 
	   * channel-ID, because some
	   * phones will setup the B-channel
	   * immediately when receiving
	   * this message:
	   */
	  if(flag & L3_TX_PROGRESSI)
	  {
	    *ptr++ = IEI_PROGRESSI;
	    *ptr++ = 2; /* bytes */
	    *ptr++ = NT_MODE(sc) ? CAUSE_STD_LOC_PUBLIC : CAUSE_STD_LOC_OUT;
	    *ptr++ = 0x88; /* in-band info available */
	  }

	  if(flag & L3_TX_CALLEDPN)
	  {
	    str = &(cd->dst_telno_early[0]);

	    if(str[0] != 0)
	    {
		len = strlen(str);
		*ptr++ = IEI_CALLEDPN;		/* called party no */
		*ptr++ = NUMBER_TYPE_LEN+len;	/* called party no length */
		*ptr++ = NUMBER_TYPE_PLAN;	/* type of number, number plan id */

		while(len--)
		{
		  *ptr++ = *str++;
		}
	    }
	  }

	  if(flag & L3_TX_DEFLECT)
	  {
	    str = &(cd->dst_telno_part[0]);

	    if(str[0] != 0)
	    {
	        len = strlen(str);

		*ptr++ = IEI_FACILITY; /* Facility IE */
		*ptr++ = 0x10 + len;  /* Length */
		*ptr++ = 0x91; /* Remote Operations Protocol */
		*ptr++ = 0xa1; /* Tag: context specific */

		*ptr++ = 0x0D + len; /* Length */
		*ptr++ = 0x02; /* Tag: Universal, Primitive, INTEGER */ 
		*ptr++ = 0x02; /* Length */
		*ptr++ = 0x22; /* Data: Invoke Identifier = 34 */

		*ptr++ = 0x00; /* Data */
		*ptr++ = 0x02; /* Tag: Universal, Primitive, INTEGER */
		*ptr++ = 0x01; /* Length */
		*ptr++ = 0x0d; /* Data: Operation Value = Call Deflection */

		*ptr++ = 0x30; /* Tag: Universal, Constructor, SEQUENCE */
		*ptr++ = 0x04 + len; /* Length */
		*ptr++ = 0x30; /* Tag: Universal, Constructor, SEQUENCE */
		*ptr++ = 0x02 + len;  /* Length */

		*ptr++ = 0x80; /* Tag: Context specific, Primitive, code = 0 */
		*ptr++ = 0x00 + len; 

		while(len--)
		{
		    *ptr++ = *str++;
		}
	    }
	  }

	  if(flag & L3_TX_MCID_REQ)
	  {
	    *ptr++ = IEI_FACILITY; /* Facility IE */
	    *ptr++ = 0x0a; /* Length */
	    *ptr++ = 0x91; /* Remote Operations Protocol */
	    *ptr++ = 0xa1; /* Tag: Context-specific */

	    *ptr++ = 0x07; /* Length */
	    *ptr++ = 0x02; /* Tag: Universal, Primitive, INTEGER */
	    *ptr++ = 0x02; /* Length */
	    *ptr++ = 0x22; /* Data: Invoke Identifier = 34 */

	    *ptr++ = 0x00; /* Data */
	    *ptr++ = 0x02; /* Tag: Universal, Primitive, INTEGER */
	    *ptr++ = 0x01; /* Length */
	    *ptr++ = 0x03; /* Data: Operation Value = MCID request */
	  }

	  if ((flag & L3_TX_DATE_TIME) &&
	      (cd->odate_time_len)) {
	    *ptr++ = IEI_DATETIME; /* Date/Time IE */
	    if (cd->odate_time_len > 8) {
		cd->odate_time_len = 8;
	    }
	    *ptr++ = cd->odate_time_len;
	    bcopy(cd->odate_time_data, ptr, cd->odate_time_len);
	    ptr += cd->odate_time_len;
	  }


	  /* check length */
#if (I_HEADER_LEN   +\
    3               +\
    4               +\
                \
    4               +\
                \
    4               +\
                \
    3               +\
                \
    5               +\
                \
    3               +\
    TELNO_MAX       +\
		\
    18              +\
    TELNO_MAX       +\
     	        \
    8		    +\
		\
    2               +\
    8               +\
                \
    0) > DCH_MAX_DATALEN
#error " > DCH_MAX_DATALEN"
#endif

	  /* update length */
	  m->m_len = ptr - ((__typeof(ptr))m->m_data);

	  dss1_pipe_data_req(cd->pipe,m);
	}
	else
	{
	  NDBGL3(L3_ERR, "out of mbufs!");
	}
	return;
}

/*---------------------------------------------------------------------------*
 *	send message to the pipes complementing the given pipes
 *---------------------------------------------------------------------------*/
static void
dss1_l3_tx_message_complement(struct call_desc *cd,
			      DSS1_TCP_pipe_t *pipe_skip_1,
			      DSS1_TCP_pipe_t *pipe_skip_2,
			      uint8_t message_type, 
			      uint8_t flag)
{
	DSS1_TCP_pipe_t *pipe_curr;
	DSS1_TCP_pipe_t *pipe_old;
	l2softc_t *sc;

	/* save current pipe pointer and
	 * get pointer to softc
	 */
	pipe_old = cd->pipe;
	sc = pipe_old->L5_sc;

	PIPE_FOREACH(pipe_curr,&sc->sc_pipe[0])
	{
	     /* it might look like some devices do not
	      * check the call reference values too well,
	      * so only send messages to pipes that
	      * have responded to this call
	      */
	    if((pipe_curr != pipe_skip_1) &&
	       (pipe_curr != pipe_skip_2) &&
	       (pipe_curr->state != ST_L2_PAUSE))
	    {
	        cd->pipe = pipe_curr;

		dss1_l3_tx_message(cd, message_type, flag);
	    }
	}

	/* restore pipe pointer */
	cd->pipe = pipe_old;
	return;
}

/*---------------------------------------------------------------------------*
 *	send a message by pipe and call reference
 *---------------------------------------------------------------------------*/
static void
dss1_l3_tx_message_by_pipe_cr(DSS1_TCP_pipe_t *pipe, uint32_t call_ref, 
			      uint8_t message_type, uint8_t flag, 
			      uint8_t cause_out, uint8_t restart_indication)
{
	l2softc_t *sc = pipe->L5_sc;
	struct mbuf *m;
	uint8_t *ptr;

	NDBGL3(L3_PRIM, "call_ref=%d, cause=0x%02x, rst_ind=0x%02x",
	       call_ref, cause_out, restart_indication);

	m = i4b_getmbuf(DCH_MAX_DATALEN, M_NOWAIT);

	if(m)
	{
	  ptr = mtod(m, uint8_t *) + I_HEADER_LEN;
	
	  *ptr++ = PD_Q931;               /* protocol discriminator */
	   ptr   = make_callreference(pipe,call_ref,ptr);
	  *ptr++ = message_type;

	  if(flag & L3_TX_CAUSE)
	  {
	    *ptr++ = IEI_CAUSE;                      /* cause ie */
	    *ptr++ = IEI_CAUSE_LEN;
	    *ptr++ = NT_MODE(sc) ? CAUSE_STD_LOC_PUBLIC : CAUSE_STD_LOC_OUT;
	    *ptr++ = i4b_make_q850_cause(cause_out)|EXT_LAST;
	  }

	  if(flag & L3_TX_RESTARTI)
	  {
	    /* (restart_indication & 7):
	     *   7: restart all interfaces
	     *   6: restart single interface
	     *   0: restart indicated channels
	     */
	    *ptr++ = IEI_RESTARTI;
	    *ptr++ = 1; /* byte */
	    *ptr++ = restart_indication | 0x80; /* restart indication */
	  }

	  /* check length */
#if (I_HEADER_LEN   +\
    3               +\
    4               +\
                \
    4               +\
                \
    3               +\
                \
    0) > DCH_MAX_DATALEN
#error " > DCH_MAX_DATALEN"
#endif

	  /* update length */
	  m->m_len = ptr - ((__typeof(ptr))m->m_data);

	  dss1_pipe_data_req(pipe,m);
	}
	else
	{
	  NDBGL3(L3_ERR, "out of mbufs!");
	}
	return;
}
