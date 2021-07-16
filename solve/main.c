#include <stdio.h>
#include <stdlib.h>

#include "data_sort.h"

#define INPUTFILE   "./source_data.dat"       // 源文件位置
#define OUTPUTFILE  "./source_data_out.dat"

int main() {

    FILE *sfp = NULL, *dfp = NULL;
    struct file_sort_t *ptr;

    // 打开源文件
    sfp = fopen(INPUTFILE, "r");
    if (sfp == NULL) {
        perror("source fopen()");
        exit(1);
    }

    // 打开目标文件
    dfp = fopen(OUTPUTFILE, "w");
    if (dfp == NULL) {
        fclose(sfp);
        perror("destination fopen()");
    }

    ptr = sort_init(sfp, dfp);
    if (ptr == NULL) {
        fprintf(stderr, "sort_init()");
        exit(1);
    }

    // 得到初始归并段
    get_merge_segments(ptr);

    // 进行归并排序
    mergeSort(ptr);

    sort_destory(ptr);
    fclose(dfp);
    fclose(sfp);


    exit(0);
}