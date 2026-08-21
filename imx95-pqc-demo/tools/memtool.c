/* memtool.c - peek/poke/load physical memory via /dev/mem on the i.MX95.
 *
 * Torizon's busybox has no devmem applet, and plain dd on /dev/mem fails with
 * EFAULT, so map the page explicitly instead.
 *
 * Usage:
 *   memtool r <hex-addr> [words]     read (default 8 words)
 *   memtool w <hex-addr> <hex-val>   write one 32-bit word
 *   memtool load <hex-addr> <file>   copy a file to physical memory
 *   memtool fill <hex-addr> <len> <hex-byte>
 *
 * Copyright (C) 2026 wolfSSL Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int map_region(off_t phys, size_t len, void **base, void **ptr, int *fd)
{
    long pagesz = sysconf(_SC_PAGESIZE);
    off_t aligned = phys & ~((off_t)pagesz - 1);
    size_t offset = (size_t)(phys - aligned);
    size_t maplen = len + offset;

    *fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (*fd < 0) {
        fprintf(stderr, "open /dev/mem: %s\n", strerror(errno));
        return -1;
    }
    *base = mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, aligned);
    if (*base == MAP_FAILED) {
        fprintf(stderr, "mmap 0x%llx (len %zu): %s\n",
                (unsigned long long)aligned, maplen, strerror(errno));
        close(*fd);
        return -1;
    }
    *ptr = (void *)((char *)*base + offset);
    return (int)maplen;
}

int main(int argc, char **argv)
{
    void *base = NULL, *ptr = NULL;
    int fd = -1, maplen;
    off_t addr;

    if (argc < 3)
        goto usage;

    addr = (off_t)strtoull(argv[2], NULL, 16);

    if (strcmp(argv[1], "r") == 0) {
        unsigned n = (argc > 3) ? (unsigned)strtoul(argv[3], NULL, 0) : 8;
        unsigned i;

        maplen = map_region(addr, n * 4, &base, &ptr, &fd);
        if (maplen < 0)
            return 1;
        for (i = 0; i < n; i++) {
            if ((i % 4) == 0)
                printf("\n%08llx: ", (unsigned long long)(addr + i * 4));
            printf("%08x ", ((volatile uint32_t *)ptr)[i]);
        }
        printf("\n");
    }
    else if (strcmp(argv[1], "w") == 0) {
        uint32_t val;

        if (argc < 4)
            goto usage;
        val = (uint32_t)strtoul(argv[3], NULL, 16);
        maplen = map_region(addr, 4, &base, &ptr, &fd);
        if (maplen < 0)
            return 1;
        *(volatile uint32_t *)ptr = val;
    }
    else if (strcmp(argv[1], "fill") == 0) {
        size_t len;
        int byte;

        if (argc < 5)
            goto usage;
        len = (size_t)strtoul(argv[3], NULL, 0);
        byte = (int)strtoul(argv[4], NULL, 16);
        maplen = map_region(addr, len, &base, &ptr, &fd);
        if (maplen < 0)
            return 1;
        /* Same device-memory constraint as "load" below: libc's memset emits
         * unaligned and multi-register stores, which raise SIGBUS against the
         * /dev/mem mapping on this part. Fill in aligned 32-bit words. */
        {
            volatile uint32_t *dst = (volatile uint32_t *)ptr;
            uint32_t word;
            size_t words, i;

            word = (uint32_t)(byte & 0xff);
            word |= (word << 8);
            word |= (word << 16);
            words = (len + 3) / 4;
            for (i = 0; i < words; i++)
                dst[i] = word;
        }
    }
    else if (strcmp(argv[1], "load") == 0) {
        struct stat st;
        FILE *f;
        size_t got;

        if (argc < 4)
            goto usage;
        if (stat(argv[3], &st) != 0) {
            fprintf(stderr, "stat %s: %s\n", argv[3], strerror(errno));
            return 1;
        }
        f = fopen(argv[3], "rb");
        if (f == NULL) {
            fprintf(stderr, "open %s: %s\n", argv[3], strerror(errno));
            return 1;
        }
        maplen = map_region(addr, (size_t)st.st_size, &base, &ptr, &fd);
        if (maplen < 0) {
            fclose(f);
            return 1;
        }
        /* Do NOT fread() straight into the mapping. /dev/mem hands back a
         * device-memory mapping on this part, where the unaligned and
         * multi-register stores libc's memcpy emits raise SIGBUS. Stage the
         * file in normal memory and copy it across in aligned 32-bit words. */
        {
            unsigned char *buf = malloc((size_t)st.st_size + 4);
            volatile uint32_t *dst = (volatile uint32_t *)ptr;
            const uint32_t *src;
            size_t words, i;

            if (buf == NULL) {
                fprintf(stderr, "malloc %lld failed\n", (long long)st.st_size);
                fclose(f);
                return 1;
            }
            memset(buf, 0, (size_t)st.st_size + 4);
            got = fread(buf, 1, (size_t)st.st_size, f);
            src = (const uint32_t *)buf;
            words = ((size_t)st.st_size + 3) / 4;
            for (i = 0; i < words; i++)
                dst[i] = src[i];
            free(buf);
        }
        fclose(f);
        if (got != (size_t)st.st_size) {
            fprintf(stderr, "short read: %zu of %lld\n",
                    got, (long long)st.st_size);
            munmap(base, (size_t)maplen);
            close(fd);
            return 1;
        }
        printf("loaded %lld bytes to 0x%llx\n",
               (long long)st.st_size, (unsigned long long)addr);
    }
    else if (strcmp(argv[1], "con") == 0) {
        /* Dump the M7 shared-memory console ring buffer (see
         * wolfboot hal/uart/uart_drv_imx95_m7.c). Header is
         * magic / wr / size / rsvd, followed by the text. */
        volatile uint32_t *hdr;
        uint32_t magic, wr, bufsz, i;
        unsigned char *out;

        maplen = map_region(addr, 16, &base, &ptr, &fd);
        if (maplen < 0)
            return 1;
        hdr = (volatile uint32_t *)ptr;
        magic = hdr[0];
        wr = hdr[1];
        bufsz = hdr[2];
        munmap(base, (size_t)maplen);
        close(fd);
        base = NULL; fd = -1;

        if (magic != 0x4E4F4357UL) { /* "WCON" */
            fprintf(stderr, "no console at 0x%llx (magic 0x%08x)\n",
                    (unsigned long long)addr, magic);
            return 1;
        }
        if (bufsz == 0 || bufsz > (64U * 1024U * 1024U)) {
            fprintf(stderr, "implausible console size %u\n", bufsz);
            return 1;
        }
        if (wr == 0) {
            fprintf(stderr, "console empty\n");
            return 0;
        }
        if (wr > bufsz) {
            fprintf(stderr,
                "[console overran: %u bytes written into a %u byte buffer; "
                "showing the most recent %u]\n", wr, bufsz, bufsz);
        }

        maplen = map_region(addr + 16, bufsz, &base, &ptr, &fd);
        if (maplen < 0)
            return 1;
        /* The copy below moves whole 32-bit words, so round the allocation up
         * to the word count rather than bufsz + 1: a ring size that is not a
         * multiple of 4 would otherwise be written up to 2 bytes past the end. */
        out = malloc(((size_t)bufsz + 3u) / 4u * 4u + 1u);
        if (out == NULL) {
            fprintf(stderr, "malloc failed\n");
            munmap(base, (size_t)maplen);
            close(fd);
            return 1;
        }
        /* /dev/mem hands back device memory: copy with aligned 32-bit reads,
         * never memcpy (which raises SIGBUS with unaligned/vector loads). */
        {
            volatile uint32_t *src = (volatile uint32_t *)ptr;
            uint32_t words = (bufsz + 3) / 4;
            uint32_t *dst = (uint32_t *)out;

            for (i = 0; i < words; i++)
                dst[i] = src[i];
        }

        if (wr <= bufsz) {
            fwrite(out, 1, wr, stdout);
        }
        else {
            /* Ring wrapped: oldest byte is at wr % bufsz. */
            uint32_t start = wr % bufsz;
            fwrite(out + start, 1, bufsz - start, stdout);
            fwrite(out, 1, start, stdout);
        }
        fflush(stdout);
        free(out);
    }
    else {
        goto usage;
    }

    if (base != NULL && base != MAP_FAILED)
        munmap(base, (size_t)maplen);
    if (fd >= 0)
        close(fd);
    return 0;

usage:
    fprintf(stderr,
        "usage:\n"
        "  memtool r <hex-addr> [words]\n"
        "  memtool w <hex-addr> <hex-val>\n"
        "  memtool load <hex-addr> <file>\n"
        "  memtool fill <hex-addr> <len> <hex-byte>\n"
        "  memtool con  <hex-addr>          dump M7 shared-memory console\n");
    return 1;
}
