/* $FreeBSD$ */
/*-
 * Copyright (c) 1997, 2002 Hellmuth Michaelis. All rights reserved.
 *
 * Copyright (c) 2005, 2011 Hans Petter Selasky. All rights reserved.
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
 *	i4b_i4bdrv.c - i4b userland interface driver
 *	--------------------------------------------
 *
 * Locking order:
 *
 * 1. i4b_global_lock or a controller lock
 *
 * 2. I4B application interface lock
 *
 *---------------------------------------------------------------------------*/

#ifdef I4B_GLOBAL_INCLUDE_FILE
#include I4B_GLOBAL_INCLUDE_FILE
#else
#include <sys/param.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/queue.h>
#include <net/if.h>
#endif

#include <i4b/include/i4b_debug.h>
#include <i4b/include/i4b_ioctl.h>
#include <i4b/include/i4b_cause.h>
#include <i4b/include/i4b_global.h>

#include <i4b/layer4/i4b_l4.h>

struct i4b_ai_softc {

	struct mtx sc_mtx;
#define	I4B_AI_LOCK(sc) mtx_lock(&(sc)->sc_mtx)
#define	I4B_AI_UNLOCK(sc) mtx_unlock(&(sc)->sc_mtx)

	unsigned long sc_refs;

	uint16_t sc_flags;
#define ST_CLOSING        0x0001 /* set if AI is closing */
#define ST_SLEEP_WAKEUP   0x0004 /* set if AI needs wakeup */
#define ST_SLEEP_ENTERED  0x0008 /* set if AI is sleeping */
#define ST_SELECT         0x0010 /* set if AI is selected by poll */
#define ST_IOCTL          0x0020 /* set if AI is doing a IOCTL */
#define ST_BLOCK          0x0080 /* set if I/O is blocking */
#define ST_MBUF_LOST      0x0100 /* set if an mbuf was lost */

	struct _ifqueue sc_rdqueue;
	struct selinfo sc_selinfo;

	TAILQ_ENTRY(i4b_ai_softc) entry;
};

static	d_open_t	i4b_open;
static	d_close_t	i4b_close;
static	d_read_t	i4b_read;
static	d_ioctl_t	i4b_ioctl;
static	d_poll_t	i4b_poll;

static cdevsw_t i4b_cdevsw = {
      .d_version =    D_VERSION,
      .d_open =       i4b_open,
      .d_close =      i4b_close,
      .d_read =       i4b_read,
      .d_ioctl =      i4b_ioctl,
      .d_poll =       i4b_poll,
      .d_name =       "i4b",
      .d_flags =      D_TRACKCLOSE,
};

static TAILQ_HEAD(,i4b_ai_softc) i4b_head = TAILQ_HEAD_INITIALIZER(i4b_head);

static struct cdev *i4b_dev;

#define I4BNAME "i4b"

static struct i4b_ai_softc *
i4b_ai_new_softc(void)
{
	struct i4b_ai_softc *sc;

	/* try to create another unit */

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);

	if (sc == NULL)
		return (NULL);

	/* setup softc */

	sc->sc_rdqueue.ifq_maxlen = 0x80;
	sc->sc_flags = ST_BLOCK;

	mtx_init(&sc->sc_mtx, "I4B AI", NULL, MTX_DEF | MTX_RECURSE);

	mtx_lock(&i4b_global_lock);
	TAILQ_INSERT_TAIL(&i4b_head, sc, entry);
	mtx_unlock(&i4b_global_lock);

	return (sc);
}

static void
i4b_ai_free_softc(void *arg)
{
	struct i4b_ai_softc *sc = (struct i4b_ai_softc *)arg;

	mtx_lock(&i4b_global_lock);
	while (sc->sc_refs != 0) {
		msleep(sc, &i4b_global_lock, PZERO,
		       "I4B AI free", hz / 16);
	}
	TAILQ_REMOVE(&i4b_head, sc, entry);
	mtx_unlock(&i4b_global_lock);

	I4B_AI_LOCK(sc);
	_IF_DRAIN(&sc->sc_rdqueue);
	I4B_AI_UNLOCK(sc);

	mtx_destroy(&sc->sc_mtx);

	free(sc, M_DEVBUF);
}

/*---------------------------------------------------------------------------*
 *	interface attach routine
 *---------------------------------------------------------------------------*/
static void
i4b_ai_attach(void *dummy)
{
	/*
	 * Make a character device so that we are visible:
	 */
	i4b_dev = 
	  make_dev(&i4b_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, I4BNAME);

	if(i4b_dev == NULL)
		printf("i4b: Failed to create character device.\n");

	printf("i4b: ISDN call control device attached\n");
}
SYSINIT(i4b_ai_attach, SI_SUB_PSEUDO, SI_ORDER_ANY, i4b_ai_attach, NULL);

/*---------------------------------------------------------------------------*
 *	i4b_ai_putqueue - put message into application interface queue(s)
 *---------------------------------------------------------------------------*/
void
i4b_ai_putqueue(struct i4b_ai_softc *sc, uint8_t sc_complement,
		struct mbuf *m1, uint16_t *p_copy_count)
{
	struct mbuf *m2;
	struct i4b_ai_softc *sc_exclude;

	if((sc == NULL) || (sc_complement))
	{
		/* Broadcast a message */

		sc_exclude = sc;

		mtx_lock(&i4b_global_lock);
		sc = TAILQ_FIRST(&i4b_head);
		if (sc != NULL)
			sc->sc_refs++;
		mtx_unlock(&i4b_global_lock);

		while(sc)
		{
			if(sc != sc_exclude)
			{
				/* 
				 * m_copypacket() is used because the
				 * copies does not need to be
				 * writeable. In other words the data
				 * pointed to by m_data can be shared.
				 */
				m2 = m_copypacket(m1, M_NOWAIT);

				if (m2 == NULL) {
					NDBGL4(L4_ERR, "out of mbufs!");
				} else {
					i4b_ai_putqueue(sc, 0, m2, p_copy_count);
				}
			}

			mtx_lock(&i4b_global_lock);
			sc->sc_refs--;
			sc = TAILQ_NEXT(sc, entry);
			if (sc != NULL)
				sc->sc_refs++;
			mtx_unlock(&i4b_global_lock);
		}
		m_freem(m1);
	}
	else
	{
		I4B_AI_LOCK(sc);

		if (sc->sc_flags & ST_CLOSING)
		{
			m_freem(m1);
			goto done;
		}

		if(_IF_QFULL(&sc->sc_rdqueue))
		{
			_IF_DEQUEUE(&sc->sc_rdqueue, m2);
			m_freem(m2);

			NDBGL4(L4_ERR, "ERROR: queue full, removing entry!");
		}

		if (p_copy_count)
			(*p_copy_count) ++;

		_IF_ENQUEUE(&sc->sc_rdqueue, m1);

		if(sc->sc_flags & ST_SLEEP_WAKEUP)
		{
			sc->sc_flags &= ~ST_SLEEP_WAKEUP;
			wakeup(&sc->sc_rdqueue);
		}

		if(sc->sc_flags & ST_SELECT)
		{
			sc->sc_flags &= ~ST_SELECT;
			selwakeup(&sc->sc_selinfo);
		}

	done:
		I4B_AI_UNLOCK(sc);
	}
}

/*---------------------------------------------------------------------------*
 *	i4b_open - device driver open routine
 *---------------------------------------------------------------------------*/
static int
i4b_open(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct i4b_ai_softc *sc;

	sc = i4b_ai_new_softc();
	if (sc == NULL)
		return (ENOMEM);

	devfs_set_cdevpriv(sc, i4b_ai_free_softc);

	/* open D-channels */
	i4b_update_all_d_channels(1);

	return (0);
}

/*---------------------------------------------------------------------------*
 *	i4b_close - device driver close routine
 *---------------------------------------------------------------------------*/
static int
i4b_close(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct i4b_ai_softc *sc;
	int error;

        error = devfs_get_cdevpriv((void **)&sc);
	if (error != 0)
                return (error);
	if (sc == NULL)
		return (ENXIO);

	I4B_AI_LOCK(sc);

	sc->sc_flags |= ST_CLOSING;

	while (sc->sc_flags & (ST_SLEEP_WAKEUP|
			       ST_SLEEP_ENTERED|
			       ST_IOCTL)) {

		if (sc->sc_flags & ST_SLEEP_WAKEUP) {
			sc->sc_flags &= ~ST_SLEEP_WAKEUP;
			wakeup(&sc->sc_rdqueue);
		}

		msleep(sc, &sc->sc_mtx, PZERO,
		       "I4B AI closing", 0);
	}

	I4B_AI_UNLOCK(sc);

	/*
	 * Disconnect any active calls on this application interface
	 */
	i4b_disconnect_by_appl_interface(I4B_AI_I4B, sc);

	/* close D-channels */
	i4b_update_all_d_channels(0);

	/* release all reserved drivers */
	i4b_release_drivers_by_appl_interface(I4B_AI_I4B, sc);

	I4B_AI_LOCK(sc);

	/* free memory last */
	_IF_DRAIN(&sc->sc_rdqueue);

	I4B_AI_UNLOCK(sc);

	return (0);
}

/*---------------------------------------------------------------------------*
 *	i4b_read - device driver read routine
 *---------------------------------------------------------------------------*/
static int
i4b_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct i4b_ai_softc *sc;
	struct mbuf *m;
	int error;

        error = devfs_get_cdevpriv((void **)&sc);
	if (error != 0)
                return (error);
	if (sc == NULL)
		return (ENXIO);

	I4B_AI_LOCK(sc);

	if(sc->sc_flags & (ST_SLEEP_ENTERED|ST_CLOSING))
	{
		/* only one thread at a time */
		I4B_AI_UNLOCK(sc);
		return EBUSY;
	}

	while(_IF_QEMPTY(&sc->sc_rdqueue))
	{
	    if(sc->sc_flags & ST_BLOCK)
	    {
		sc->sc_flags |= (ST_SLEEP_WAKEUP|ST_SLEEP_ENTERED);

		error = msleep(&sc->sc_rdqueue, &sc->sc_mtx,
			       (PZERO + 1) | PCATCH, "bird", 0);

		sc->sc_flags &= ~(ST_SLEEP_WAKEUP|ST_SLEEP_ENTERED);

		if(sc->sc_flags & ST_CLOSING)
		{
			wakeup(sc);
			error = EIO;
		}
	    }
	    else
	    {
	        error = EWOULDBLOCK;
	    }

	    if(error)
	    {
	        I4B_AI_UNLOCK(sc);
		return error;
	    }
	}
	_IF_DEQUEUE(&sc->sc_rdqueue, m);
	I4B_AI_UNLOCK(sc);

	if(m && m->m_len)
		error = uiomove(m->m_data, m->m_len, uio);
	else
		error = EIO;

	if(m)
		m_freem(m);

	return (error);
}

/*---------------------------------------------------------------------------*
 *	i4b_controller_download - download firmware from userland to an
 *	                          active controller
 *---------------------------------------------------------------------------*/
int
i4b_controller_download(struct i4b_controller *cntl, 
			struct isdn_download_request *req)
{
	struct isdn_dr_prot *prot;
	void *protocols_old;
	void *microcode;
	uint32_t size;
	int error = 0;
	int i;

	/* backup "protocols" pointer, 
	 * hence it will be overwritten:
	 */
	protocols_old = req->protocols;
	size = req->numprotos * sizeof(struct isdn_dr_prot);

	req->protocols = size ? malloc(size, M_DEVBUF, M_WAITOK) : NULL;

	if(req->protocols == NULL)
	{
		error = ENOMEM;
		goto done;
	}

	copyin(protocols_old, req->protocols, size);

	prot = req->protocols;
	i = req->numprotos;

	while(i--)
	{
		microcode = prot->bytecount ? 
		  malloc(prot->bytecount, M_DEVBUF, M_WAITOK) : NULL;

		if(microcode == NULL)
		{
			error = ENOMEM;
		}
		else
		{
			copyin(prot->microcode, microcode, 
			       prot->bytecount);
		}

		/* update microcode pointer 
		 * regardless of result
		 */
		prot->microcode = microcode;
		prot++;
	}

	if(!error)
	{
		error = L1_COMMAND_REQ(cntl, CMR_DOWNLOAD, req);
	}

 done:
	/* free all allocated memory */

	if(req->protocols)
	{
		prot = req->protocols;
		i = req->numprotos;

		while(i--)
		{
			if(prot->microcode)
			{
				free(prot->microcode, M_DEVBUF);
			}
			prot++;
		}

		free(req->protocols, M_DEVBUF);
	}

	/* restore "protocols" pointer */
	req->protocols = protocols_old;

	return (error);
}

/*---------------------------------------------------------------------------*
 *	i4b_active_diagnostic - get diagnostics from an active controller
 *---------------------------------------------------------------------------*/
static int
i4b_active_diagnostic(struct i4b_controller *cntl, 
		      struct isdn_diagnostic_request *req)
{
	void *in_param_ptr_old = req->in_param_ptr;
	void *out_param_ptr_old = req->out_param_ptr;
	int error = 0;
  
	req->in_param_ptr = NULL;
	req->out_param_ptr = NULL;

	if(req->in_param_len)
	{
		/* XXX arbitrary limit */
		if(req->in_param_len > I4B_ACTIVE_DIAGNOSTIC_MAXPARAMLEN)
		{
			error = EINVAL;
			goto done;
		}	

		req->in_param_ptr = malloc(req->in_param_len, 
					   M_DEVBUF, M_WAITOK);

		if(req->in_param_ptr == NULL)
		{
			error = ENOMEM;
			goto done;
		}

		error = copyin(in_param_ptr_old, 
			       req->in_param_ptr, 
			       req->in_param_len);
		if(error)
		{
			goto done;
		}
	}

	if(req->out_param_len)
	{
		req->out_param_ptr = malloc(req->out_param_len, 
					    M_DEVBUF, M_WAITOK);

		if(req->out_param_ptr == NULL)
		{
			error = ENOMEM;
			goto done;
		}
	}

	error = L1_COMMAND_REQ(cntl, CMR_DIAGNOSTICS, req);
			
	if(!error && req->out_param_len)
	{
		error = copyout(req->out_param_ptr,
				out_param_ptr_old,
				req->out_param_len);
	}

 done:
	if(req->in_param_ptr)
	{
		free(req->in_param_ptr, M_DEVBUF);
	}
				
	if(req->out_param_ptr)
	{
		free(req->out_param_ptr, M_DEVBUF);
	}

	/* restore pointers */
	req->in_param_ptr = in_param_ptr_old;
	req->out_param_ptr = out_param_ptr_old;

	return (error);
}

/*---------------------------------------------------------------------------*
 *	i4b_version_request - get ISDN4BSD version
 *---------------------------------------------------------------------------*/
void
i4b_version_request(msg_vr_req_t *mvr)
{
	mvr->max_controllers = I4B_MAX_CONTROLLERS;
	mvr->max_channels = I4B_MAX_CHANNELS;
	mvr->version = I4B_VERSION;
	mvr->release = I4B_REL;
	mvr->step = I4B_STEP;
}

/*---------------------------------------------------------------------------*
 *	i4b_ioctl - device driver ioctl routine
 *---------------------------------------------------------------------------*/
static int
i4b_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	struct i4b_ai_softc *sc;
	struct i4b_controller *cntl;
	call_desc_t *cd;
	int error;

        error = devfs_get_cdevpriv((void **)&sc);
	if (error != 0)
                return (error);
	if (sc == NULL)
		return (ENXIO);

	if (cmd == FIONBIO) {
		I4B_AI_LOCK(sc);
		if(*(int *)data)
			sc->sc_flags &= ~ST_BLOCK;
		else
			sc->sc_flags |= ST_BLOCK;
		I4B_AI_UNLOCK(sc);

		return (0);
	}

	if(IOCPARM_LEN(cmd) >= sizeof(cdid_t))
	{
		cntl = CNTL_FIND(((cdid_t *)data)[0]); 
	}
	else
	{
		cntl = NULL;
	}

	if(cntl == NULL)
	{
		error = ENODEV;
		goto done;
	}

	I4B_AI_LOCK(sc);

	if(sc->sc_flags & (ST_IOCTL|ST_CLOSING))
	{
		/* only one thread at a time */
		I4B_AI_UNLOCK(sc);
		return EBUSY;
	}

	sc->sc_flags |= ST_IOCTL;

	I4B_AI_UNLOCK(sc);

	cd = NULL;

	CNTL_LOCK(cntl);

	if(cntl->N_fifo_translator)
	{
		/* connected */
		cd = cd_by_cdid(cntl, ((cdid_t *)data)[0]);

		if((!cd) && (cmd == I4B_CDID_REQ))
		{
		  /* allocate a new call-descriptor
		   * for a outgoing call
		   *
		   * cd's are allocated from a controller,
		   * because of locking
		   */
		  cd = N_ALLOCATE_CD(cntl,NULL,0,I4B_AI_I4B,sc);
		}
	}

	if(cd)
	{
	  /* check the call descriptors 
	   * application interface before issuing
	   * command
	   */
	  if((cd->ai_type == I4B_AI_BROADCAST) ||
	     ((cd->ai_type == I4B_AI_I4B) && (cd->ai_ptr == ((void *)sc))))
	  {
	    switch(cmd)
	    {
		/* cdid request, reserve cd and return cdid */
		case I4B_CDID_REQ:
		{
			msg_cdid_req_t *mir = (void *)data;

			mir->cdid = cd->cdid;
			break;
		}
		
		/* connect request, dial out to remote */
		case I4B_CONNECT_REQ:
		{
			msg_connect_req_t *mcr = (void *)data;

			cd->channel_bprot = mcr->bprot;
			cd->channel_bsubprot = BSUBPROT_G711_ALAW;
			cd->driver_type = mcr->driver;
			cd->driver_unit = mcr->driver_unit;
			cd->shorthold_data = mcr->shorthold_data;

			cd->last_aocd_time = 0;
			if(mcr->unitlen_method == ULEN_METHOD_DYNAMIC)
			  cd->aocd_flag = 1;
			else
			  cd->aocd_flag = 0;

			cd->cunits = 0;

			strlcpy(cd->dst_telno, mcr->dst_telno, sizeof(cd->dst_telno));
			strlcpy(cd->src[0].telno, mcr->src_telno, sizeof(cd->src[0].telno));

			strlcpy(cd->dst_subaddr, mcr->dst_subaddr, sizeof(cd->dst_subaddr));
			strlcpy(cd->src[0].subaddr, mcr->src_subaddr, sizeof(cd->src[0].subaddr));

			strlcpy(cd->keypad, mcr->keypad, sizeof(cd->keypad));

			cd->display[0] = '\0';

			SET_CAUSE_TYPE(cd->cause_in, CAUSET_I4B);
			SET_CAUSE_VAL(cd->cause_in, CAUSE_I4B_NORMAL);
			  
			/*
			 * if(cd->user_user[0] != 0)
			 * {
			 *   CHAN_NOT_ANY;
			 *   use unused/invalid/sms prot??;
			 * }
			 */

			if(cd->channel_allocated == 0)
			{
			    cd->channel_id = mcr->channel;

			    if(cntl->N_nt_mode ||
			       /* channel specified */			     
			       ((cd->channel_id >= 0) &&
				(cd->channel_id < cntl->L1_channel_end)) ||
			       /* channel-less */
			       (cd->channel_id == CHAN_NOT_ANY)
			       /* || no channel available */)
			    {
			      cd_allocate_channel(cd);

			      if(cd->channel_allocated == 0)
			      {
				SET_CAUSE_VAL(cd->cause_in, CAUSE_I4B_NOCHAN);
				N_FREE_CD(cd);
				break;
			      }
			    }
			}

			cd->isdntxdelay = mcr->txdelay;
			cd->sending_complete = !(mcr->sending_not_complete);
			N_CONNECT_REQUEST(cd);
			break;
		}

		case I4B_INFORMATION_REQ:
		{
			msg_information_req_t *mir = (void *)data;

			i4b_l3_information_req(cd, &(mir->dst_telno[0]), 
					       strlen(&(mir->dst_telno[0])));
			break;
		}

		/* replay connect indication */

		case I4B_CONNECT_REPLAY_REQ:
		{
			/* msg_connect_replay_req_t *mcrreq = (void *)data; */

			if(cd->dir_incoming)
			{
			    if(cd->ai_type == I4B_AI_BROADCAST)
			    {
			        i4b_ai_connect_ind(cd, sc, NULL);
			    }
			}
			else
			{
			    error = EINVAL;
			}
			break;
		}

		/* connect response, accept/reject/ignore incoming call */
		
		case I4B_CONNECT_RESP:
		{
			msg_connect_resp_t *mcrsp = (void *)data;

			if(mcrsp->response == SETUP_RESP_DNTCRE)
			{
				/* ignore the call */

				N_CONNECT_RESPONSE(cd,mcrsp->response,mcrsp->cause);
				break;
			}

			if(cd->ai_type == I4B_AI_BROADCAST)
			{
			    /* set application interface before 
			     * disconnect indication
			     */
			    cd_set_appl_interface(cd,I4B_AI_I4B,sc);

			    /* send disconnect indication
			     * to all other application interfaces
			     */
			    i4b_l4_disconnect_ind(cd,1);

			    cd->driver_type = mcrsp->driver;
			    cd->driver_unit = mcrsp->driver_unit;
			}

			cd->shorthold_data.shorthold_algorithm = SHA_FIXU;
			cd->shorthold_data.unitlen_time = 0;
			cd->shorthold_data.earlyhup_time = 0;
			cd->shorthold_data.idle_time = mcrsp->max_idle_time;

			cd->isdntxdelay = mcrsp->txdelay;

			N_CONNECT_RESPONSE(cd,mcrsp->response,mcrsp->cause);
			break;
		}

		/* link B-channel request */

		case I4B_LINK_B_CHANNEL_DRIVER_REQ:
		{
			msg_link_b_channel_driver_req_t *mlr = (void *)data;

			if(cd->ai_type == I4B_AI_BROADCAST)
			{
			    /* set application interface */
			    cd_set_appl_interface(cd,I4B_AI_I4B,sc);

			    /* send disconnect indication
			     * to all other application interfaces
			     */
			    i4b_l4_disconnect_ind(cd,1);

			    cd->driver_type = mlr->driver;
			    cd->driver_unit = mlr->driver_unit;
			}

			if(i4b_link_bchandrvr(cd,mlr->activate))
			{
			  if(mlr->activate)
			  {
				/* activation failed */
				error = ENXIO;

				NDBGL4(L4_MSG,"could not setup B-channel");
			  }
			}
			else
			{
			  if(mlr->activate == 0)
			  {
				/* deactivation failed */
				error = ENXIO;
			  }
			}
			break;
		}

		/* disconnect request, actively terminate connection */

		case I4B_DISCONNECT_REQ:
		{
			msg_disconnect_req_t *mdr = (void *)data;

			/* preset causes with our cause */
			cd->cause_in = cd->cause_out = mdr->cause;

			N_DISCONNECT_REQUEST(cd,mdr->cause);

			cd = NULL; /* call descriptor is freed ! */

			break;
		}

		/* send ALERT request */

		case I4B_ALERT_REQ:
		{
			msg_alert_req_t *mar = (void *)data;

			N_ALERT_REQUEST(cd, mar->send_call_proceeding ? 1 : 0);
			break;
		}

		/* update timeout value */

		case I4B_TIMEOUT_UPD:
		{
			msg_timeout_upd_t *mtu = (void *)data;

			/* copy in new shorthold data */

			cd->shorthold_data = 
			  mtu->shorthold_data;

			/* check if timeout is started */
			if(cd->idle_state != IST_NOT_STARTED)
			{
			  /* re-compute and check shorthold data */
			  i4b_l4_setup_timeout(cd);
			}
			/* else wait for connect */
			break;
		}

		default:
			error = ENOTTY;
			break;
	    }
	  }
	  else
	  {
		NDBGL4(L4_MSG, "cmd=0x%08lx for cdid 0x%08x ignored!",
		       cmd, cd->cdid);
	  }
	}
	else
	{
	  switch(cmd)
	  {
		/* set D-channel protocol for a controller */

		case I4B_PROT_IND:
			L1_COMMAND_REQ(cntl, CMR_SET_LAYER2_PROTOCOL, data);
			break;

		/* controller info request */

		case I4B_CTRL_INFO_REQ:
			L1_COMMAND_REQ(cntl, CMR_INFO_REQUEST, data);
			break;

		/* response to user */

		case I4B_RESPONSE_TO_USER:

			CNTL_UNLOCK(cntl);
			cntl = NULL;

		{
			msg_response_to_user_t *mrtu = (void *)data;

			if(mrtu->driver_type < N_I4B_DRIVERS)
			{
			  response_to_user_t *func;

			  func = i4b_drivers_response_to_user[mrtu->driver_type];

			  if(func != NULL)
			  {
				(func)(mrtu);
			  }
			}
			break;
		}

		/* version/release number request */
		
		case I4B_VR_REQ:
			i4b_version_request((void *)data);
			break;

		/* download request */

		case I4B_CTRL_DOWNLOAD:

			CNTL_UNLOCK(cntl);

			error = i4b_controller_download(cntl, (void *)data);

			/* clear "cntl" hence controller
			 * lock has been exited
			 */
			cntl = NULL;
			break;

		/* diagnostic request */

		case I4B_ACTIVE_DIAGNOSTIC:

			CNTL_UNLOCK(cntl);

			error = i4b_active_diagnostic(cntl, (void *)data);

			/* clear "cntl" hence controller
			 * lock has been exited
			 */
			cntl = NULL;
			break;

		default:
			NDBGL4(L4_MSG, "ioctl, cdid not found!");
			error = ENOTTY;
			break;
	  }
	}
	if(cntl)
	{
		CNTL_UNLOCK(cntl);
	}

	I4B_AI_LOCK(sc);

	sc->sc_flags &= ~ST_IOCTL;

	if(sc->sc_flags & ST_CLOSING)
	{
		wakeup(sc);
		error = EIO;
	}
	I4B_AI_UNLOCK(sc);

done:
	return (error);
}

/*---------------------------------------------------------------------------*
 *	i4b_poll - device driver poll routine
 *---------------------------------------------------------------------------*/
static int
i4b_poll(struct cdev *dev, int events, struct thread *td)
{
	struct i4b_ai_softc *sc;
	int revents;

        revents = devfs_get_cdevpriv((void **)&sc);
	if (revents != 0)
                return (POLLNVAL);
	if (sc == NULL)
		return (POLLNVAL);

	I4B_AI_LOCK(sc);

	if(!_IF_QEMPTY(&sc->sc_rdqueue))
	{
		revents |= (events & (POLLIN|POLLRDNORM));
	}

	if(revents == 0)
	{
		selrecord(td, &sc->sc_selinfo);
		sc->sc_flags |= ST_SELECT;
	}

	I4B_AI_UNLOCK(sc);

	return (revents);
}
