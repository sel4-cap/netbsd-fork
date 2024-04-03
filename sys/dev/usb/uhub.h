#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/device.h>
#include <sys/select.h>
#include <sys/proc.h>

struct uhub_softc {
	device_t		 sc_dev;	/* base device */
	struct usbd_device	*sc_hub;	/* USB device */
	int			 sc_proto;	/* device protocol */
	struct usbd_pipe	*sc_ipipe;	/* interrupt pipe */

	kmutex_t		 sc_lock;
	kcondvar_t		 sc_cv;

	uint8_t			*sc_statusbuf;
	uint8_t			*sc_statuspend;
	uint8_t			*sc_status;
	size_t			 sc_statuslen;
	bool			 sc_explorepending;
	bool			 sc_first_explore;
	bool			 sc_running;
	bool			 sc_rescan;

	// struct lwp		*sc_exploring;
};