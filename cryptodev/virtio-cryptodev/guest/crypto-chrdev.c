/*
 * crypto-chrdev.c
 *
 * Implementation of character devices
 * for virtio-cryptodev device 
 *
 * Vangelis Koukis <vkoukis@cslab.ece.ntua.gr>
 * Dimitris Siakavaras <jimsiak@cslab.ece.ntua.gr>
 * Stefanos Gerangelos <sgerag@cslab.ece.ntua.gr>
 *
 */
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/semaphore.h>

#include "crypto.h"
#include "crypto-chrdev.h"
#include "debug.h"

#include "cryptodev.h"

/*
 * Global data
 */
struct cdev crypto_chrdev_cdev;

/**
 * Given the minor number of the inode return the crypto device 
 * that owns that number.
 **/
static struct crypto_device *get_crypto_dev_by_minor(unsigned int minor)
{
	struct crypto_device *crdev;
	unsigned long flags;

	debug("Entering");

	spin_lock_irqsave(&crdrvdata.lock, flags);
	list_for_each_entry(crdev, &crdrvdata.devs, list) {
		if (crdev->minor == minor)
			goto out;
	}
	crdev = NULL;

out:
	spin_unlock_irqrestore(&crdrvdata.lock, flags);

	debug("Leaving");
	return crdev;
}

/*************************************
 * Implementation of file operations
 * for the Crypto character device
 *************************************/

static int crypto_chrdev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	int err;
	unsigned int num_in, num_out, len;
	struct crypto_open_file *crof;
	struct crypto_device *crdev;
	unsigned int *syscall_type;
	int *host_fd;
	struct scatterlist syscall_type_sg, fd_sg, *sgs[2];

	debug("Entering");

	syscall_type = kzalloc(sizeof(*syscall_type), GFP_KERNEL);
	*syscall_type = VIRTIO_CRYPTODEV_SYSCALL_OPEN;
	host_fd = kzalloc(sizeof(*host_fd), GFP_KERNEL);
	*host_fd = -1;
	crof = kzalloc(sizeof(*crof), GFP_KERNEL);
	if (!crof) {
		ret = -ENOMEM;
		goto fail;
	}
	
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto fail;

	/* Associate this open file with the relevant crypto device. */
	crdev = get_crypto_dev_by_minor(iminor(inode));
	if (!crdev) {
		debug("Could not find crypto device with %u minor", 
		      iminor(inode));
		ret = -ENODEV;
		goto fail;
	}

	crof->crdev = crdev;
	crof->host_fd = -1;
	filp->private_data = crof;

	/**
	 * We need two sg lists, one for syscall_type and one to get the 
	 * file descriptor from the host.
	 **/
	/* ?? */
	num_out = 0;
	num_in = 0;
	sg_init_one(&syscall_type_sg, syscall_type, sizeof(*syscall_type));
	sgs[num_out++] = &syscall_type_sg;
	*host_fd = -ENODEV;
	sg_init_one(&fd_sg, host_fd, sizeof(*host_fd));
	sgs[num_out + num_in++] = &fd_sg;

	if(down_interruptible(&crdev->sem))
		return -ERESTARTSYS;
	err = virtqueue_add_sgs(crdev->vq, sgs, num_out, num_in,
							&syscall_type_sg, GFP_ATOMIC);
	virtqueue_kick(crdev->vq);
	/**
	 * Wait for the host to process our data.
	 **/
	/* ?? */
	while (virtqueue_get_buf(crdev->vq, &len) == NULL);
	up(&crdev->sem);
	crof->host_fd = *host_fd;
	/* If host failed to open() return -ENODEV. */
	/* ?? */
	if(*host_fd >= 0) goto succeed;
		
fail:
	ret = -1;
	kfree(crof);
succeed:
	kfree(syscall_type);
	kfree(host_fd);
	debug("Leaving");
	return ret;
}

static int crypto_chrdev_release(struct inode *inode, struct file *filp)
{
	int ret = 0, err;
	struct crypto_open_file *crof = filp->private_data;
	struct crypto_device *crdev = crof->crdev;
	unsigned int *syscall_type, num_in, num_out, len;
	struct scatterlist syscall_type_sg, fd_sg, *sgs[2];

	debug("Entering");

	syscall_type = kzalloc(sizeof(*syscall_type), GFP_KERNEL);
	*syscall_type = VIRTIO_CRYPTODEV_SYSCALL_CLOSE;

	num_out = 0;
	num_in = 0;
	/**
	 * Send data to the host.
	 **/
	/* ?? */
	sg_init_one(&syscall_type_sg, syscall_type, sizeof(*syscall_type));
	sgs[num_out++] = &syscall_type_sg;
	sg_init_one(&fd_sg, &crof->host_fd, sizeof(crof->host_fd));
	sgs[num_out++] = &fd_sg;
	
	if(down_interruptible(&crdev->sem))
		return -ERESTARTSYS;
	err = virtqueue_add_sgs(crdev->vq, sgs, num_out, num_in,
							&syscall_type_sg, GFP_ATOMIC);
	virtqueue_kick(crdev->vq);
	/**
	 * Wait for the host to process our data.
	 **/
	/* ?? */
	while (virtqueue_get_buf(crdev->vq, &len) == NULL);
	up(&crdev->sem);
	
	kfree(crof);
	debug("Leaving");
	return ret;

}

int virtqueue_helper(struct virtqueue *vq, struct scatterlist **sgs, unsigned int out_sgs,
						unsigned int in_sgs, void *data, gfp_t gfp, struct semaphore *sem, unsigned int *len) {
	
	int err;
	if(down_interruptible(sem))
		return -ERESTARTSYS;
	err = virtqueue_add_sgs(vq, sgs, out_sgs, in_sgs,
	                        data, gfp);
	virtqueue_kick(vq);

	while (virtqueue_get_buf(vq, len) == NULL)
		/* do nothing */;
	up(sem);
	return 0;

}

static long crypto_chrdev_ioctl(struct file *filp, unsigned int cmd, 
                                unsigned long arg_)
{
	void __user *arg = (void __user *) arg_;
	void __user	*saved_dst = NULL;
	long ret = 0;
	int err, *host_fd, *host_return_val;
	struct crypto_open_file *crof = filp->private_data;
	struct crypto_device *crdev = crof->crdev;
	struct virtqueue *vq = crdev->vq;
	struct scatterlist syscall_type_sg, output_msg_sg, input_msg_sg,
	    *sgs[8], ioctl_cmd_sg, host_fd_sg, host_return_val_sg,
		first, second, third, fourth;
	unsigned int num_out, num_in, len;
// #define MSG_LEN 16385
// #define MAX_KEY_SIZE 128
	// unsigned char *output_msg, *input_msg;
	unsigned char *session_key, *usr_src, *usr_dst, *usr_iv,
		*backend_dst;
	unsigned int *syscall_type, *ioctl_cmd;
	
	struct session_op *sop;  // CIOCGSESSION
	struct crypt_op *cop;	// CIOCCRYPT
	uint32_t *ses_id;		// CIOCFSESSION

	debug("Entering");

	/**
	 * Allocate all data that will be sent to the host.
	 **/
	// output_msg = kzalloc(MSG_LEN, GFP_KERNEL);
	// input_msg = kzalloc(MSG_LEN, GFP_KERNEL);
	ioctl_cmd = kzalloc(sizeof(*ioctl_cmd), GFP_KERNEL);
	*ioctl_cmd = cmd;
	syscall_type = kzalloc(sizeof(*syscall_type), GFP_KERNEL);
	*syscall_type = VIRTIO_CRYPTODEV_SYSCALL_IOCTL;
	host_fd = kzalloc(sizeof(*host_fd), GFP_KERNEL);
	*host_fd = crof->host_fd;
	host_return_val = kzalloc(sizeof(*host_return_val), GFP_KERNEL);

	num_out = 0;
	num_in = 0;

	/**
	 *  These are common to all ioctl commands.
	 **/
	sg_init_one(&syscall_type_sg, syscall_type, sizeof(*syscall_type));
	sgs[num_out++] = &syscall_type_sg;
	/* ?? */
	sg_init_one(&host_fd_sg, host_fd, sizeof(*host_fd));
	sgs[num_out++] = &host_fd_sg;
	sg_init_one(&ioctl_cmd_sg, ioctl_cmd, sizeof(*ioctl_cmd));
	sgs[num_out++] = &ioctl_cmd_sg;

	sg_init_one(&host_return_val_sg, host_return_val, sizeof(*host_return_val));
	// this should be adde in the sgs table last.

	/**
	 *  Add all the cmd specific sg lists.
	 **/
	switch (cmd) {
	case CIOCGSESSION:
		debug("CIOCGSESSION");
		sop = kzalloc(sizeof(*sop), GFP_KERNEL);
		if (copy_from_user(sop, arg, sizeof(*sop)))
			return -EFAULT;

		session_key = kzalloc(sop->keylen, GFP_KERNEL);
		if (copy_from_user(session_key, sop->key, (size_t) sop->keylen))
			return -EFAULT;
		sg_init_one(&first, session_key, sop->keylen);
		sgs[num_out++] = &first;
		sop->key = session_key;
		sg_init_one(&second, sop, sizeof(*sop));
		sgs[num_out + num_in++] = &second;
		sgs[num_out + num_in++] = &host_return_val_sg;
		
		if (virtqueue_helper(vq, sgs, num_out, num_in, &syscall_type_sg, GFP_ATOMIC,
						&crdev->sem, &len) == -ERESTARTSYS)
			return -ERESTARTSYS;
		ret = (long) *host_return_val;
		
		ses_id = kzalloc(sizeof(*ses_id), GFP_KERNEL);
		*ses_id = sop->ses;
		if (copy_from_user(sop, arg, sizeof(*sop)))
			return -EFAULT;
		sop->ses = *ses_id;
		if (copy_to_user(arg, sop, sizeof(*sop)))
			return -EFAULT;
		kfree(ses_id);
		kfree(session_key);
		kfree(sop);
		break;

	case CIOCFSESSION:
		debug("CIOCFSESSION");
		ses_id = kzalloc(sizeof(*ses_id), GFP_KERNEL);
		if (copy_from_user(ses_id, arg, sizeof(*ses_id)))
			return -EFAULT;

		sg_init_one(&first, ses_id, sizeof(*ses_id));		
		sgs[num_out++] = &first;
		sgs[num_out + num_in++] = &host_return_val_sg;
		
		if (virtqueue_helper(vq, sgs, num_out, num_in, &syscall_type_sg, GFP_ATOMIC,
			&crdev->sem, &len) == -ERESTARTSYS)
		return -ERESTARTSYS;
		ret = (long) *host_return_val;
		
		kfree(ses_id);
		break;

	case CIOCCRYPT:
		debug("CIOCCRYPT");
		cop = kzalloc(sizeof(*cop), GFP_KERNEL);
		if (copy_from_user(cop, arg, sizeof(*cop)))
			return -EFAULT;
		usr_src = kzalloc(cop->len, GFP_KERNEL);
		if (copy_from_user(usr_src, cop->src, cop->len))
			return -EFAULT;
		usr_iv = kzalloc(VIRTIO_CRYPTODEV_BLOCK_SIZE, GFP_KERNEL);
		if (copy_from_user(usr_iv, cop->iv, VIRTIO_CRYPTODEV_BLOCK_SIZE))
			return -EFAULT;
		saved_dst = cop->dst;

		usr_dst = kzalloc(cop->len, GFP_KERNEL);
		cop->dst = usr_dst; 
		cop->src = usr_src;
		cop->iv = usr_iv;
		sg_init_one(&first, cop, sizeof(*cop));
		sgs[num_out++] = &first;
		sg_init_one(&second, usr_src, cop->len);
		sgs[num_out++] = &second;
		sg_init_one(&third, usr_iv, VIRTIO_CRYPTODEV_BLOCK_SIZE);
		sgs[num_out++] = &third;
		sg_init_one(&fourth, usr_dst, cop->len);
		sgs[num_out + num_in++] = &fourth;
		sgs[num_out + num_in++] = &host_return_val_sg;
		
		if (virtqueue_helper(vq, sgs, num_out, num_in, &syscall_type_sg, GFP_ATOMIC,
			&crdev->sem, &len) == -ERESTARTSYS)
		return -ERESTARTSYS;
		ret = (long) *host_return_val;

		backend_dst = kzalloc(cop->len, GFP_KERNEL);
		memcpy(backend_dst, usr_dst, cop->len);
		if (saved_dst == NULL) goto fail;
		if (copy_to_user(saved_dst, backend_dst, cop->len))
			return -EFAULT;

		kfree(backend_dst);
		kfree(cop);
		kfree(usr_dst);
		kfree(usr_src);
		kfree(usr_iv);
		break;

	default:
		debug("Unsupported ioctl command");
		goto fail;
		break;
	}

fail:
	kfree(syscall_type);
	kfree(ioctl_cmd);
	kfree(host_fd);
	kfree(host_return_val);
	debug("Leaving");

	return ret;
}

static ssize_t crypto_chrdev_read(struct file *filp, char __user *usrbuf, 
                                  size_t cnt, loff_t *f_pos)
{
	debug("Entering");
	debug("Leaving");
	return -EINVAL;
}

static struct file_operations crypto_chrdev_fops = 
{
	.owner          = THIS_MODULE,
	.open           = crypto_chrdev_open,
	.release        = crypto_chrdev_release,
	.read           = crypto_chrdev_read,
	.unlocked_ioctl = crypto_chrdev_ioctl,
};

int crypto_chrdev_init(void)
{
	int ret;
	dev_t dev_no;
	unsigned int crypto_minor_cnt = CRYPTO_NR_DEVICES;
	
	debug("Initializing character device...");
	cdev_init(&crypto_chrdev_cdev, &crypto_chrdev_fops);
	crypto_chrdev_cdev.owner = THIS_MODULE;
	
	dev_no = MKDEV(CRYPTO_CHRDEV_MAJOR, 0);
	ret = register_chrdev_region(dev_no, crypto_minor_cnt, "crypto_devs");
	if (ret < 0) {
		debug("failed to register region, ret = %d", ret);
		goto out;
	}
	ret = cdev_add(&crypto_chrdev_cdev, dev_no, crypto_minor_cnt);
	if (ret < 0) {
		debug("failed to add character device");
		goto out_with_chrdev_region;
	}

	debug("Completed successfully");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, crypto_minor_cnt);
out:
	return ret;
}

void crypto_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int crypto_minor_cnt = CRYPTO_NR_DEVICES;

	debug("entering");
	dev_no = MKDEV(CRYPTO_CHRDEV_MAJOR, 0);
	cdev_del(&crypto_chrdev_cdev);
	unregister_chrdev_region(dev_no, crypto_minor_cnt);
	debug("leaving");
}
