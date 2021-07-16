#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "mypipe.h"

/* 用队列模拟缓冲区 */
struct mypipe_st {
    int head;               // 头指针，还未读的位置
    int tail;               // 尾指针,即将要写的位置
    int datasize;           // 队列中数据长度
    char data[PIPESIZE];    // 数据
    int count_rd;           // 读者个数
    int count_wr;           // 写者个数
    pthread_mutex_t mut;
    pthread_cond_t cond;
};

mypipe_t *mypipe_init(void) {
    struct mypipe_st *me;

    me = malloc(sizeof(*me));
    if (me == NULL)
        return NULL;

    me->head = 0;
    me->tail = 0;
    me->datasize = 0;
    me->count_rd = 0;
    me->count_wr = 0;
    pthread_mutex_init(&me->mut, NULL);
    pthread_cond_init(&me->cond, NULL);

    return me;
}

int mypipe_register(mypipe_t *ptr, int opmap) {
    struct mypipe_st *me = ptr;

    pthread_mutex_lock(&me->mut);
    if (opmap & MYPIPE_READ)
        me->count_rd++;
    if (opmap & MYPIPE_WRITE)
        me->count_wr++;

    pthread_cond_broadcast(&me->cond);
    while (me->count_rd <= 0 || me->count_wr <= 0)  // 只有读写者一方时进行等待
        pthread_cond_wait(&me->cond, &me->mut);
    pthread_mutex_unlock(&me->mut);

    return 0;
}

int mypipe_unregister(mypipe_t *ptr, int opmap) {
    struct mypipe_st *me = ptr;

    pthread_mutex_lock(&me->mut);
    if (opmap & MYPIPE_READ)
        me->count_rd--;
    if (opmap & MYPIPE_WRITE)
        me->count_wr--;

    pthread_cond_broadcast(&me->cond);  // 唤醒读写者的等待
    pthread_mutex_unlock(&me->mut);
    return 0;
}

static int next(int before) {
    return (before + 1) % PIPESIZE;
}

static int mypipe_readbyte_unlocked(struct mypipe_st *me, char *datap) {
    if (me->datasize <= 0)
        return -1;

    *datap = me->data[me->head];
    me->head = next(me->head);
    me->datasize--;
    return 0;
}

int mypipe_gets(mypipe_t *ptr, void *buf, size_t count) {
    struct mypipe_st *me = ptr;
    int i, ret;

    pthread_mutex_lock(&me->mut);
    while (me->datasize <= 0 && me->count_wr > 0)   // 管道空
        pthread_cond_wait(&me->cond, &me->mut);

    if(me->datasize <= 0 && me->count_wr <= 0) {    // 管道空，且无写者时退出
        pthread_mutex_unlock(&me->mut);
        return -1;
    }

    for (i = 0; i < count; i++) {
        ret = mypipe_readbyte_unlocked(me, buf + i);
        if(ret == 0 && *((char*)buf + i) == '\n') {  // 读取成功且读到'\n'退出
            if (i < count - 1) {
                i++;
                *((char*)buf + i) = '\0';
            }
            break;
        }
        else if (ret != 0) {    // 读取不成功
//            pthread_cond_wait(&me->cond, &me->mut);
//            pthread_cond_broadcast(&me->cond);  // 唤醒写者

            pthread_cond_broadcast(&me->cond);  // 唤醒写者
            while (me->datasize <= 0 && me->count_wr > 0) {  // 管道空
                pthread_cond_wait(&me->cond, &me->mut);

            }

            if(me->datasize <= 0 && me->count_wr <= 0) {    // 管道空，且无写者时退出
                pthread_mutex_unlock(&me->mut);
                return -1;
            }
            i--;
        }
    }

    pthread_cond_broadcast(&me->cond);  // 唤醒写者
    pthread_mutex_unlock(&me->mut);
    return i;
}

int mypipe_read(mypipe_t *ptr, void *buf, size_t count) {
    struct mypipe_st *me = ptr;
    int i;

    pthread_mutex_lock(&me->mut);
    while (me->datasize <= 0 && me->count_wr > 0)   // 管道空
        pthread_cond_wait(&me->cond, &me->mut);

    if(me->datasize <= 0 && me->count_wr <= 0) {    // 管道空，且无写者时退出
        pthread_mutex_unlock(&me->mut);
        return -1;
    }

    for(i = 0; i < count; i++) {
        if(mypipe_readbyte_unlocked(me, buf + i) != 0)
            break;
    }
    pthread_cond_broadcast(&me->cond);  // 唤醒写者
    pthread_mutex_unlock(&me->mut);
    return i;
}

static int mypipe_writebyte_unlocked(struct mypipe_st *me, const char *datap) {
    if (me->datasize == PIPESIZE)
        return -1;

    me->data[me->tail] = *datap;
    me->tail = next(me->tail);
    me->datasize++;
    return 0;
}

int mypipe_write(mypipe_t *ptr, const void *buf, size_t count) {
    struct mypipe_st *me = ptr;
    int i;

    pthread_mutex_lock(&me->mut);
    while (me->datasize == PIPESIZE && me->count_rd > 0) // 管道满
        pthread_cond_wait(&me->cond, &me->mut);

    if(me->datasize == PIPESIZE && me->count_rd <= 0) {    // 管道满，且无读者时退出
        pthread_mutex_unlock(&me->mut);
        return -1;
    }

    for(i = 0; i < count; i++) {
        if (mypipe_writebyte_unlocked(me, buf + i) != 0)
            break;
    }
    pthread_cond_broadcast(&me->cond);  // 唤醒读者
    pthread_mutex_unlock(&me->mut);
    return i;
}

int mypipe_destroy(mypipe_t *ptr) {
    struct mypipe_st *me = ptr;

    pthread_mutex_destroy(&me->mut);
    pthread_cond_destroy(&me->cond);
    free(ptr);
    return 0;
}