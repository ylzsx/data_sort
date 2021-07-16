#ifndef DATA_SORT_DATA_SORT_H
#define DATA_SORT_DATA_SORT_H

#define TEST_SIZE       10000000                // 总记录个数
#define ITEMSPERFILE    10000                    // 初始每个归并段最多包含条目个数
#define MAX_MERGE_SEM   (TEST_SIZE / ITEMSPERFILE)  // 最大归并段个数(即临时文件个数)

#define MAX_MERGE_WAYS  100                     // 同时能进行的最大归并路数

#define THREAD_NUM      1                       // 写线程个数

#define STRLEN          32                      // value 字符串长度
/* 文件中每个条目对应的结构体 */
struct item_st {
    int key;
    char value[STRLEN];
    struct item_st *next;                       // 基数排序时候会用到该指针
};

/* 归并段对应的结构体 */
struct itemRepository_st {
    struct item_st *items[ITEMSPERFILE];         // 一个临时文件的所有条目
    int length;                                  // 实际拥有的条目个数
};

typedef void file_sort_t;

/**
 * 排序功能初始化
 * @param sfd 原文件指针
 * @param dfd 目标文件指针
 * @return 失败返回NULL， 成功返回一个指针
 */
file_sort_t *sort_init(FILE *sfd, FILE *dfd);

/**
 * 通过读取源文件生成初始需要的归并段
 * @param ptr sort_init得到的指针
 */
void get_merge_segments(file_sort_t *ptr);

/**
 * 进行归并排序
 * @param ptr sort_init得到的指针
 */
void mergeSort(file_sort_t *ptr);

/**
 * 清理现场，释放资源
 * @param ptr sort_init得到的指针
 */
void sort_destory(file_sort_t *ptr);

#endif //DATA_SORT_DATA_SORT_H
