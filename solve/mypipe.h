/**
 * 线程安全的缓冲区(顺序存储的循环队列)、读写者模式
 * 必须凑齐读写双发才能进行实现
 */
#ifndef DATA_SORT_MYPIPE_H
#define DATA_SORT_MYPIPE_H

#define PIPESIZE        (1024 * 1024)     // 缓冲区大小，1M
#define MYPIPE_READ     0x00000001UL    // 读者
#define MYPIPE_WRITE    0x00000002UL    // 写者

typedef void mypipe_t;

/**
 * 初始化缓冲区
 * @return 失败NULL，成功返回一个指针
 */
mypipe_t *mypipe_init(void);

/**
 * 注册身份
 * @param ptr mypipe_init返回的指针
 * @param opmap 位图
 * @return
 */
int mypipe_register(mypipe_t *ptr, int opmap);

/**
 * 注销身份
 * @param ptr mypipe_init返回的指针
 * @param opmap 位图
 * @return
 */
int mypipe_unregister(mypipe_t *ptr, int opmap);

/**
 * 读取字符串，当遇到'\n' 或者 读到字符等于count时返回
 * @param ptr mypipe_init返回的指针
 * @param buf 读入数据的目标地址
 * @param count 想要读入的字节数
 * @return 成功读入的字节个数, -1表示管道空且无写者
 */
int mypipe_gets(mypipe_t *ptr, void *buf, size_t count);

/**
 * 从缓冲区中读取字节
 * @param ptr mypipe_init返回的指针
 * @param buf 读入数据的目标地址
 * @param count 想要读入的字节数
 * @return 成功读入的字节个数, -1表示管道空且无写者
 */
int mypipe_read(mypipe_t *ptr, void *buf, size_t count);

/**
 * 往缓冲区写字节
 * @param ptr mypipe_init返回的指针
 * @param buf 要写数据的原地址
 * @param count 想要写入的字节数
 * @return 成功写入的字节个数, -1表示管道满，且无读者
 */
int mypipe_write(mypipe_t *ptr, const void *buf, size_t count);

/**
 * 清理现场，释放资源
 * @param ptr
 * @return 0表示成功，其他均表示失败
 */
int mypipe_destroy(mypipe_t *ptr);


#endif //DATA_SORT_MYPIPE_H
