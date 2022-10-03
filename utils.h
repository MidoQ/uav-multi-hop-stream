#ifndef _UTILS_H
#define _UTILS_H

#include <arpa/inet.h>

/*************************************
 *          网络字节序转换函数
 *************************************/

/* Convert INT16 from host byte order to network byte order */
inline uint16_t hton16(uint16_t host16_t)
{
    return htons(host16_t);
}

/* Convert INT16 from network byte order to host byte order */
inline uint16_t ntoh16(uint16_t net16_t)
{
    return ntohs(net16_t);
}

/* Convert INT32 from host byte order to network byte order */
inline uint32_t hton32(uint32_t host32_t)
{
    return htonl(host32_t);
}

/* Convert INT32 from network byte order to host byte order */
inline uint32_t ntoh32(uint32_t net32_t)
{
    return ntohl(net32_t);
}

/* Convert INT64 from host byte order to network byte order */
inline uint64_t hton64(uint64_t host64_t)
{
    if (__BYTE_ORDER == __LITTLE_ENDIAN) {
        return (((uint64_t)htonl((uint32_t)((host64_t << 32) >> 32))) << 32) | (uint64_t)htonl((uint32_t)(host64_t >> 32));
    } else if (__BYTE_ORDER == __BIG_ENDIAN) {
        return host64_t;
    }
}

/* Convert INT64 from network byte order to host byte order */
inline uint64_t ntoh64(uint64_t net64_t)
{
    if (__BYTE_ORDER == __LITTLE_ENDIAN) {
        return (((uint64_t)ntohl((uint32_t)((net64_t << 32) >> 32))) << 32) | (uint64_t)ntohl((uint32_t)(net64_t >> 32));
    } else if (__BYTE_ORDER == __BIG_ENDIAN) {
        return net64_t;
    }
}

#endif