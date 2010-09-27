/*
 * test-bitfield.c
 *
 * Common Trace Format - bitfield test program
 *
 * Copyright 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <ctf/bitfield.h>
#include <stdio.h>

unsigned int glob;

/*
 * This function is only declared to show the size of a bitfield write in
 * objdump.
 */
void fct(void)
{
	ctf_bitfield_write(&glob, 12, 15, 0x12345678);
}

/* Test array size, in bytes */
#define TEST_LEN 128
#define NR_TESTS 10

unsigned int srcrand;

#if defined(__i386) || defined(__x86_64)

static inline int fls(int x)
{
	int r;
	asm("bsrl %1,%0\n\t"
	    "cmovzl %2,%0"
	    : "=&r" (r) : "rm" (x), "rm" (-1));
	return r + 1;
}

#elif defined(__PPC__)

static __inline__ int fls(unsigned int x)
{
	int lz;

	asm ("cntlzw %0,%1" : "=r" (lz) : "r" (x));
	return 32 - lz;
}

#else

static int fls(unsigned int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xFFFF0000U)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xFF000000U)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xF0000000U)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xC0000000U)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000U)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

#endif

static void print_byte_array(const unsigned char *c, unsigned long len)
{
	unsigned long i;

	for (i = 0; i < len; i++) {
		printf("0x%X", c[i]);
		if (i != len - 1)
			printf(" ");
	}
	printf("\n");
}

static void init_byte_array(unsigned char *c,
			    unsigned long len,
			    unsigned char val)
{
	unsigned long i;

	for (i = 0; i < len; i++)
		c[i] = val;
}

int run_test_unsigned(void)
{
	unsigned int src, nrbits;
	union {
		unsigned char c[TEST_LEN];
		unsigned short s[TEST_LEN/sizeof(unsigned short)];
		unsigned int i[TEST_LEN/sizeof(unsigned int)];
		unsigned long l[TEST_LEN/sizeof(unsigned long)];
		unsigned long long ll[TEST_LEN/sizeof(unsigned long long)];
	} target;
	uint64_t readval;
	unsigned int s, l;
	int err = 0;

	printf("Running unsigned test with 0x%X\n", srcrand);

	src = srcrand;
	nrbits = fls(src);

	for (s = 0; s < CHAR_BIT * TEST_LEN; s++) {
		for (l = nrbits; l < (CHAR_BIT * TEST_LEN) - s; l++) {
			init_byte_array(target.c, TEST_LEN, 0xFF);
			ctf_bitfield_write(target.c, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (bytewise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0xFF);
			ctf_bitfield_write(target.s, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (shortwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0xFF);
			ctf_bitfield_write(target.i, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (intwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0xFF);
			ctf_bitfield_write(target.l, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (longwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0xFF);
			ctf_bitfield_write(target.ll, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (longlongwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}
		}
	}
	if (!err)
		printf("Success!\n");
	else
		printf("Failed!\n");
	return err;
}

int run_test_signed(void)
{
	int src, nrbits;
	union {
		char c[TEST_LEN];
		short s[TEST_LEN/sizeof(short)];
		int i[TEST_LEN/sizeof(int)];
		long l[TEST_LEN/sizeof(long)];
		long long ll[TEST_LEN/sizeof(long long)];
	} target;
	int64_t readval;
	unsigned int s, l;
	int err = 0;

	printf("Running signed test with 0x%X\n", srcrand);

	src = srcrand;
	if (src & 0x80000000U)
		nrbits = fls(~src) + 1;	/* Find least significant bit conveying sign */
	else
		nrbits = fls(src) + 1;	/* Keep sign at 0 */

	for (s = 0; s < 8 * TEST_LEN; s++) {
		for (l = nrbits; l < (8 * TEST_LEN) - s; l++) {
			init_byte_array(target.c, TEST_LEN, 0x0);
			ctf_bitfield_write(target.c, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (bytewise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0x0);
			ctf_bitfield_write(target.s, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (shortwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0x0);
			ctf_bitfield_write(target.i, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (intwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0x0);
			ctf_bitfield_write(target.l, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (longwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}

			init_byte_array(target.c, TEST_LEN, 0x0);
			ctf_bitfield_write(target.ll, s, l, src);
			ctf_bitfield_read(target.c, s, l, &readval);
			if (readval != src) {
				printf("Error (longlongwise) src %lX read %llX shift %d len %d\n",
				       src, readval, s, l);
				print_byte_array(target.c, TEST_LEN);
				err = 1;
			}
		}
	}
	if (!err)
		printf("Success!\n");
	else
		printf("Failed!\n");
	return err;
}

int run_test(void)
{
	int err = 0;
	int i;

	srand(time(NULL));

	srcrand = 0;
	err |= run_test_unsigned();
	srcrand = 0;
	err |= run_test_signed();
	srcrand = 1;
	err |= run_test_unsigned();
	srcrand = ~0U;
	err |= run_test_unsigned();
	srcrand = -1;
	err |= run_test_signed();
	srcrand = (int)0x80000000U;
	err |= run_test_signed();

	for (i = 0; i < NR_TESTS; i++) {
		srcrand = rand();
		err |= run_test_unsigned();
		err |= run_test_signed();
	}
	return err;
}

int main(int argc, char **argv)
{
	unsigned long src;
	unsigned int shift, len;
	int ret;
	union {
		unsigned char c[8];
		unsigned short s[4];
		unsigned int i[2];
		unsigned long l[2];
		unsigned long long ll[1];
	} target;
	uint64_t readval;

	if (argc > 1)
		src = atoi(argv[1]);
	else
		src = 0x12345678;
	if (argc > 2)
		shift = atoi(argv[2]);
	else
		shift = 12;
	if (argc > 3)
		len = atoi(argv[3]);
	else
		len = 40;

	target.i[0] = 0xFFFFFFFF;
	target.i[1] = 0xFFFFFFFF;
	ctf_bitfield_write(target.c, shift, len, src);
	printf("bytewise\n");
	print_byte_array(target.c, 8);

	target.i[0] = 0xFFFFFFFF;
	target.i[1] = 0xFFFFFFFF;
	ctf_bitfield_write(target.s, shift, len, src);
	printf("shortwise\n");
	print_byte_array(target.c, 8);

	target.i[0] = 0xFFFFFFFF;
	target.i[1] = 0xFFFFFFFF;
	ctf_bitfield_write(target.i, shift, len, src);
	printf("intwise\n");
	print_byte_array(target.c, 8);

	target.i[0] = 0xFFFFFFFF;
	target.i[1] = 0xFFFFFFFF;
	ctf_bitfield_write(target.l, shift, len, src);
	printf("longwise\n");
	print_byte_array(target.c, 8);

	target.i[0] = 0xFFFFFFFF;
	target.i[1] = 0xFFFFFFFF;
	ctf_bitfield_write(target.ll, shift, len, src);
	printf("lluwise\n");
	print_byte_array(target.c, 8);

	ctf_bitfield_read(target.c, shift, len, &readval);
	printf("read: %llX\n", readval);

	ret = run_test();

	return ret;
}
