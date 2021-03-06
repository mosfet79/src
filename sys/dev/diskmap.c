/*	$OpenBSD: diskmap.c,v 1.21 2018/07/03 05:50:46 mpi Exp $	*/

/*
 * Copyright (c) 2009, 2010 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Disk mapper.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/dkio.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/pledge.h>
#include <sys/namei.h>

int
diskmapopen(dev_t dev, int flag, int fmt, struct proc *p)
{
	return 0;
}

int
diskmapclose(dev_t dev, int flag, int fmt, struct proc *p)
{
	return 0;
}

int
diskmapioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct dk_diskmap *dm;
	struct nameidata ndp;
	struct filedesc *fdp = p->p_fd;
	struct file *fp0 = NULL, *fp = NULL;
	struct vnode *vp = NULL;
	char *devname, flags;
	int fd, error = EINVAL;

	if (cmd != DIOCMAP)
		return EINVAL;

	/*
	 * Map a request for a disk to the correct device. We should be
	 * supplied with either a diskname or a disklabel UID.
	 */

	dm = (struct dk_diskmap *)addr;
	fd = dm->fd;
	devname = malloc(PATH_MAX, M_DEVBUF, M_WAITOK);
	if (copyinstr(dm->device, devname, PATH_MAX, NULL))
		goto invalid;
	if (disk_map(devname, devname, PATH_MAX, dm->flags) == 0)
		if (copyoutstr(devname, dm->device, PATH_MAX, NULL))
			goto invalid;

	/* Attempt to open actual device. */
	if ((error = getvnode(p, fd, &fp0)) != 0)
		goto invalid;

	NDINIT(&ndp, 0, 0, UIO_SYSSPACE, devname, p);
	ndp.ni_pledge = PLEDGE_RPATH;
	if ((error = vn_open(&ndp, fp0->f_flag, 0)) != 0)
		goto bad;

	vp = ndp.ni_vp;
	VOP_UNLOCK(vp);

	fdplock(fdp);
	/*
	 * Stop here if the 'struct file *' has been replaced,
	 * for example by another thread calling dup2(2), while
	 * this thread was sleeping in vn_open().
	 *
	 * Note that this would not happen for correct usages of
	 * "/dev/diskmap".
	 */
	if (fdp->fd_ofiles[fd] != fp0) {
		error = EAGAIN;
		goto bad;
	}

	fp = fnew(p);
	if (fp == NULL) {
		error = ENFILE;
		goto bad;
	}

	/* Zap old file. */
	mtx_enter(&fdp->fd_fplock);
	KASSERT(fdp->fd_ofiles[fd] == fp0);
	flags = fdp->fd_ofileflags[fd];
	fdp->fd_ofiles[fd] = NULL;
	fdp->fd_ofileflags[fd] = 0;
	mtx_leave(&fdp->fd_fplock);

	/* Insert new file. */
	fp->f_flag = fp0->f_flag;
	fp->f_type = DTYPE_VNODE;
	fp->f_ops = &vnops;
	fp->f_data = (caddr_t)vp;
	fdinsert(fdp, fd, flags, fp);
	fdpunlock(fdp);
	FRELE(fp, p);

	closef(fp0, p);
	free(devname, M_DEVBUF, PATH_MAX);

	return 0;

bad:
	fdpunlock(fdp);

	if (vp)
		vrele(vp);
	if (fp0)
		FRELE(fp0, p);

invalid:
	free(devname, M_DEVBUF, PATH_MAX);

	return (error);
}

int
diskmapread(dev_t dev, struct uio *uio, int flag)
{
	return ENXIO;
}

int
diskmapwrite(dev_t dev, struct uio *uio, int flag)
{
	return ENXIO;
}
