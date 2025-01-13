#include <stdint.h>
#include <stddef.h>
#include "utils.h"

int abs(int n) {
    return n < 0 ? -n : n;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (int i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }

    return dst;
}

size_t strlen(const char *s) {
    size_t i;
    for (i = 0; *s != '\0'; i++, s++) {}
    return i;
}

size_t strnlen(const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n && *s != '\0'; i++, s++) {}
    return i;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    size_t i;
    for (i = 0; i < n && *s1 && (*s1 == *s2); i++, s1++, s2++) {}
    return (i != n) ? (*s1 - *s2) : 0;
}

void *memset(void *dst, int c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        *(uint8_t *)dst++ = (uint8_t)c;
    }
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    size_t i;
    uint8_t *p1 = (uint8_t *)s1;
    uint8_t *p2 = (uint8_t *)s2;
    for (i = 0; i < n; i++, p1++, p2++) {
        if (*p1 != *p2) {
            break;
        }
    }
    if (i == n) {
        return 0;
    }
    else {
        return *p1 - *p2;
    }
}

void *memmove(void *dst, const void *src, size_t n) {
    if (src + n > dst) {
        src += n - 1;
        dst += n - 1;
        for (size_t i = 0; i < n; i++) {
            *(uint8_t *)dst-- = *(uint8_t *)src--;
        }
    } else {
        memcpy(dst, src, n);
    }
    return dst;
}

void *memchr(const void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++, p++) {
        if (*p == c) {
            return p;
        }
    }
    return NULL;
}

char *strchr(const char *s, int c) {
    char *p = (char *)s;
    while (*p != '\0' && *p != c) {
        p++;
    }
    if (*p == '\0') {
        return NULL;
    }
    else {
        return p;
    }
}

char *strcpy(char *dst, const char *src) {
    do {
        *dst++ = *src;
    } while (*src++ != '\0');
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    size_t destlen = strlen(dst);
    size_t i;
    for (i = 0; i < n; i++) {
        dst[destlen + i] = src[i];
    }
    dst[destlen + i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    size_t destlen = strlen(dst);
    strcpy(dst + destlen, src);
    return dst;
}

int isspace(int c) {
    return (c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' ||
            c == '\v');
}

int toupper(int c) {
    if ('a' <= c && c <= 'z') {
        return c - ('a' - 'A');
    } else {
        return c;
    }
}

int tolower(int c) {
    if ('A' <= c && c <= 'Z') {
        return c + ('a' - 'A');
    } else {
        return c;
    }
}
