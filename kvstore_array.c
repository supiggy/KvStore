
#include <string.h>

#include "kvstore.h"


//这是 数组实现的 KVStore，提供了基本的增删改查
//它使用一个固定大小的数组来存储键值对
//每个键值对由一个结构体 kvs_array_item 表示，包含 key 和 value 两个字符串指针。数组的大小由 KVS_ARRAY_SIZE 定义，这里设置为 1024。
struct kvs_array_item array_table[KVS_ARRAY_SIZE] = {0};

int array_idx = 0;

int kvstore_array_set(char *key, char *value) {
    if (key == NULL || value == NULL) {
        return -1;
    }

    for (int i = 0; i < array_idx; i++) {
        if (strcmp(array_table[i].key, key) == 0) {
            return 1;
        }
    }

    if (array_idx >= KVS_ARRAY_SIZE) {
        return -1; // Array is full
    }

    //为了避免直接使用传入的指针，导致后续修改原始字符串时影响到存储的键值对
    //我们需要在数组中为 key 和 value 分配新的内存，并复制它们的内容。
    char *key_copy = kvstore_malloc(strlen(key) + 1);
    if (key_copy == NULL) {
        return -1; // Memory allocation failed
    }

    char *value_copy = kvstore_malloc(strlen(value) + 1);
    if (value_copy == NULL) {
        kvstore_free(key_copy);
        return -1; // Memory allocation failed
    }


    //复制字符串内容到新分配的内存中, 这里使用strncpy函数来复制字符串, 它会确保不会发生缓冲区溢出, 因为我们已经为key_copy和value_copy分配了足够的内存来存储key和value的内容。
    strncpy(key_copy, key, strlen(key) + 1);
    strncpy(value_copy, value, strlen(value) + 1);


    array_table[array_idx].key = key_copy;
    array_table[array_idx].value = value_copy;
    array_idx++;
    return 0;
}

char *kvstore_array_get(char *key) {
    if (key == NULL) {
        return NULL;
    }

    for (int i = 0; i < array_idx; i++) {
        if (strcmp(array_table[i].key, key) == 0) {
            return array_table[i].value;
        }
    }

    return NULL; // Key not found
}

int kvstore_array_del(char *key) {
    if (key == NULL) {
        return -1;
    }

    for (int i = 0; i < array_idx; i++) {
        //strcmp函数是用来比较两个字符串的, 如果两个字符串相等, strcmp函数返回0, 所以这里是判断array_table[i].key和key是否相等
        if (strcmp(array_table[i].key, key) == 0) {
            kvstore_free(array_table[i].key);
            kvstore_free(array_table[i].value);

            // Shift remaining items to fill the gap
            for (int j = i; j < array_idx - 1; j++) {
                array_table[j] = array_table[j + 1];
            }
            //这里是把数组中后面的元素往前移动一位,
            //来覆盖掉被删除的元素, 这样就保持了数组的连续性, 同时也避免了留下一个空洞在数组中。
            array_idx--;
            array_table[array_idx].key = NULL;
            array_table[array_idx].value = NULL;
            return 0; // Key deleted successfully
        }
    }

    return 1; // Key not found
}

int kvstore_array_mod(char *key, char *value) {
    if (key == NULL || value == NULL) {
        return -1;
    }
    int i = 0;
    for (i = 0; i < array_idx; i++) {
        //strcmp函数是用来比较两个字符串的, 如果两个字符串相等, strcmp函数返回0,
        //所以这里是判断array_table[i].key和key是否相等
        //如果找到了要修改的键, 就先释放掉原来的值的内存, 然后为新的值分配内存并复制内容,
        //最后更新数组中的值指针。
        if (strcmp(array_table[i].key, key) == 0) {
            kvstore_free(array_table[i].value);

            char *value_copy = kvstore_malloc(strlen(value) + 1);
            if (value_copy == NULL) {
                return -1; // Memory allocation failed
            }
            strncpy(value_copy, value, strlen(value) + 1);

            
            array_table[i].value = value_copy;
            return 0; // Key modified successfully
        }
    }

    return 1; // Key not found
}
