/* $NetBSD: fdt_subr.c,v 1.39 2021/01/24 15:43:22 thorpej Exp $ */

/*-
 * Copyright (c) 2015 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <stdio.h>
__KERNEL_RCSID(0, "$NetBSD: fdt_subr.c,v 1.39 2021/01/24 15:43:22 thorpej Exp $");

#ifndef SEL4
#include "opt_fdt.h"
#endif

#include <sys/param.h>
#include <sys/bus.h>

#include <libfdt.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/fdt_private.h>

#ifndef FDT_DEFAULT_STDOUT_PATH
#define	FDT_DEFAULT_STDOUT_PATH		"serial0:115200n8"
#endif

static const void *fdt_data;

static struct fdt_conslist fdt_console_list =
    TAILQ_HEAD_INITIALIZER(fdt_console_list);

bool
fdtbus_init(const void *data)
{
	KASSERT(fdt_data == NULL);
	if (fdt_check_header(data) != 0) {
		return false;
	}
	fdt_data = data;

	return true;
}

const void *
fdtbus_get_data(void)
{
	return fdt_data;
}

int
fdtbus_offset2phandle(int offset)
{
	if (offset < 0)
		return 0;

	return offset + fdt_off_dt_struct(fdt_data);
}

int
fdtbus_phandle2offset(int phandle)
{
	const int dtoff = fdt_off_dt_struct(fdt_data);

	if (phandle == -1)
		phandle = dtoff;

	if (phandle < dtoff)
		return -1;

	return phandle - dtoff;
}

static bool fdtbus_decoderegprop = true;

void
fdtbus_set_decoderegprop(bool decode)
{
	fdtbus_decoderegprop = decode;
}

int
fdtbus_get_addr_cells(int phandle)
{
	uint32_t addr_cells;

	if (of_getprop_uint32(phandle, "#address-cells", &addr_cells))
		addr_cells = 2;

	return addr_cells;
}

int
fdtbus_get_size_cells(int phandle)
{
	uint32_t size_cells;

	if (of_getprop_uint32(phandle, "#size-cells", &size_cells))
		size_cells = 0;

	return size_cells;
}

int
fdtbus_get_phandle(int phandle, const char *prop)
{
	u_int phandle_ref;
	const u_int *buf;
	int len;

	buf = fdt_getprop(fdtbus_get_data(),
	    fdtbus_phandle2offset(phandle), prop, &len);
	if (buf == NULL || len < sizeof(phandle_ref))
		return -1;

	phandle_ref = be32dec(buf);

	return fdtbus_get_phandle_from_native(phandle_ref);
}

int
fdtbus_get_phandle_with_data(int phandle, const char *prop, const char *cells,
    int index, struct fdt_phandle_data *data)
{
	int len;
	const int offset = 1;

	const u_int *p = fdtbus_get_prop(phandle, prop, &len);
	if (p == NULL || len <= 0)
		return EINVAL;

	for (int i = 0; len > 0; i++) {
		u_int phandle_ref = be32toh(*p);
		const u_int iparent = fdtbus_get_phandle_from_native(phandle_ref);
		uint32_t cells_num;
		of_getprop_uint32(iparent, cells, &cells_num);

		if (index == i) {
			if (data != NULL) {
				data->phandle = iparent;
				data->count = cells_num;
				data->values = p + offset;
			}
			goto done;
		}

		const u_int reclen = offset + cells_num;
		len -= reclen * sizeof(u_int);
		p += reclen;
	}
	return EINVAL;

done:
	return 0;
}

int
fdtbus_get_phandle_from_native(int phandle)
{
	const int off = fdt_node_offset_by_phandle(fdt_data, phandle);
	if (off < 0) {
		return -1;
	}
	return fdtbus_offset2phandle(off);
}

bool
fdtbus_get_path(int phandle, char *buf, size_t buflen)
{
	const int off = fdtbus_phandle2offset(phandle);
	if (off < 0) {
		return false;
	}
	if (fdt_get_path(fdt_data, off, buf, (int)buflen) != 0) {
		return false;
	}
	return true;
}

uint64_t
fdtbus_get_cells(const uint8_t *buf, int cells)
{
	switch (cells) {
	case 0:		return 0;
	case 1: 	return be32dec(buf);
	case 2:		return ((uint64_t)be32dec(buf)<<32)|be32dec(buf+4);
	default:	printf("fdtbus_get_cells: bad cells val %d\n", cells);
	}
}

static uint64_t
fdtbus_decode_range(int phandle, uint64_t paddr)
{
	const int parent = OF_parent(phandle);
	if (parent == -1)
		return paddr;

	if (!fdtbus_decoderegprop)
		return paddr;

	const uint8_t *buf;
	int len;

	buf = fdt_getprop(fdtbus_get_data(),
	    fdtbus_phandle2offset(phandle), "ranges", &len);
	if (buf == NULL)
		return paddr;

	if (len == 0) {
		/* pass through to parent */
		return fdtbus_decode_range(parent, paddr);
	}

	const int addr_cells = fdtbus_get_addr_cells(phandle);
	const int size_cells = fdtbus_get_size_cells(phandle);
	const int paddr_cells = fdtbus_get_addr_cells(parent);
	if (addr_cells == -1 || size_cells == -1 || paddr_cells == -1)
		return paddr;

	while (len > 0) {
		uint64_t cba, pba, cl;
		cba = fdtbus_get_cells(buf, addr_cells);
		buf += addr_cells * 4;
		pba = fdtbus_get_cells(buf, paddr_cells);
		buf += paddr_cells * 4;
		cl = fdtbus_get_cells(buf, size_cells);
		buf += size_cells * 4;

#ifdef FDTBUS_DEBUG
		printf("%s: %s: cba=%#" PRIx64 ", pba=%#" PRIx64 ", cl=%#" PRIx64 "\n", __func__, fdt_get_name(fdtbus_get_data(), fdtbus_phandle2offset(phandle), NULL), cba, pba, cl);
#endif

		if (paddr >= cba && paddr < cba + cl)
			return fdtbus_decode_range(parent, pba) + (paddr - cba);

		len -= (addr_cells + paddr_cells + size_cells) * 4;
	}

	/* No mapping found */
	return paddr;
}

int
fdtbus_get_reg_byname(int phandle, const char *name, bus_addr_t *paddr,
    bus_size_t *psize)
{
	u_int index;
	int error;

	error = fdtbus_get_index(phandle, "reg-names", name, &index);
	if (error != 0)
		return ENOENT;

	return fdtbus_get_reg(phandle, index, paddr, psize);
}

int
fdtbus_get_reg(int phandle, u_int index, bus_addr_t *paddr, bus_size_t *psize)
{
	uint64_t addr, size;
	int error;

	error = fdtbus_get_reg64(phandle, index, &addr, &size);
	if (error)
		return error;

	if (sizeof(bus_addr_t) == 4 && (addr + size) > 0x100000000)
		return ERANGE;

	if (paddr)
		*paddr = (bus_addr_t)addr;
	if (psize)
		*psize = (bus_size_t)size;

	return 0;
}

int
fdtbus_get_reg64(int phandle, u_int index, uint64_t *paddr, uint64_t *psize)
{
	uint64_t addr, size;
	const uint8_t *buf;
	int len;

	const int addr_cells = fdtbus_get_addr_cells(OF_parent(phandle));
	const int size_cells = fdtbus_get_size_cells(OF_parent(phandle));
	if (addr_cells == -1 || size_cells == -1)
		return EINVAL;

	buf = fdt_getprop(fdtbus_get_data(),
	    fdtbus_phandle2offset(phandle), "reg", &len);
	if (buf == NULL || len <= 0)
		return EINVAL;

	const u_int reglen = size_cells * 4 + addr_cells * 4;
	if (reglen == 0)
		return EINVAL;

	if (index >= len / reglen)
		return ENXIO;

	buf += index * reglen;
	addr = fdtbus_get_cells(buf, addr_cells);
	buf += addr_cells * 4;
	size = fdtbus_get_cells(buf, size_cells);

	if (paddr) {
		*paddr = fdtbus_decode_range(OF_parent(phandle), addr);
#ifdef FDTBUS_DEBUG
		const char *name = fdt_get_name(fdtbus_get_data(),
		    fdtbus_phandle2offset(phandle), NULL);
		printf("fdt: [%s] decoded addr #%u: %" PRIx64
		    " -> %" PRIx64 "\n", name, index, addr, *paddr);
#endif
	}
	if (psize)
		*psize = size;

	return 0;
}

#if defined(FDT)
const struct fdt_console *
fdtbus_get_console(void)
{
	static const struct fdt_console_info *booted_console = NULL;

	if (booted_console == NULL) {
		__link_set_decl(fdt_consoles, struct fdt_console_info);
		struct fdt_console_info * const *info;
		const struct fdt_console_info *best_info = NULL;
		const int phandle = fdtbus_get_stdout_phandle();
		int best_match = 0;

		if (phandle == -1) {
			printf("WARNING: no console device\n");
			return NULL;
		}

		__link_set_foreach(info, fdt_consoles) {
			const int match = (*info)->ops->match(phandle);
			if (match > best_match) {
				best_match = match;
				best_info = *info;
			}
		}

		booted_console = best_info;
	}

	return booted_console == NULL ? NULL : booted_console->ops;
}
#endif

#ifndef SEL4
const char *
fdtbus_get_stdout_path(void)
{
	const char *prop;

	const int off = fdt_path_offset(fdtbus_get_data(), "/chosen");
	if (off >= 0) {
		prop = fdt_getprop(fdtbus_get_data(), off, "stdout-path", NULL);
		if (prop != NULL)
			return prop;
	}

	/* If the stdout-path property is not found, return the default */
	return FDT_DEFAULT_STDOUT_PATH;
}

int
fdtbus_get_stdout_phandle(void)
{
	const char *prop, *p;
	int off, len;

	prop = fdtbus_get_stdout_path();
	if (prop == NULL)
		return -1;

	p = strchr(prop, ':');
	len = p == NULL ? strlen(prop) : (p - prop);
	if (*prop != '/') {
		/* Alias */
		prop = fdt_get_alias_namelen(fdtbus_get_data(), prop, len);
		if (prop == NULL)
			return -1;
		len = strlen(prop);
	}
	off = fdt_path_offset_namelen(fdtbus_get_data(), prop, len);
	if (off < 0)
		return -1;

	return fdtbus_offset2phandle(off);
}

int
fdtbus_get_stdout_speed(void)
{
	const char *prop, *p;

	prop = fdtbus_get_stdout_path();
	if (prop == NULL)
		return -1;

	p = strchr(prop, ':');
	if (p == NULL)
		return -1;

	return (int)strtoul(p + 1, NULL, 10);
}

tcflag_t
fdtbus_get_stdout_flags(void)
{
	const char *prop, *p;
	tcflag_t flags = TTYDEF_CFLAG;
	char *ep;

	prop = fdtbus_get_stdout_path();
	if (prop == NULL)
		return flags;

	p = strchr(prop, ':');
	if (p == NULL)
		return flags;

	ep = NULL;
	(void)strtoul(p + 1, &ep, 10);
	if (ep == NULL)
		return flags;

	/* <baud>{<parity>{<bits>{<flow>}}} */
	while (*ep) {
		switch (*ep) {
		/* parity */
		case 'n':	flags &= ~(PARENB|PARODD); break;
		case 'e':	flags &= ~PARODD; flags |= PARENB; break;
		case 'o':	flags |= (PARENB|PARODD); break;
		/* bits */
		case '5':	flags &= ~CSIZE; flags |= CS5; break;
		case '6':	flags &= ~CSIZE; flags |= CS6; break;
		case '7':	flags &= ~CSIZE; flags |= CS7; break;
		case '8':	flags &= ~CSIZE; flags |= CS8; break;
		/* flow */
		case 'r':	flags |= CRTSCTS; break;
		}
		ep++;
	}

	return flags;
}
#endif

bool
fdtbus_status_okay(int phandle)
{
	const int off = fdtbus_phandle2offset(phandle);

	const char *prop = fdt_getprop(fdtbus_get_data(), off, "status", NULL);
	if (prop == NULL)
		return true;

	return strncmp(prop, "ok", 2) == 0;
}

const void *
fdtbus_get_prop(int phandle, const char *prop, int *plen)
{
	const int off = fdtbus_phandle2offset(phandle);

	return fdt_getprop(fdtbus_get_data(), off, prop, plen);
}

const char *
fdtbus_get_string(int phandle, const char *prop)
{
	const int off = fdtbus_phandle2offset(phandle);

	if (strcmp(prop, "name") == 0)
		return fdt_get_name(fdtbus_get_data(), off, NULL);
	else
		return fdt_getprop(fdtbus_get_data(), off, prop, NULL);
}

const char *
fdtbus_get_string_index(int phandle, const char *prop, u_int index)
{
	const char *names;
	int len;

	if ((len = OF_getproplen(phandle, prop)) < 0)
		return NULL;

	names = fdtbus_get_string(phandle, prop);

	return strlist_string(names, len, index);
}

int
fdtbus_get_index(int phandle, const char *prop, const char *name, u_int *idx)
{
	const char *p;
	int len, index;

	p = fdtbus_get_prop(phandle, prop, &len);
	if (p == NULL || len <= 0)
		return -1;

	index = strlist_index(p, len, name);
	if (index == -1)
		return -1;

	*idx = index;
	return 0;
}
