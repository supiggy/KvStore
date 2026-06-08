#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "kvstore.h"

#define KVSTORE_MAX_TOKENS 128

const char *comands[] = {
    "SET",
    "GET",
    "DEL",
    "MOD"
};

enum {
    KVS_CMD_START = 0,
    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_COUNT,
};

void *kvstore_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    return ptr;
}

void kvstore_free(void *ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

//rbuffer

//wbuffer
int kvstore_spilt_token(char *msg,char **tokens){
    if(msg == NULL || tokens == NULL){
        return -1;
    }

    int idx = 0;
    //strtok函数是用来分割字符串的, 它会将输入的字符串按照指定的分隔符进行分割,
    //并返回一个指向第一个分割结果的指针。
    //每次调用strtok函数时, 它会继续从上一次分割的位置开始分割, 直到没有更多的分割结果为止。
    char *token = strtok(msg, " ");
    //这里token应该是是三个, set key value
    while(token != NULL){
        //这里是把分割出来的每个token存储到tokens数组中, 以便后续处理使用
        tokens[idx++] = token;
        //继续分割下一个token
        token = strtok(NULL," ");
    }

    return idx;
}

int kvstore_parse_protocol(struct conn_item *item,char **tokens,int count){
    if(item == NULL || tokens == NULL || count <= 0){
        return -1;
    }

    int cmd = KVS_CMD_START;
    for(cmd = KVS_CMD_START;cmd < KVS_CMD_COUNT;cmd++){
        if(strcmp(comands[cmd],tokens[0]) == 0){
            break;
        }
    }


    //这里memset应该是为了清空之前的响应数据, 因为wbuffer是复用的
    char *msg = item->wbuffer;
    char *key = tokens[1];
    char *value = tokens[2];
    //memset函数
    //是用来将一块内存区域的内容设置为指定的值, 这里是将wbuffer中的内容全部设置为0, 也就是清空wbuffer
    memset(msg,0,BUFFER_LENGTH);

    switch(cmd){
        case KVS_CMD_SET:{
            //set key value
            int res = kvstore_array_set(key,value);
            if(!res){
                //成功设置了key value,snprintf是为了将响应结果写入到wbuffer中, 这里的响应结果是"SUCCESS"或者"FAIL"
                snprintf(msg,BUFFER_LENGTH,"SUCCESS");
            }else{
                snprintf(msg,BUFFER_LENGTH,"FAIL");
            }
            break;
        }
        case KVS_CMD_GET:{
            //get key
            char *val = kvstore_array_get(key);
            if(val){
                snprintf(msg,BUFFER_LENGTH,"%s",val);
            }else{
                snprintf(msg,BUFFER_LENGTH,"NO EXIST");
            }
            break;
        }
        case KVS_CMD_DEL:{
            //del key
            int res = kvstore_array_del(key);
            if(res < 0){
                snprintf(msg,BUFFER_LENGTH,"%s","ERROR");
            }else if(res == 0){
                snprintf(msg,BUFFER_LENGTH,"%s","SUCCESS");
            }else{
                snprintf(msg,BUFFER_LENGTH,"%s","NO EXIST");
            }
            
            break;
        }
        case KVS_CMD_MOD:{
            //mod key value
            int res = kvstore_array_mod(key,value);
            if(res < 0){
                snprintf(msg,BUFFER_LENGTH,"%s","ERROR");
            }else if(res == 0){
                snprintf(msg,BUFFER_LENGTH,"%s","SUCCESS");
            }else{
                snprintf(msg,BUFFER_LENGTH,"%s","NO EXIST");
            }
            break;
        }
        default:
            assert(0);
    }

    
}


int kvstore_request(struct conn_item *item){
    printf("kvstore_request,:%s\n",item->rbuffer);

    char *msg = item->rbuffer;
    char *tokens[KVSTORE_MAX_TOKENS];

    int token_count = kvstore_spilt_token(msg,tokens);
    int idx = 0;
    for(idx = 0;idx < token_count;idx++){
        printf("token: %s\n",tokens[idx]);
        
    }
    kvstore_parse_protocol(item,tokens,token_count);

    return 0;

}

int kvstore_response(struct conn_item *item){

}

int main(){

#if ENABLE_NETWORK_SELECT == NETWORK_EPOLL
    enpoll_entry();
#elif ENABLE_NETWORK_SELECT == NETWORK_NTYCO
    ntyco_entry();
#elif ENABLE_NETWORK_SELECT == NETWORK_IO_URING
    io_uring_entry();
#endif

    return 0;
}