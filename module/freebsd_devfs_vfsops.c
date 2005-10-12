/*-
 * Copyright (c) 2005
 *      Hans Petter Selasky. All rights reserved.
 * Copyright (c) 2000
 *	Poul-Henning Kamp.  All rights reserved.
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 
 *       Jan-Simon Pendry. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is a lite version of "FreeBSD/src/sys/fs/devfs/devfs_vfsops.c"
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <sys/freebsd_compat.h>

static int
devfs_root(struct mount *mp, /* int flags, */
	   struct vnode **vpp)
{
	struct thread *td = curthread; /* XXX */
	struct devfs_mount *dmp;
	struct vnode *vp;
	int error;

	dmp = VFSTODEVFS(mp);
	error = devfs_allocv(dmp->dm_rootdir, mp, &vp, td);
	if(error)
	{
	    goto done;
	}
	vp->v_flag |= VROOT;
	*vpp = vp;

 done:
	return error;
}

static int
devfs_mount(struct mount *mp, const char *path, void *data,
	    struct nameidata *ndp, struct thread *td)
{
	struct devfs_mount *fmp;
	struct vnode *rvp;
	int error = 0;

	if(mp->mnt_flag & (MNT_UPDATE | MNT_ROOTFS))
	{
	    return EOPNOTSUPP;
	}

	MALLOC(fmp, struct devfs_mount *, sizeof(struct devfs_mount),
	       M_DEVFS, M_WAITOK | M_ZERO);

	MALLOC(fmp->dm_dirent, struct devfs_dirent **,
	       sizeof(struct devfs_dirent *) * NDEVFSINO,
	       M_DEVFS, M_WAITOK | M_ZERO);

	lockinit(&fmp->dm_lock, PVFS, "devfs", 0, 0);

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_data = fmp;
#if 0
	mp->mnt_stat.f_namemax = MAXNAMLEN;
#endif

	fmp->dm_mount = mp;

	vfs_getnewfsid(mp);

	fmp->dm_inode = DEVFSINOMOUNT;

	fmp->dm_rootdir = devfs_vmkdir("(root)", 6, NULL);
	fmp->dm_rootdir->de_inode = 2;
	fmp->dm_basedir = fmp->dm_rootdir;

#if 0
	devfs_rules_newmount(fmp, td);
#endif

	error = devfs_root(mp, /* LK_EXCLUSIVE, */ &rvp /* , td */);
	if(error)
	{
	    goto done;
	}

	VOP_UNLOCK(rvp, 0);

	fmp->dm_root_vnode = rvp;

	error = set_statfs_info
	  (path, UIO_USERSPACE, "devfs", UIO_SYSSPACE, mp, td);

	if(error)
	{
	    goto done;
	}
#if 0
	vfs_mountedfrom(mp, "devfs");
#endif

 done:
	if(error)
	{
	    mp->mnt_data = NULL;
	    lockdestroy(&fmp->dm_lock);
	    FREE(fmp, M_DEVFS);
	}
	return error;
}

static int
devfs_unmount(struct mount *mp, int mntflags, 
	      struct thread *td)
{
	struct devfs_mount *fmp;
	int flags = 0;
	int error;

	if((mntflags & MNT_FORCE))
	{
	    flags |= FORCECLOSE;
	}

	fmp = VFSTODEVFS(mp);

#if 1
	/* drop extra reference to root vnode */

	if(fmp->dm_root_vnode)
	{
	    vrele(fmp->dm_root_vnode);
	}
#endif

	/* flush vnodes */

	error = vflush(mp, fmp->dm_root_vnode, flags);
	if(error)
	{
	    goto done;
	}

	devfs_purge(fmp->dm_rootdir);
	mp->mnt_data = NULL;
	lockdestroy(&fmp->dm_lock);
	free(fmp->dm_dirent, M_DEVFS);
	free(fmp, M_DEVFS);

 done:
	return error;
}

static int
devfs_statfs(struct mount *mp, struct statfs *sbp, struct thread *td)
{
	sbp->f_flags = 0;
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;		/* 1K to keep df happy */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;

	copy_statfs_info(sbp, mp);
	return 0;
}

static void
devfs_init()
{
	return;
}

static void
devfs_reinit()
{
	return;
}

static void
devfs_done()
{
	return;
}

static int
devfs_start(struct mount *a, int b, struct proc *c)
{
	return 0;
}

static int
devfs_quotactl(struct mount *a, int b, uid_t c, caddr_t d,
	       struct proc *e)
{
	return ENOTSUP;
}

static int
devfs_sync(struct mount *a, int b, struct ucred * c,
	   struct proc *d)
{
	return 0;
}

static int
devfs_vget(struct mount *a, ino_t b, struct vnode **c)
{
	return ENOTSUP;
} 

static int
devfs_fhtovp(struct mount *a, struct fid *b,
	     struct vnode **c)
{
	return ENOTSUP;
} 

static int
devfs_vptofh(struct vnode *a, struct fid *b)
{
	return ENOTSUP;
} 

static int
devfs_checkexp(struct mount *a, struct mbuf *b, int *c,
	       struct ucred **d)
{
	return ENOTSUP;
}

static const struct vnodeopv_desc * const 
devfs_vnodeopv_descs[] = 
{
	&devfs_vnodeop_opv_desc,
	NULL,
};

static struct vfsops devfs_vfsops = 
{
  .vfs_name           = MOUNT_DEVFS,
  .vfs_mount          = &devfs_mount,
  .vfs_unmount        = &devfs_unmount,
  .vfs_root           = &devfs_root,
  .vfs_statfs         = &devfs_statfs,
  .vfs_opv_descs      = &devfs_vnodeopv_descs[0],

  /*
   * not used
   */
  .vfs_init           = &devfs_init,
  .vfs_reinit         = &devfs_reinit,
  .vfs_done           = &devfs_done,
  .vfs_start          = &devfs_start,
  .vfs_quotactl       = &devfs_quotactl,
  .vfs_sync           = &devfs_sync,
  .vfs_vget           = &devfs_vget,
  .vfs_fhtovp         = &devfs_fhtovp,
  .vfs_vptofh         = &devfs_vptofh,
  .vfs_checkexp       = &devfs_checkexp,
};

static void
devfs_sysinit(void *arg)
{
	int error;

	error = vfs_attach(&devfs_vfsops);

	if(error)
	{
	    printf("%s: VFS attach failed, error=%d!\n",
		   __FUNCTION__, error);
	    goto done;
	}

 done:
	return;
}

SYSINIT(devfs_sysinit, SI_SUB_DRIVERS, SI_ORDER_FIRST, 
	devfs_sysinit, NULL);

static void
devfs_sysuninit(void *arg)
{
	vfs_detach(&devfs_vfsops);

	return;
}

SYSUNINIT(devfs_sysuninit, SI_SUB_DRIVERS, SI_ORDER_FIRST, 
	  devfs_sysuninit, NULL);
