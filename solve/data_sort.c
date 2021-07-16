#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "data_sort.h"
#include "mypipe.h"

#define BUFSIZE     1024
#define BUCKETSIZE  10      // 基数排序个数

/* 记录输入输出文件的结构体 */
struct file_sort_st {
    FILE *sfp, *dfp;
};

/* 基数排序桶 */
struct bucket_st {
    int no;     // 桶标号
    struct item_st *head;
};

/* 归并排序需要的数据结构 */
struct merge_sort_st {
    FILE *fp;               // 文件指针
    struct item_st item;    // 当前读取到的内容
    long long rtimes;             // 需要读取的次数
    long long times;              // 已经读取的次数
};

static pthread_t rtid;                 // 读线程，从文件中读数据到pipe
static pthread_t wtid[THREAD_NUM];     // 写线程，负责从缓冲区pipe读入数据，生成归并段
static mypipe_t *mypipe;               // 读写者缓冲区

static struct itemRepository_st itemsRep[MAX_MERGE_SEM];    // 归并段(记录仓库、文件个数)
static long long temp_file_items[MAX_MERGE_SEM];            // 归并文件中记录的条数
static int undealrep_no = 0;                                // 未有线程操作的rep起始号(也是初始归并段个数)
static pthread_mutex_t repmut = PTHREAD_MUTEX_INITIALIZER;
static int round = 1;                                       // 用于生成临时文件名：轮数

static int ltree[MAX_MERGE_WAYS];                           // 败者树

static void* readTask(void *p);                             // 读任务：从文件中读入数据写入缓冲区pipe
static void *writeTask(void *p);                            // 写任务：从pipe中取数据生成归并段
static void radixSort(struct item_st **pSt, int length);    // 对归并段进行基数排序
static void createLoserTree(struct merge_sort_st **runs, int nums); // 创建败者树
static void adjust(struct merge_sort_st **runs, int nums, int current); // 调整败者树

/**
 * 归并段是否生成结束
 * @return 
 *      return == 0     生成未结束
 *             == 1     生成结束
 */
static int isGenerateOver_unlocked() {
    return undealrep_no >= MAX_MERGE_SEM;
}

/**
 * 读取缓冲区pipe得到Item
 * @return
 */
static struct item_st *getItem() {
    struct item_st *me;
    char buf[BUFSIZE];

    me = malloc(sizeof(*me));
    if (me == NULL) {
        perror("malloc()");
        exit(1);
    }

    if (mypipe_gets(mypipe, buf, BUFSIZE) < 0) {
        free(me);
        return NULL;
    }
    sscanf(buf, "%d %s\n", &me->key, me->value);
    me->next = NULL;

    return me;
}

file_sort_t *sort_init(FILE *sfp, FILE *dfp) {
    struct file_sort_st *me;

    me = malloc(sizeof(*me));
    if (me == NULL)
        return NULL;
    me->sfp = sfp;
    me->dfp = dfp;

    mypipe = mypipe_init();
    if (mypipe == NULL) {
        free(me);
        return NULL;
    }

    return me;
}

void get_merge_segments(file_sort_t *ptr) {
//    struct file_sort_st *me = ptr;
    int err, i, j;

    // 读线程: 从文件读写入pipe
    err = pthread_create(&rtid, NULL, readTask, ptr);
    if (err) {
        fprintf(stderr, "pthread_create(): %s\n", strerror(err));
        exit(1);
    }

    // 写线程: 从pipe中取
    for (i = 0; i < THREAD_NUM; i++) {
        err = pthread_create(&wtid[i], NULL, writeTask, NULL);
        if (err) {
            pthread_join(rtid, NULL);
            for (j = 0; j < i; j++)
                pthread_join(wtid[j], NULL);
            fprintf(stderr, "pthread_create(): %s\n", strerror(err));
            exit(1);
        }
    }

    pthread_join(rtid, NULL);           // 线程回收
    for (i = 0; i < THREAD_NUM; i++)
        pthread_join(wtid[i], NULL);

    round++;
}


void sort_destory(file_sort_t *ptr) {

    pthread_mutex_destroy(&repmut);
    mypipe_destroy(mypipe);
    free(ptr);
}

/**
 * 从文件中读入内容，写入到pipe缓冲区
 * @param ptr
 */
static void* readTask(void *p) {
    struct file_sort_st *ptr = p;
    size_t len;
    int pos, ret;
    char buf[BUFSIZE];

    mypipe_register(mypipe, MYPIPE_WRITE);
    while (1) {
        len = (size_t) read(fileno(ptr->sfp), buf, BUFSIZE);
        if (len < 0) {
            if (errno == EINTR)
                continue;
            perror("read()");
            break;
        } else if (len == 0)    // 文件读取结束
            break;

        pos = 0;
        while (len > 0) {   // 此处使用循环是为了防止缓冲区满无法完全写入
            ret = mypipe_write(mypipe, buf + pos, len);
            if (ret < 0)
                break;
            pos += ret;
            len -= ret;
        }
    }

    mypipe_unregister(mypipe, MYPIPE_WRITE);
    pthread_exit(NULL);
}

/**
 * 从缓冲区pipe取数据，生成归并段，并排序后写入临时文件
 */
static void *writeTask(void *p) {
    int deal_no;    // 当前处理的是第几个归并段
    struct item_st *item = NULL;
    int i;
    FILE *tfp;
    char fileName[BUFSIZE];

    mypipe_register(mypipe, MYPIPE_READ);
    while (1) {
        pthread_mutex_lock(&repmut);
        if (isGenerateOver_unlocked()) {
            undealrep_no++;
            pthread_mutex_unlock(&repmut);
            break;
        }
        deal_no = undealrep_no;
        undealrep_no++;
        pthread_mutex_unlock(&repmut);

        i = 0;
        while (1) {
            item = getItem();
            if (item == NULL)   // 读取结束
                break;
            itemsRep[deal_no].items[i] = item;
            itemsRep[deal_no].length++;
            if (itemsRep[deal_no].length == ITEMSPERFILE)
                break;
            i++;
        }

        if (itemsRep[deal_no].length <= 0) {
            break;
        }

        // 对每个归并段进行基数排序
        radixSort(itemsRep[deal_no].items, itemsRep[deal_no].length);

        // 写入文件
        sprintf(fileName, "./tmp/tmp_r%d_%d.dat", round, deal_no);
        tfp = fopen(fileName, "w");
        if (tfp == NULL) {
            perror("fopen()");
            exit(1);
        }

        for (i = 0; i < itemsRep[deal_no].length; i++) {
            fprintf(tfp, "%d %s\n", itemsRep[deal_no].items[i]->key, itemsRep[deal_no].items[i]->value);
        }
        fflush(tfp);
        fclose(tfp);

        temp_file_items[deal_no] = itemsRep[deal_no].length;
        // 写入文件后释放对应的空间
        for (i = 0; i < itemsRep[deal_no].length; i++)
            free(itemsRep[deal_no].items[i]);
    }

    mypipe_unregister(mypipe, MYPIPE_READ);
    pthread_exit(NULL);
}


/**
 * 对归并段进行基数排序
 * @param pSt       待排序数据数组的首地址
 * @param length    待排序数据数组的长度
 */
static void radixSort(struct item_st **pSt, int length) {

    struct bucket_st bucket[BUCKETSIZE];    // 10个桶 0～9
    struct item_st *ptail[BUCKETSIZE];      // 记录10个桶的尾指针
    int i, j, k, n;
    int digit;           // 当前处理的是哪个桶
    int max;             // 找到归并段中最大值
    int maxLength;       // 数组中最大数的位数
    char buf[16];
    struct item_st *before ,*tmp;

    // 初始化桶
    for (i = 0; i < BUCKETSIZE; i++) {
        bucket[i].no = i;
        bucket[i].head = malloc(sizeof(struct item_st));    // 头结点 不放任何数据
        ptail[i] = bucket[i].head;
    }

    // 找到数组中最大数的位数
    max = pSt[0]->key;
    for (i = 1; i < length; i++) {
        if (pSt[i]->key > max)
            max = pSt[i]->key;
    }
    sprintf(buf, "%d", max);
    maxLength = (int) strlen(buf);

    for (i = 0, n = 1;i < maxLength; i++, n *= 10) {
        // 从右往左将对应位数据放入桶中
        for (j = 0; j < length; j++) {
            digit = pSt[j]->key / n % 10;
            ptail[digit]->next = pSt[j];
            ptail[digit] = ptail[digit]->next;
        }

        k = 0;
        // 将桶中元素按序取出
        for (j = 0; j < BUCKETSIZE; j++) {
            before = bucket[j].head;
            tmp = bucket[j].head->next;
            while (tmp != NULL) {
                before->next = NULL;
                before = tmp;
                pSt[k++] = tmp;
                tmp = tmp->next;
            }
            ptail[j] = bucket[j].head;
        }
    }

    for (i = 0; i < BUCKETSIZE; i++) {    // 初始化桶
        ptail[i] = NULL;
        free(bucket[i].head);
    }
}

/**
 * 从归并段中读取每个记录
 * @param run 归并段指针
 */
static void readItem(struct merge_sort_st *run) {

    char buf[BUFSIZE];

    fgets(buf, BUFSIZE, run->fp);
    sscanf(buf, "%d %s\n", &run->item.key, run->item.value);
    run->times++;
}

/**
 * 归并
 * @param nums  归并文件的个数
 * @param round 归并文件的文件名轮数
 * @param start 归并文件的文件名起始下标
 * @param dfd   归并生成文件指针
 */
static void merge(int nums, int round, int start, FILE *dfd) {

    struct merge_sort_st **runs = malloc(nums * sizeof(struct merge_sort_st*));
    int i;
    int live_runs;
    char fileName[BUFSIZE];

    live_runs = nums;
    // 初始化每个归并段对应的结构体
    for (i = 0; i < nums; i++) {
        runs[i] = malloc(sizeof(struct merge_sort_st));
        sprintf(fileName, "./tmp/tmp_r%d_%d.dat", round, start + i);
        runs[i]->fp = fopen(fileName, "r");
        if (runs[i]->fp == NULL) {
            fprintf(stderr, "%s fopen(): %s\n", fileName, strerror(errno));
            exit(1);
        }
        runs[i]->rtimes = temp_file_items[start + i];
        runs[i]->times = 0;
        if (runs[i]->rtimes != 0) {
            readItem(runs[i]);
        } else {
            live_runs--;
        }
    }

    // 创建败者树
    createLoserTree(runs, nums);

    while (live_runs > 0) {
        // 将败者数的胜利节点数据写入输出文件
        fprintf(dfd, "%d %s\n", runs[ltree[0]]->item.key, runs[ltree[0]]->item.value);
        if (runs[ltree[0]]->times >= runs[ltree[0]]->rtimes) {  // 该归并文件读取结束
            runs[ltree[0]]->item.key = -1;
            live_runs--;
        } else {
            readItem(runs[ltree[0]]);
        }

        adjust(runs, nums, ltree[0]);
    }

    fflush(dfd);
    for (i = 0; i < nums; i++) {
        fclose(runs[i]->fp);
        free(runs[i]);
    }
    free(runs);
}

/**
 * 归并排序
 * @param ptr
 */
void mergeSort(file_sort_t *ptr) {

    struct file_sort_st *me = ptr;
    int merge_sem = undealrep_no - 1;   // 归并段的个数
    int count;                      // 归并计数
    int remain;
    FILE *tmpf;                     // 中间文件指针
    int i;
    long long temp;
    char fileName[BUFSIZE];

    while (merge_sem > MAX_MERGE_WAYS) {    // 需要归并的段数大于最大能支持的归并路数需要进行多次归并
        count = 0;
        remain = merge_sem;
        while (1) {
            // 打开一个待写的临时文件
            sprintf(fileName, "./tmp/tmp_r%d_%d.dat", round, count);
            tmpf = fopen(fileName, "w");
            if (tmpf == NULL) {
                perror("fopen()");
                exit(1);
            }

            if (remain > MAX_MERGE_WAYS) {
                merge(MAX_MERGE_WAYS, round - 1, count * 100, tmpf);
                // 修改 temp_file_items
                temp = 0;
                for (i = 0; i < MAX_MERGE_WAYS; i++)
                    temp += temp_file_items[count * 100 + i];
                temp_file_items[count] = temp;
                remain -= MAX_MERGE_WAYS;
                count++;
            } else {
                merge(remain, round - 1, count * 100, tmpf);
                // 修改 temp_file_items
                temp = 0;
                for (i = 0; i < remain; i++)
                    temp += temp_file_items[count * 100 + i];
                temp_file_items[count] = temp;
                count++;
                break;
            }
            fclose(tmpf);
        }
        round++;
        merge_sem = count;
    }

    merge(merge_sem, round - 1, 0, me->dfp);
}

/**
 * 创建败者树
 * @param runs 归并文件结构体数组指针
 * @param nums 归并文件个数
 */
static void createLoserTree(struct merge_sort_st **runs, int nums) {
    int i;

    for (i = 0; i < nums; i++)
        ltree[i] = -1;
    for (i = nums - 1; i >= 0; i--)
        adjust(runs, nums, i);
}

/**
 * 调整败者树
 * @param runs      归并文件结构体数组指针
 * @param nums      归并文件个数
 * @param current   当前归并文件
 */
static void adjust(struct merge_sort_st **runs, int nums, int current) {
    int t = (nums + current) / 2;
    int tmp;

    while (t != 0) {    // current中一直记录着当前胜者
        if (current == -1)
            break;
        if (ltree[t] == -1 || runs[current]->item.key < 0 ||
                (runs[ltree[t]]->item.key > 0 && runs[current]->item.key > runs[ltree[t]]->item.key)) {
            tmp = current;
            current = ltree[t];
            ltree[t] = tmp;
        }
        t /= 2;
    }
    ltree[0] = current;
}