/*
 * KRB5 - Enctype 23 (arcfour-hmac) cracker patch for JtR
 * Created on August of 2012 by Mougey Camille (CEA/DAM)
 *
 * This format is one of formats saved in KDC database and used during the authentication part
 *
 * This software is Copyright (c) 2012, Mougey Camille (CEA/DAM)
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted.
 *
 * Input Format :
 * - user:$krb23$hash
 * - user:hash
 */

#if AC_BUILT
/* need to know if HAVE_KRB5 is set, for autoconfig build */
#include "autoconfig.h"
#endif

#if HAVE_KRB5

#if FMT_EXTERNS_H
extern struct fmt_main fmt_KRB5_kinit;
#elif FMT_REGISTERS_H
john_register_one(&fmt_KRB5_kinit);
#else

#include <string.h>
#include <assert.h>
#include <errno.h>
#include "arch.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#include <krb5.h>
#ifdef _OPENMP
#include <omp.h>
#define OMP_SCALE               64
#endif
#include "memdbg.h"

#define FORMAT_LABEL		"krb5-23"
#define FORMAT_NAME		"Kerberos 5 db etype 23 rc4-hmac"

#define FORMAT_TAG		"$krb23$"
#define TAG_LENGTH		7

#if !defined(USE_GCC_ASM_IA32) && defined(USE_GCC_ASM_X64)
#define ALGORITHM_NAME		"64/64"
#else
#define ALGORITHM_NAME		"32/" ARCH_BITS_STR
#endif

#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	-1
#define PLAINTEXT_LENGTH	32
#define CIPHERTEXT_LENGTH	32
#define BINARY_SIZE		16
#define BINARY_ALIGN		4
#define SALT_SIZE		0
#define SALT_ALIGN		1
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	1

#if !AC_BUILT && defined(__APPLE__) && defined(__MACH__)
#ifdef __MAC_OS_X_VERSION_MIN_REQUIRED
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
#define HAVE_MKSHIM
#endif
#endif
#endif

/* Does some system not declare this in krb5.h? */
extern krb5_error_code KRB5_CALLCONV
krb5_c_string_to_key_with_params(krb5_context context, krb5_enctype enctype,
                                 const krb5_data *string,
                                 const krb5_data *salt,
                                 const krb5_data *params, krb5_keyblock *key);

static struct fmt_tests kinit_tests[] = {
  {"1667b5ee168fc31fba85ffb8f925fb70", "aqzsedrf"},
  {"8846f7eaee8fb117ad06bdd830b7586c", "password"},
  {"32ed87bdb5fdc5e9cba88547376818d4", "123456"},
  {FORMAT_TAG "1667b5ee168fc31fba85ffb8f925fb70", "aqzsedrf"},
  {NULL}
};

static char (*saved_key)[PLAINTEXT_LENGTH + 1];
static ARCH_WORD_32 (*crypt_out)[8];

static krb5_data salt;
static krb5_enctype enctype;

static void init(struct fmt_main *pFmt)
{
#ifdef _OPENMP
	if (krb5_is_thread_safe()) {
		int omp_t = omp_get_max_threads();
		pFmt->params.min_keys_per_crypt *= omp_t;
		omp_t *= OMP_SCALE;
		pFmt->params.max_keys_per_crypt *= omp_t;
	} else
		omp_set_num_threads(1);
#endif
	salt.data = "";
	salt.length = 0;
	enctype = 23; /* arcfour-hmac */

	saved_key = mem_calloc_tiny(sizeof(*saved_key) *
			pFmt->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	crypt_out = mem_calloc_tiny(sizeof(*crypt_out) *
			pFmt->params.max_keys_per_crypt, MEM_ALIGN_WORD);
}

static int valid(char *ciphertext, struct fmt_main *pFmt)
{
	char *p, *q;

	p = ciphertext;

	if (!strncmp(p, FORMAT_TAG, TAG_LENGTH))
		p += TAG_LENGTH;

	q = p;
	while (atoi16[ARCH_INDEX(*q)] != 0x7F) {
	        if (*q >= 'A' && *q <= 'F') /* support lowercase only */
			return 0;
		q++;
	}

	return !*q && q - p == CIPHERTEXT_LENGTH;
}


static char *split(char *ciphertext, int index, struct fmt_main *pFmt)
{
	static char out[TAG_LENGTH + CIPHERTEXT_LENGTH + 1];

	if (!strncmp(ciphertext, FORMAT_TAG, TAG_LENGTH))
		return ciphertext;

	memcpy(out, FORMAT_TAG, TAG_LENGTH);
	memcpy(out + TAG_LENGTH, ciphertext, CIPHERTEXT_LENGTH + 1);
	return out;
}

static void *get_binary(char *ciphertext)
{
	static unsigned char *out;
	char *p;
	int i;

	if (!out) out = mem_alloc_tiny(BINARY_SIZE, MEM_ALIGN_WORD);

	p = ciphertext;

	if (!strncmp(ciphertext, FORMAT_TAG, TAG_LENGTH))
		p += TAG_LENGTH;

	for (i = 0; i < BINARY_SIZE; i++) {
	        out[i] =
		        (atoi16[ARCH_INDEX(*p)] << 4) |
		        atoi16[ARCH_INDEX(p[1])];
		p += 2;
	}
	return out;
}

static int crypt_all(int *pcount, struct db_salt *_salt)
{
	int count = *pcount;
	int index = 0;

#ifdef _OPENMP
#pragma omp parallel for
#endif
#if defined(_OPENMP) || MAX_KEYS_PER_CRYPT > 1
	for (index = 0; index < count; index++)
#endif
	{
		int i = 0;
		krb5_data string;
		krb5_keyblock key;

		memset(&key, 0, sizeof(krb5_keyblock));

		string.data = saved_key[index];
		string.length = strlen(saved_key[index]);
#ifdef HAVE_MKSHIM
		krb5_c_string_to_key(NULL, ENCTYPE_ARCFOUR_HMAC, &string,
		                     &salt, &key);
#else
		krb5_c_string_to_key_with_params(NULL, enctype, &string,
		                                 &salt, NULL, &key);
#endif
		for(i = 0; i < key.length / 4; i++) {
			crypt_out[index][i] = (key.contents[4 * i]) |
				(key.contents[4 * i + 1] << 8) |
				(key.contents[4 * i + 2] << 16) |
				(key.contents[4 * i + 3] << 24);
		}
#ifndef HAVE_MKSHIM  // MKShim does this automatically
		krb5_free_keyblock_contents(NULL, &key);
#endif
	}
	return count;
}

static int cmp_all(void *binary, int count)
{
	int index = 0;

#if defined(_OPENMP) || MAX_KEYS_PER_CRYPT > 1
	for (; index < count; index++)
#endif
		if (crypt_out[index][0] == *(ARCH_WORD_32*)binary)
			return 1;

	return 0;
}

static int cmp_one(void *binary, int index)
{
	return !memcmp(binary, crypt_out[index], BINARY_SIZE);
}

static int cmp_exact(char *source, int index)
{
	return 1;
}

static void set_key(char *key, int index)
{
	int saved_key_length = strlen(key);
	if (saved_key_length > PLAINTEXT_LENGTH)
		saved_key_length = PLAINTEXT_LENGTH;
	memcpy(saved_key[index], key, saved_key_length);
	saved_key[index][saved_key_length] = 0;
}

static char *get_key(int index)
{
	return saved_key[index];
}

static int get_hash_0(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0xf; }
static int get_hash_1(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0xff; }
static int get_hash_2(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0xfff; }
static int get_hash_3(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0xffff; }
static int get_hash_4(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0xfffff; }
static int get_hash_5(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0xffffff; }
static int get_hash_6(int index) { return *((ARCH_WORD_32*)&crypt_out[index]) & 0x7ffffff; }

struct fmt_main fmt_KRB5_kinit = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		BINARY_ALIGN,
		SALT_SIZE,
		SALT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_OMP,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		kinit_tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		valid,
		split,
		get_binary,
		fmt_default_salt,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		fmt_default_source,
		{
			fmt_default_binary_hash_0,
			fmt_default_binary_hash_1,
			fmt_default_binary_hash_2,
			fmt_default_binary_hash_3,
			fmt_default_binary_hash_4,
			fmt_default_binary_hash_5,
			fmt_default_binary_hash_6
		},
		fmt_default_salt_hash,
		fmt_default_set_salt,
		set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
		{
			get_hash_0,
			get_hash_1,
			get_hash_2,
			get_hash_3,
			get_hash_4,
			get_hash_5,
			get_hash_6
		},
		cmp_all,
		cmp_one,
		cmp_exact,
	}
};
#endif /* plugin stanza */

#endif /* HAVE_KRB5 */
