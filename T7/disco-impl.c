/* Necessary includes for device drivers */
#include <linux/init.h>
/* #include <linux/config.h> */
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/uaccess.h> /* copy_from/to_user */

#include "kmutex.h"

MODULE_LICENSE("Dual BSD/GPL");

/* Declaration of disco.c functions */
int disco_open(struct inode *inode, struct file *filp);
int disco_release(struct inode *inode, struct file *filp);
ssize_t disco_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t disco_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
void disco_exit(void);
int disco_init(void);

/* Structure that declares the usual file */
/* access functions */
struct file_operations disco_fops = {
  read: disco_read,
  write: disco_write,
  open: disco_open,
  release: disco_release
};

/* Declaration of the init and exit functions */
module_init(disco_init);
module_exit(disco_exit);

/*** The driver *************************************/

#define TRUE 1
#define FALSE 0
#define MAX_SIZE 10  /* Max size of a pipe's buffer */

/* Structures and types*/
typedef enum Kind {NONE, WRITER, READER} Kind;

typedef struct Pipe {
  char buffer[MAX_SIZE];
  int in, out, size;
  KMutex mutex;
  KCondition cond;
  Kind closedBy;
} Pipe;

typedef struct PairRequest {
  int *ready;
  struct file *filp;
} PairRequest;


/* Global variables of the driver */
int disco_major = 61;         /* Major number */
static Kind pend_kind;        /* Who is waiting? A reader, a writer or nobody? */
static PairRequest pair_req;  /* Global variable to pipe-link and wake up a waiting process */
static KMutex gmutex;
static KCondition gcond;


/* Functions */

int disco_open(struct inode *inode, struct file *filp) {
  int rc = 0;
  Kind thisKind = NONE, oppKind = NONE;
  char *strKind, *strKindfun;
  m_lock(&gmutex);

  
  if (filp->f_mode & FMODE_WRITE) {
    thisKind = WRITER;
    strKind = "writer"; strKindfun = "write";
    oppKind = READER;
  }
  else if (filp->f_mode & FMODE_READ)  {
    thisKind = READER;
    strKind = "reader"; strKindfun = "read";
    oppKind = WRITER;
  }
  else
    goto epilog;

  printk("<1>open request for %s\n", strKindfun);
  /* if my opposite kind is already waiting (e.g. if im a writer my opposite kind is a reader)... */ 
  if (pend_kind == oppKind) {
    /* create pipe and pair up*/
    Pipe *p_pipe = kmalloc(sizeof(Pipe), GFP_KERNEL);
    if (p_pipe == NULL) {
      rc = -ENOMEM;
      goto epilog;
    }
    (pair_req.filp)->private_data = filp->private_data = p_pipe;
    p_pipe->in = p_pipe->out = p_pipe->size = 0;
    m_init(&(p_pipe->mutex));
    c_init(&(p_pipe->cond));
    p_pipe->closedBy = NONE;
    
    /* prepare to wake up the waiting process*/
    *(pair_req.ready) = TRUE;
    pend_kind = NONE;
    pair_req = (PairRequest){.ready = NULL, .filp = NULL};
    c_broadcast(&gcond);
  }
  else {
    /* if a process of my same kind is already waiting... */
    if (pend_kind == thisKind) {
      /* open fails: two processes of the same kind are not allowed to wait for open() at the same time */
      rc = -EBUSY;
      printk("<1>open request fail: another %s is already waiting to be paired\n", strKind);
      goto epilog;
    }
    /* else: wait for a process of the opposite kind */ 
    pend_kind = thisKind;
    int ready = FALSE;
    pair_req = (PairRequest){.ready = &ready, .filp = filp};
    while (!ready) {
      if (c_wait(&gcond, &gmutex)) {
        rc = -EINTR;
        pend_kind = NONE;
        pair_req = (PairRequest){.ready = NULL, .filp = NULL};
        printk("<1>open request for %s interrupted\n", strKindfun);
        goto epilog;
      }
    }
    printk("<1>successful pairing\n");
  }

epilog:
  m_unlock(&gmutex);
  return rc;
}

int disco_release(struct inode *inode, struct file *filp) {
  Kind thisKind = NONE;
  char *strKind
  if (filp->f_mode & FMODE_WRITE) {
    thisKind = WRITER;
    strKind = "writer";
  }
  else if (filp->f_mode & FMODE_READ) {
    thisKind = READER;
    strKind = "reader";
  }
  else
    return 0;

  Pipe *p_pipe = (Pipe*) filp->private_data;
  m_lock(&p_pipe->mutex);

  /* if we close the pipe from both ends, we free the memory allocated for the pipe */
  int freePipe = FALSE;
  if (p_pipe->closedBy == NONE) {
    p_pipe->closedBy = thisKind;
    if (thisKind == WRITER)
      c_broadcast(&p_pipe->cond); /* to wake up readers so that read() returns 0 (i.e. EOF)*/
  }
  else
    freePipe = TRUE;

  printk("<1>%s: released", strKind);

  m_unlock(&p_pipe->mutex);
  if (freePipe) {
    filp->private_data = NULL; /* To avoid dangling reference*/
    kfree(p_pipe);
  }
  return 0;
}

ssize_t disco_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
  int scount = count;
  Pipe *p_pipe = (Pipe*) filp->private_data;
  printk("<1>read %p %d\n", filp, scount);
  m_lock(&p_pipe->mutex);

  while (p_pipe->size == 0) {
    /* if the buffer is empty, the reader waits */
    if (c_wait(&p_pipe->cond, &p_pipe->mutex)) {
      printk("<1>read interrupted\n");
      scount = -EINTR;
      goto epilog;
    }
    if (p_pipe->closedBy == WRITER) {
      m_unlock(&p_pipe->mutex);
      scount = 0; /* EOF: a writer closed the pipe*/
      goto epilog;
    }
  }

  if (scount > p_pipe->size) {
    scount = p_pipe->size;
  }

  /* Transfering data to user space */
  for (int k = 0; k < scount; k++) {
    if (copy_to_user(buf + k, p_pipe->buffer + p_pipe->out, 1) != 0) {
      /* buf is an invalid address */
      scount = -EFAULT;
      goto epilog;
    }
    printk("<1>read byte %c (%d) from %d\n",
            p_pipe->buffer[p_pipe->out], p_pipe->buffer[p_pipe->out], p_pipe->out);
    p_pipe->out= (p_pipe->out + 1) % MAX_SIZE;
    p_pipe->size--;
  }

epilog:
  c_broadcast(&p_pipe->cond);
  m_unlock(&p_pipe->mutex);
  return scount;
}


ssize_t disco_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
  int scount = count;
  Pipe *p_pipe = (Pipe*) filp->private_data;
  printk("<1>write %p %d\n", filp, scount);
  m_lock(&p_pipe->mutex);

  for (int k = 0; k < scount; k++) {
    while (p_pipe->size == MAX_SIZE) {
      /* if the buffer is full, the writer waits */
      if (c_wait(&p_pipe->cond, &p_pipe->mutex)) {
        printk("<1>write interrupted\n");
        scount = -EINTR;
        goto epilog;
      }
    }

    if (copy_from_user(p_pipe->buffer + p_pipe->in, buf + k, 1) != 0) {
      /* buf is an invalid address */
      scount = -EFAULT;
      goto epilog;
    }
    printk("<1>write byte %c (%d) at %d\n",
           p_pipe->buffer[p_pipe->in], p_pipe->buffer[p_pipe->in], p_pipe->in);
    p_pipe->in = (p_pipe->in + 1) % MAX_SIZE;
    p_pipe->size++;
    c_broadcast(&p_pipe->cond);
  }

epilog:
  m_unlock(&p_pipe->mutex);
  return scount;
}

void disco_exit(void) {
  /* Freeing the major number */
  unregister_chrdev(disco_major, "disco");
  printk("<1>Removing disco module\n");
}

int disco_init(void) {
  int rc;

  /* Registering device */
  rc = register_chrdev(disco_major, "disco", &disco_fops);
  if (rc < 0) {
    printk(
      "<1>disco: cannot obtain major number %d\n", disco_major);
    return rc;
  }
  pend_kind = NONE;
  pair_req = (PairRequest){.ready = NULL, .filp = NULL};
  m_init(&gmutex);
  c_init(&gcond);
  printk("<1>Inserting pipe module\n");
  return 0;
}