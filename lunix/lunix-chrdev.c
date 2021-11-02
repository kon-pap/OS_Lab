/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * <Orfeas Zografos(03117160)>
 * <Konstantinos Papaioannou(03117005)>
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>


#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 */
struct cdev lunix_chrdev_cdev;

/*
 * Just a quick [unlocked] check to see if the cached
 * chrdev state needs to be updated from sensor measurements.
 */
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	int ret = 0; 
	WARN_ON ( !(sensor = state->sensor));
	/* ? */
	if(sensor->msr_data[state->type]->last_update > state->buf_timestamp) {
		ret = 1; // if refresh is needed
	}
	/* The following return is bogus, just for the stub to compile */
	return ret;
}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	uint32_t measurement, new_timestamp;
	long readable_data;
	unsigned long flags;
	int ret = 0, available_space, just_written;
	int subdecimal = 0, hundrends = 0, tens = 0, units = 0;
	char counter_buffer[20];
	WARN_ON ( !(sensor = state->sensor));


	debug("Initiating state update\n");
	/*
	 * Grab the raw data quickly, hold the
	 * spinlock for as little as possible.
	 */
	spin_lock_irqsave(&sensor->lock, flags); // ORF|
	/*
	 * Any new data available?
	 */
	if(lunix_chrdev_state_needs_refresh(state)) { // ORF|
		new_timestamp = sensor->msr_data[state->type]->last_update;
		measurement = sensor->msr_data[state->type]->values[0];
	} else {
		spin_unlock_irqrestore(&sensor->lock, flags);
		ret = -EAGAIN;
		printk(KERN_DEBUG "didn't need refresh, EXITING with -EAGAIN\n");
		goto out;
	}
	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */
	spin_unlock_irqrestore(&sensor->lock, flags);
	if(state->type == BATT)
		readable_data = lookup_voltage[measurement];
	else if(state->type == LIGHT)
		readable_data = lookup_light[measurement];
	else if(state->type == TEMP)
		readable_data = lookup_temperature[measurement];

	state->buf_lim = 0;
	available_space = 20;
	subdecimal = (readable_data%1000);
	hundrends = subdecimal / 100;
	tens = (subdecimal - hundrends*100) / 10;
	units = (subdecimal - hundrends*100 - tens*10);	

	just_written = snprintf(counter_buffer, available_space, "%ld.%d%d%d", readable_data/1000, hundrends, tens, units);// I don't like null characters, mostly unnecessary
	just_written = snprintf(&state->buf_data[(state->buf_lim)], just_written, "%ld.%d%d%d", readable_data/1000, hundrends, tens, units);

	state->buf_lim = (state->buf_lim + just_written); 
	state->buf_timestamp = new_timestamp;

out:
	debug("State update done\n");
	return ret;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/

static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* Declarations */
	/* ? */
	unsigned int min, maj;			// to identify which "device" to "bind"
	struct lunix_sensor_struct *sensor; 	// temp hold of the appropriate sensor struct
	struct lunix_chrdev_state_struct *state;		// temp hold of the created state struct
	int ret;

	debug("file open attempt\n");
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0) // this makes sure llseek, pread, pwrite operations are not available
		goto out;
	/*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */
	min = iminor(inode); // Capture the minor
	sensor = &lunix_sensors[min >> 3];	 // divide by 8 to get the sensor number, lunix_sensors declared in lunix.h
	
	/* Allocate a new Lunix character device private state structure */
	/* ? */
	state = kmalloc(sizeof(*state), GFP_KERNEL);	// allocate space for a lunix_chrdev_state_struct struct.
	if(!state) {
		printk(KERN_ERR "Failed to allocate memory for Lunix state struct\n");
		ret = -ENOMEM;
		goto out;
	}
	state->sensor = sensor;	// make this private state point to the proper sensor struct
	state->type = (min - ((min >> 3) * 8));
	state->buf_timestamp = 0;
	state->buf_lim = 0;
	sema_init(&(state->lock),1);
	/*
	 * this places our custom struct into the file struct cause we know it's accessed from here
	 * a bit later on (in the read implementation) 
	 */
	filp->private_data = state; 
out:
	debug("file open done, exiting with ret = %d\n", ret);
	return ret;
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);	// this frees up the space allocated during opening.
	printk(KERN_DEBUG "private state struct destroyed\n");
	return 0;
}

static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Why? */
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	ssize_t ret;
	int readable_amount;
	struct lunix_sensor_struct *sensor;
	struct lunix_chrdev_state_struct *state;
	char mini_buff[20];

	state = filp->private_data;
	WARN_ON(!state);

	sensor = state->sensor;
	WARN_ON(!sensor);

	/* Lock? */
	if(down_interruptible(&(state->lock)))
		return -ERESTARTSYS;
	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement, do so
	 */
	if (*f_pos == 0) {
		while (lunix_chrdev_state_update(state) == -EAGAIN) {
			up(&(state->lock));
			if(wait_event_interruptible(sensor->wq,state->buf_timestamp < sensor->msr_data[state->type]->last_update) == -ERESTARTSYS)
				return -ERESTARTSYS;
			if(down_interruptible(&(state->lock)))
				return -ERESTARTSYS;		
		}
	}

	/* End of file */
	/* ? */

	readable_amount = state->buf_lim - *f_pos;
	if(*f_pos == 0 && cnt > readable_amount) {
		memcpy(mini_buff, &state->buf_data[*f_pos], readable_amount);
		mini_buff[readable_amount] = '\n';
		*f_pos = state->buf_lim;
		cnt = readable_amount + 1;
	} else {
		cnt = (cnt > readable_amount) ? readable_amount : cnt;
		memcpy(mini_buff, &state->buf_data[*f_pos], cnt);
		*f_pos += cnt;
	}

	ret = cnt;
	if(copy_to_user(usrbuf, mini_buff, cnt)) {
		ret = -EFAULT;
		goto out;
	}
	printk(KERN_DEBUG "Just copied to user\n");
	
	/* Determine the number of cached bytes to copy to userspace */
	/* ? */
	if(*f_pos >= state->buf_lim) {
		printk(KERN_DEBUG "Zeroing the *f_pos\n");
		*f_pos = 0;
	}
out:
	up(&(state->lock));
	return ret;
}

void mm_open(struct vm_area_struct *vma) {printk(KERN_NOTICE "Opening supposedly\n");}
void mm_close(struct vm_area_struct *vma) {printk(KERN_NOTICE "Closing supposedly\n");}
static struct vm_operations_struct my_vm_ops = {
	.open = mm_open,
	.close = mm_close,
};
static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct lunix_sensor_struct *sensor;
	struct lunix_chrdev_state_struct *state;
	unsigned long temp, offset;
	state = filp->private_data;
	WARN_ON(!state);
	sensor = state->sensor;
	WARN_ON(!sensor);

	vma->vm_flags |= VM_LOCKED;
	
	temp = (unsigned long)(sensor->msr_data[state->type]);
	temp = virt_to_phys((void*)temp)  >> PAGE_SHIFT;

	if(remap_pfn_range(vma, vma->vm_start, temp ,vma->vm_end - vma->vm_start, vma->vm_page_prot)){
		printk(KERN_DEBUG "failed to remap range\n");
		return -EAGAIN;
	}
	vma->vm_ops = &my_vm_ops;
	mm_open(vma);	
	return 0;
}

static struct file_operations lunix_chrdev_fops = 
{
	// .llseek			= no_llseek,
	.owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

int lunix_chrdev_init(void)
{
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
	
	debug("initializing character device\n");
	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
	lunix_chrdev_cdev.owner = THIS_MODULE;
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	
	/* ? */
	/* register_chrdev_region? */
	ret = register_chrdev_region(dev_no,lunix_minor_cnt,"lunix");
	// printk("register_chrdev_region returned: %d\n", ret);
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}

	/* ? */
	/* cdev_add? */
	ret = cdev_add(&lunix_chrdev_cdev,dev_no,lunix_minor_cnt);	// <-------- Uncomment at the end.
	// printk("cdev_add returned: %d\n", ret);
	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}
	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}

void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
		
	debug("entering\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving\n");
}
