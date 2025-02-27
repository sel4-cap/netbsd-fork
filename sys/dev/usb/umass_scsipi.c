/*	$NetBSD: umass_scsipi.c,v 1.70 2021/12/31 14:24:16 riastradh Exp $	*/

/*
 * Copyright (c) 2001, 2003, 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology, Charles M. Hamnnum and Matthew R. Green.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: umass_scsipi.c,v 1.70 2021/12/31 14:24:16 riastradh Exp $");

#ifdef _KERNEL_OPT
#include "opt_usb.h"
#endif

#include "atapibus.h"
#include "scsibus.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disk.h>		/* XXX */
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <stdlib.h>
#include <sys/systm.h>

/* SCSI & ATAPI */
#include <sys/scsiio.h>
#include <dev/scsipi/scsi_spc.h>
#include <dev/scsipi/scsi_all.h>
#include <dev/scsipi/scsipi_all.h>
#include <dev/scsipi/scsiconf.h>

#include <dev/scsipi/atapiconf.h>

#include <dev/scsipi/scsipi_disk.h>
#include <dev/scsipi/scsi_disk.h>
#include <dev/scsipi/scsi_changer.h>

#include <dev/scsipi/sdvar.h>	/* XXX */

/* USB */
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbhist.h>

#include <dev/usb/umassvar.h>
#include <dev/usb/umass_scsipi.h>
#include <microkit.h>
#include <stdio.h>
#include <shared_ringbuffer.h>
#include <dev/usb/xhcivar.h>
#include <xhci_api.h>

extern blk_queue_handle_t *umass_buffer_ring;

struct umass_scsipi_softc {
	struct umassbus_softc	base;

	struct atapi_adapter	sc_atapi_adapter;
#define sc_adapter sc_atapi_adapter._generic
	struct scsipi_channel sc_channel;
	usbd_status		sc_sync_status;
	struct scsi_request_sense	sc_sense_cmd;
};


#define SHORT_INQUIRY_LENGTH    36 /* XXX */

#define UMASS_ATAPI_DRIVE	0

Static void umass_scsipi_request(struct scsipi_channel *,
				 scsipi_adapter_req_t, void *);
Static void umass_scsipi_minphys(struct buf *);
#ifndef SEL4
Static int umass_scsipi_ioctl(struct scsipi_channel *, u_long,
			      void *, int, proc_t *);
#endif
Static int umass_scsipi_getgeom(struct scsipi_periph *,
				struct disk_parms *, u_long);

Static void umass_null_cb(struct umass_softc *, void *,
			  int, int);
Static void umass_scsipi_cb(struct umass_softc *, void *,
			    int, int);
Static void umass_scsipi_sense_cb(struct umass_softc *, void *,
				  int, int);

Static struct umass_scsipi_softc *umass_scsipi_setup(struct umass_softc *);

void *get_umass_scsipi_cb() {
	return &umass_scsipi_cb;
}

void *get_umass_null_cb() {
	return &umass_null_cb;
}

#if NATAPIBUS > 0
Static void umass_atapi_probe_device(struct atapibus_softc *, int);

#ifndef SEL4
const struct scsipi_bustype umass_atapi_bustype = {
	.bustype_type = SCSIPI_BUSTYPE_ATAPI,
	.bustype_cmd = atapi_scsipi_cmd,
	.bustype_interpret_sense = atapi_interpret_sense,
	.bustype_printaddr = atapi_print_addr,
	.bustype_kill_pending = scsi_kill_pending,
	.bustype_async_event_xfer_mode = NULL,
};
#endif
#endif /* NATAPIBUS */


#if NSCSIBUS > 0
int
umass_scsi_attach(struct umass_softc *sc)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();
	struct umass_scsipi_softc *scbus;

	KASSERT(KERNEL_LOCKED_P());

	scbus = umass_scsipi_setup(sc);

	scbus->sc_channel.chan_bustype = &scsi_bustype;
	scbus->sc_channel.chan_ntargets = 2;
	scbus->sc_channel.chan_nluns = sc->maxlun + 1;
	scbus->sc_channel.chan_id = scbus->sc_channel.chan_ntargets - 1;
	DPRINTFM(UDMASS_USB, "sc %#jx: SCSI", (uintptr_t)sc, 0, 0, 0);

	scbus->base.sc_child =
	    config_found(sc->sc_dev, &scbus->sc_channel, scsiprint,
			 CFARGS(.iattr = "scsi"));

	return 0;
}

void
umass_scsi_detach(struct umass_softc *sc)
{
	struct umass_scsipi_softc *scbus = (struct umass_scsipi_softc *)sc->bus;

	kmem_free(scbus, sizeof(*scbus));
	sc->bus = NULL;
}
#endif /* NATAPIBUS */

#if NATAPIBUS > 0
int
umass_atapi_attach(struct umass_softc *sc)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();
	struct umass_scsipi_softc *scbus;

	KASSERT(KERNEL_LOCKED_P());

	scbus = umass_scsipi_setup(sc);
#ifndef SEL4
	scbus->sc_atapi_adapter.atapi_probe_device =  umass_atapi_probe_device;

	scbus->sc_channel.chan_bustype = &umass_atapi_bustype;
	scbus->sc_channel.chan_ntargets = 2;
	scbus->sc_channel.chan_nluns = 1;

	scbus->sc_channel.chan_defquirks |= sc->sc_busquirks;
	DPRINTFM(UDMASS_USB, "sc %#jxp: ATAPI", (uintptr_t)sc, 0, 0, 0);

	scbus->base.sc_child =
	    config_found(sc->sc_dev, &scbus->sc_channel, atapiprint,
			 CFARGS(.iattr = "atapi"));
#endif

	return 0;
}

void
umass_atapi_detach(struct umass_softc *sc)
{
	struct umass_scsipi_softc *scbus = (struct umass_scsipi_softc *)sc->bus;

	kmem_free(scbus, sizeof(*scbus));
	sc->bus = NULL;
}
#endif

Static struct umass_scsipi_softc *
umass_scsipi_setup(struct umass_softc *sc)
{
	struct umass_scsipi_softc *scbus;

	scbus = kmem_zalloc(sizeof(*scbus), KM_SLEEP);
	sc->bus = &scbus->base;

	/* Only use big commands for USB SCSI devices. */
	/* Do not ask for timeouts.  */
	sc->sc_busquirks |= PQUIRK_ONLYBIG|PQUIRK_NOREPSUPPOPC;

	/* Fill in the adapter. */
	memset(&scbus->sc_adapter, 0, sizeof(scbus->sc_adapter));
	scbus->sc_adapter.adapt_dev = sc->sc_dev;
	scbus->sc_adapter.adapt_nchannels = 1;
	scbus->sc_adapter.adapt_request = umass_scsipi_request;
	//scbus->sc_adapter.adapt_minphys = umass_scsipi_minphys;
	//scbus->sc_adapter.adapt_ioctl = umass_scsipi_ioctl;
	scbus->sc_adapter.adapt_getgeom = umass_scsipi_getgeom;
	scbus->sc_adapter.adapt_flags = SCSIPI_ADAPT_MPSAFE;

	/* Fill in the channel. */
	memset(&scbus->sc_channel, 0, sizeof(scbus->sc_channel));
	scbus->sc_channel.chan_adapter = &scbus->sc_adapter;
	scbus->sc_channel.chan_channel = 0;
	scbus->sc_channel.chan_flags = SCSIPI_CHAN_OPENINGS | SCSIPI_CHAN_NOSETTLE;
	scbus->sc_channel.chan_openings = 1;
	scbus->sc_channel.chan_max_periph = 1;
	scbus->sc_channel.chan_defquirks |= sc->sc_busquirks;

	return scbus;
}

Static void
umass_scsipi_request(struct scsipi_channel *chan,
		scsipi_adapter_req_t req, void *arg)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();
	struct scsipi_adapter *adapt = chan->chan_adapter;
	struct scsipi_periph *periph;
	struct scsipi_xfer *xs;
	struct umass_softc *sc = device_private(adapt->adapt_dev);
	struct umass_scsipi_softc *scbus = (struct umass_scsipi_softc *)sc->bus;
	struct scsipi_generic *cmd;
	int cmdlen;
	int dir;
#ifdef UMASS_DEBUG
	microtime(&sc->tv);
#endif
	switch(req) {
	case ADAPTER_REQ_RUN_XFER:
		xs = arg;
		periph = xs->xs_periph;
		DIF(UDMASS_UPPER, periph->periph_dbflags |= SCSIPI_DEBUG_FLAGS);

		DPRINTFM(UDMASS_CMD, "sc %#jxp: %jd:%jd xs=%#jxp",
		    (uintptr_t)sc, periph->periph_target, periph->periph_lun,
		    (uintptr_t)xs);
		DPRINTFM(UDMASS_CMD, "cmd=0x%02jx datalen=%jd (quirks=%#jx, "
		    "poll=%jd)", xs->cmd->opcode, xs->datalen,
		    periph->periph_quirks, !!(xs->xs_control & XS_CTL_POLL));
#if defined(UMASS_DEBUG) && defined(SCSIPI_DEBUG)
		if (umassdebug & UDMASS_SCSI)
			show_scsipi_xs(xs);
		else if (umassdebug & ~UDMASS_CMD)
			show_scsipi_cmd(xs);
#endif

		if (sc->sc_dying) {
			xs->error = XS_DRIVER_STUFFUP;
			goto done;
		}

#ifdef UMASS_DEBUG
		if (SCSIPI_BUSTYPE_TYPE(chan->chan_bustype->bustype_type) ==
		    SCSIPI_BUSTYPE_ATAPI ?
		    periph->periph_target != UMASS_ATAPI_DRIVE :
		    periph->periph_target == chan->chan_id) {
			DPRINTFM(UDMASS_SCSI, "sc %#jx: wrong SCSI ID %jd",
			    (uintptr_t)sc, periph->periph_target, 0, 0);
			xs->error = XS_DRIVER_STUFFUP;
			goto done;
		}
#endif

		cmd = xs->cmd;
		cmdlen = xs->cmdlen;

		dir = DIR_NONE;
		if (xs->datalen) {
			switch (xs->xs_control &
			    (XS_CTL_DATA_IN | XS_CTL_DATA_OUT)) {
			case XS_CTL_DATA_IN:
				dir = DIR_IN;
				break;
			case XS_CTL_DATA_OUT:
				dir = DIR_OUT;
				break;
			}
		}

		if (xs->datalen > UMASS_MAX_TRANSFER_SIZE) {
			printf("umass_cmd: large datalen, %d\n", xs->datalen);
			xs->error = XS_DRIVER_STUFFUP;
			goto done;
		}

		if (xs->xs_control & XS_CTL_POLL) {
			/* Use sync transfer. XXX Broken! */
			DPRINTFM(UDMASS_SCSI, "sync dir=%jd\n", dir, 0, 0, 0);
			scbus->sc_sync_status = USBD_INVAL;
			sc->sc_methods->wire_xfer(sc, periph->periph_lun, cmd,
						  cmdlen, xs->data,
						  xs->datalen, dir,
						  xs->timeout, USBD_SYNCHRONOUS,
						  intr_ptrs->umass_null_cb, xs);
			DPRINTFM(UDMASS_SCSI, "done err=%jd",
			    scbus->sc_sync_status, 0, 0, 0);
			switch (scbus->sc_sync_status) {
			case USBD_NORMAL_COMPLETION:
				xs->error = XS_NOERROR;
				break;
			case USBD_TIMEOUT:
				xs->error = XS_TIMEOUT;
				break;
			default:
				xs->error = XS_DRIVER_STUFFUP;
				break;
			}
			goto done;
		} else {
			DPRINTFM(UDMASS_SCSI, "async dir=%jd, cmdlen=%jd"
			    " datalen=%jd", dir, cmdlen, xs->datalen, 0);
			get_sc_methods(sc->sc_methods)->wire_xfer(sc, periph->periph_lun, cmd,
						  cmdlen, xs->data,
						  xs->datalen, dir,
						  xs->timeout, 0,
						  intr_ptrs->umass_scsipi_cb, xs);
			return;
		}

		/* Return if command finishes early. */
 done:
		scsipi_done(xs);
		return;
	default:
		/* Not supported, nothing to do. */
		;
	}
}

Static void
umass_scsipi_minphys(struct buf *bp)
{
#ifndef SEL4
#ifdef DIAGNOSTIC
	if (bp->b_bcount <= 0) {
		printf("umass_scsipi_minphys count(%d) <= 0\n",
		       bp->b_bcount);
		bp->b_bcount = UMASS_MAX_TRANSFER_SIZE;
	}
#endif
	if (bp->b_bcount > UMASS_MAX_TRANSFER_SIZE)
		bp->b_bcount = UMASS_MAX_TRANSFER_SIZE;
	minphys(bp);
#endif
}
#ifndef SEL4
int
umass_scsipi_ioctl(struct scsipi_channel *chan, u_long cmd,
    void *arg, int flag, proc_t *p)
{
	/*struct umass_softc *sc = link->adapter_softc;*/
	/*struct umass_scsipi_softc *scbus = sc->bus;*/

	switch (cmd) {
#if 0
	case SCBUSIORESET:
		ccb->ccb_h.status = CAM_REQ_INPROG;
		umass_reset(sc, umass_cam_cb, (void *) ccb);
		return 0;
#endif
	default:
		return ENOTTY;
	}
}
#endif

Static int
umass_scsipi_getgeom(struct scsipi_periph *periph, struct disk_parms *dp,
		     u_long sectors)
{
	struct umass_softc *sc =
	    device_private(periph->periph_channel->chan_adapter->adapt_dev);

	/* If it's not a floppy, we don't know what to do. */
	if (sc->sc_cmd != UMASS_CPROTO_UFI)
		return 0;

	switch (sectors) {
	case 1440:
		/* Most likely a single density 3.5" floppy. */
		dp->heads = 2;
		dp->sectors = 9;
		dp->cyls = 80;
		return 1;
	case 2880:
		/* Most likely a double density 3.5" floppy. */
		dp->heads = 2;
		dp->sectors = 18;
		dp->cyls = 80;
		return 1;
	default:
		return 0;
	}
}

Static void
umass_null_cb(struct umass_softc *sc, void *priv, int residue, int status)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();

	int dev_id = usbd_get_sel4_id(sc->sc_udev);
	int* buf = malloc(sizeof(dev_id));
	*buf = dev_id;

	// sending back dev_id, but not necessary in sddf
	blk_enqueue_resp(umass_buffer_ring, SUCCESS, buf, sizeof(buf), 1, 1);

	// Read / Write complete
	microkit_notify(UMASS_COMPLETE);
}

Static void
umass_scsipi_cb(struct umass_softc *sc, void *priv, int residue, int status)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();
	struct umass_scsipi_softc *scbus = (struct umass_scsipi_softc *)sc->bus;
	struct scsipi_xfer *xs = priv;
	struct scsipi_periph *periph = xs->xs_periph;
	int cmdlen, senselen;
#ifdef UMASS_DEBUG
	struct timeval tv;
	u_int delta;
	microtime(&tv);
	delta = (tv.tv_sec - sc->tv.tv_sec) * 1000000 + tv.tv_usec - sc->tv.tv_usec;
	DPRINTFM(UDMASS_CMD, "delta=%ju: xs=%#jx residue=%jd status=%jd",
	    delta, (uintptr_t)xs, residue, status);
#endif


	xs->resid = residue;

	switch (status) {
	case STATUS_CMD_OK:
		xs->error = XS_NOERROR;
		break;

	case STATUS_CMD_UNKNOWN:
		/* FALLTHROUGH */
	case STATUS_CMD_FAILED:
		/* fetch sense data */
		sc->sc_sense = 1;
		memset(&scbus->sc_sense_cmd, 0, sizeof(scbus->sc_sense_cmd));
		scbus->sc_sense_cmd.opcode = SCSI_REQUEST_SENSE;
		scbus->sc_sense_cmd.byte2 = periph->periph_lun <<
		    SCSI_CMD_LUN_SHIFT;

		if (sc->sc_cmd == UMASS_CPROTO_UFI ||
		    sc->sc_cmd == UMASS_CPROTO_ATAPI)
			cmdlen = UFI_COMMAND_LENGTH;	/* XXX */
		else
			cmdlen = sizeof(scbus->sc_sense_cmd);
		if (periph->periph_version < 0x04) /* SPC-2 */
			senselen = 18;
		else
			senselen = sizeof(xs->sense);
		scbus->sc_sense_cmd.length = senselen;
		sc->sc_methods->wire_xfer(sc, periph->periph_lun,
					  &scbus->sc_sense_cmd, cmdlen,
					  &xs->sense, senselen,
					  DIR_IN, xs->timeout, 0,
					  umass_scsipi_sense_cb, xs);
		return;

	case STATUS_WIRE_FAILED:
		xs->error = XS_RESET;
		break;

	case STATUS_TIMEOUT:
		xs->error = XS_TIMEOUT;
		break;

	default:
		panic("%s: Unknown status %d in umass_scsipi_cb",
			device_xname(sc->sc_dev), status);
	}

	DPRINTFM(UDMASS_CMD, "return xs->error=%jd, xs->xs_status=%#jx"
	    " xs->resid=%jd", xs->error, xs->xs_status, xs->resid, 0);

	scsipi_done(xs);
}

/*
 * Finalise a completed autosense operation
 */
Static void
umass_scsipi_sense_cb(struct umass_softc *sc, void *priv, int residue,
		      int status)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();
	struct scsipi_xfer *xs = priv;
	size_t extra;

	DPRINTFM(UDMASS_CMD, "sc %#jx: xs=%#jx residue=%jd status=%jd",
	    (uintptr_t)sc, (uintptr_t)xs, residue, status);

	sc->sc_sense = 0;
	switch (status) {
	case STATUS_CMD_OK:
	case STATUS_CMD_UNKNOWN:
		/* getting sense data succeeded */
#ifndef SEL4
		extra = sizeof(xs->sense.scsi_sense)
		      - sizeof(xs->sense.scsi_sense.extra_bytes);
		if (residue <= extra)
			xs->error = XS_SENSE;
		else
			xs->error = XS_SHORTSENSE;
		break;
#endif
	default:
		DPRINTFM(UDMASS_SCSI, "sc %#jx: Autosense failed, status %jd",
		    (uintptr_t)sc, status, 0, 0);
#ifndef SEL4
		xs->error = XS_DRIVER_STUFFUP;
#endif
		break;
	}

	DPRINTFM(UDMASS_CMD, "return xs->error=%jd, xs->xs_status=%#jx"
	    " xs->resid=%jd", xs->error, xs->xs_status, xs->resid, 0);

	scsipi_done(xs);
}

#if NATAPIBUS > 0
Static void
umass_atapi_probe_device(struct atapibus_softc *atapi, int target)
{
	UMASSHIST_FUNC(); UMASSHIST_CALLED();
	struct scsipi_channel *chan = atapi->sc_channel;
	struct scsipi_periph *periph;
	struct scsipibus_attach_args sa;
	char vendor[33], product[65], revision[17];
	struct scsipi_inquiry_data inqbuf;

	DPRINTFM(UDMASS_SCSI, "atapi=%#jx target=%jd", (uintptr_t)atapi,
	    target, 0, 0);

	if (target != UMASS_ATAPI_DRIVE)	/* only probe drive 0 */
		return;

	/* skip if already attached */
#ifndef SEL4
	if (scsipi_lookup_periph(chan, target, 0) != NULL) {
		return;
	}

	periph = scsipi_alloc_periph(M_WAITOK);
#endif
	DIF(UDMASS_UPPER, periph->periph_dbflags |= 1); /* XXX 1 */
#ifndef SEL4
	periph->periph_channel = chan;
	periph->periph_switch = &atapi_probe_periphsw;
	periph->periph_target = target;
	periph->periph_quirks = chan->chan_defquirks;
#endif

	DPRINTFM(UDMASS_SCSI, "doing inquiry", 0, 0, 0, 0);
	/* Now go ask the device all about itself. */
	memset(&inqbuf, 0, sizeof(inqbuf));
#ifndef SEL4
	if (scsipi_inquire(periph, &inqbuf, XS_CTL_DISCOVERY) != 0) {
		DPRINTFM(UDMASS_SCSI, "scsipi_inquire failed", 0, 0, 0, 0);
		free(periph, M_DEVBUF);
		return;
	}

	strnvisx(vendor, sizeof(vendor), inqbuf.vendor, 8,
	    VIS_TRIM|VIS_SAFE|VIS_OCTAL);
	strnvisx(product, sizeof(product), inqbuf.product, 16,
	    VIS_TRIM|VIS_SAFE|VIS_OCTAL);
	strnvisx(revision, sizeof(revision), inqbuf.revision, 4,
	    VIS_TRIM|VIS_SAFE|VIS_OCTAL);

	sa.sa_periph = periph;
	sa.sa_inqbuf.type = inqbuf.device;
	sa.sa_inqbuf.removable = inqbuf.dev_qual2 & SID_REMOVABLE ?
	    T_REMOV : T_FIXED;
	if (sa.sa_inqbuf.removable)
		periph->periph_flags |= PERIPH_REMOVABLE;
	sa.sa_inqbuf.vendor = vendor;
	sa.sa_inqbuf.product = product;
	sa.sa_inqbuf.revision = revision;
	sa.sa_inqptr = NULL;

	atapi_probe_device(atapi, target, periph, &sa);
#endif
	/* atapi_probe_device() frees the periph when there is no device.*/
}
#endif
