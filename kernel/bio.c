// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// bucket number for bufmap
#define NBUFMAP_BUCKET 13
// hash function for bufmap
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

extern uint ticks;


struct {
  struct spinlock evictLock;
  struct buf buf[NBUF];

  struct buf *HashBucket[NBUFMAP_BUCKET];
  struct spinlock hashLock[NBUFMAP_BUCKET];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcache;



void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evictLock, "bcache");
  for (int i = 0; i < NBUFMAP_BUCKET; i++) {
    initlock(&bcache.hashLock[i], "bcache");
    bcache.HashBucket[i] = 0;
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++) {
    int ind = (b - bcache.buf) % NBUFMAP_BUCKET;
    b->next = bcache.HashBucket[ind];
    bcache.HashBucket[ind] = b;
    b->lastuse = 0;
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint64 key = BUFMAP_HASH(dev, blockno);
  acquire(&bcache.hashLock[key]);

  // Is the block already cached?
  for(b = bcache.HashBucket[key]; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.hashLock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.

  release(&bcache.hashLock[key]);
  acquire(&bcache.evictLock);

  acquire(&bcache.hashLock[key]);
  for (b = bcache.HashBucket[key]; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.evictLock);
      release(&bcache.hashLock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.hashLock[key]);

  struct buf *choicebuf = 0;
  //struct buf *beforbuf = 0;
  uint64 timeStap = ticks;
  int bucketNum = -1;

  for (int i = 0; i < NBUFMAP_BUCKET; i++) {
    acquire(&bcache.hashLock[i]);
    b = bcache.HashBucket[i];
    for (; b; b = b->next) {
      if (b->refcnt == 0 && timeStap >= b->lastuse) {
        if (bucketNum != -1 && bucketNum != i) {
          release(&bcache.hashLock[bucketNum]);
        }
        choicebuf = b;
        bucketNum = i;
        timeStap = b->lastuse;
      }
    }
    if (bucketNum != i) release(&bcache.hashLock[i]);
  }
  
  if (!choicebuf) {
    panic("bget: no buffers");
  }

  choicebuf->dev = dev;
  choicebuf->blockno = blockno;
  choicebuf->valid = 0;
  choicebuf->refcnt = 1;
  if (bucketNum != key) {
    struct buf *tmp = bcache.HashBucket[bucketNum];
    if (tmp == choicebuf) {
      bcache.HashBucket[bucketNum] = tmp->next;
    } else {
      while (tmp->next != choicebuf) {
        tmp = tmp->next;
      }
      if (!tmp) panic("error bucket!\n");
      tmp->next = tmp->next->next;
    }
    release(&bcache.hashLock[bucketNum]);
    acquire(&bcache.hashLock[key]);

    choicebuf->next = bcache.HashBucket[key];
    bcache.HashBucket[key] = choicebuf;
  }
  release(&bcache.hashLock[key]);
  release(&bcache.evictLock);
  acquiresleep(&choicebuf->lock);

  return choicebuf;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint64 key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.hashLock[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->lastuse = ticks;
  }
  release(&bcache.hashLock[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.hashLock[key]);
  b->refcnt++;
  release(&bcache.hashLock[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.hashLock[key]);
  b->refcnt--;
  release(&bcache.hashLock[key]);
}


