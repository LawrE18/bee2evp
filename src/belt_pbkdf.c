/*
*******************************************************************************
\file belt_pbkdf.c
\project bee2evp [EVP-interfaces over bee2 / engine of OpenSSL]
\brief The Belt-based PBKDF
\created 2015.01.19
\version 2019.05.23
\license This program is released under the GNU General Public License 
version 3 with the additional exemption that compiling, linking, 
and/or using OpenSSL is allowed. See Copyright Notices in bee2evp/info.h.
*******************************************************************************
*/

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <bee2/core/blob.h>
#include <bee2/crypto/belt.h>
#include "bee2evp/bee2evp.h"

/*
*******************************************************************************
�������� belt-pbkdf --- ��� ���������� ����� PBKDF2 �� ������ ���������� belt.
����������� -- � PKCS#5, � ����� � ��� 34.101.45 (�.2).

��� ����������� belt-pbkdf �������:
1	��������� �����������, ������ EVP_PBE_alg_add_type(EVP_PBE_TYPE_PRF, 
	NID_belt_hmac, -1, NID_belt_hash, 0).
2	� ctrl-�������� ���������� ����������, ���� ������� ����� ��������� �� 
	������, ���������� NID_belt_hmac � ����� �� ������� EVP_CTRL_PBE_PRF_NID.
	
����������� belt-pbkdf �������� ����� �������� � ������������� (����).
� ��� 34.101.45 ������������� ������������ �� ����� ��� 64-������� 
������������� (����) � �� ����� ��� 10000 ��������. 

� evp.h ������ ����������� ��� OpenSSL ����� ������������� � ����� ��������:
	#define PKCS5_SALT_LEN		8
	#define PKCS5_DEFAULT_ITER	2048
����� �������, ������������ �� ����� ������������� ��������������, � �� ����� 
�������� -- ���. ������������ ����� ���������, ���� ���������������� 
������� ���������� EVP_PBE_KEYGEN (������������ � evp.h), ������� �� 
������������.

� ���������, ��� ������� �� �������� ���������� ��� ������ ����� ��������� 
��������� OpenSSL. ��������� ����� (����� ����) -- ���������� ������� 
builtin_pbe, ������������ � crypto/evp/evp_pbe.c.

\remark �� ������� openssl/crypto/asn1/p5_pbev2.c, 
openssl/crypto/evp/p5_crpt2.c.

todo: ����������� ����������� (?)
*******************************************************************************
*/

const char OID_belt_pbkdf[] = "1.2.112.0.2.0.34.101.31.111";
const char SN_belt_pbkdf[] = "belt-pbkdf";
const char LN_belt_pbkdf[] = "belt-pbkdf";
#define NID_belt_pbkdf OBJ_sn2nid(SN_belt_pbkdf)

int evpBeltPBKDF_keyivgen(EVP_CIPHER_CTX* ctx, const char* pass, int passlen,
	ASN1_TYPE* param, const EVP_CIPHER* c, const EVP_MD* md, int en_de)
{
	int key_len;
	const octet* der;
	int der_len;
	PBKDF2PARAM* kdf = 0;
	octet* salt;
	int salt_len;
	long iter;
	blob_t key = 0;
	int ret = 0;
	// ���������� ����� �����
	if (!ctx || !EVP_CIPHER_CTX_cipher(ctx))
		return 0;
	key_len = EVP_CIPHER_CTX_key_length(ctx);
	if (key_len > 32)
		return 0;
	// ������������ ���������
	if(!param || param->type != V_ASN1_SEQUENCE)
		return 0;
	der = param->value.sequence->data;
	der_len = param->value.sequence->length;
	kdf = d2i_PBKDF2PARAM(0, &der, der_len);
	if(!kdf)
		return 0;
	// ��������� ���������
	if(kdf->keylength && ASN1_INTEGER_get(kdf->keylength) != (int)key_len ||
		OBJ_obj2nid(kdf->prf->algorithm) != NID_belt_hmac ||
		kdf->prf->parameter->type != V_ASN1_NULL ||
		kdf->salt->type != V_ASN1_OCTET_STRING)
		goto err;
	// ��������� �������������
	salt = kdf->salt->value.octet_string->data;
	salt_len = kdf->salt->value.octet_string->length;
	if (salt_len < 8)
	{
		salt_len = 8;
		if (!ASN1_OCTET_STRING_set(kdf->salt->value.octet_string, 0, salt_len))
			goto err;
		salt = kdf->salt->value.octet_string->data;
		if (RAND_bytes(salt, salt_len) < 0)
			goto err;
	}
	// ��������� ����� ��������
	iter = ASN1_INTEGER_get(kdf->iter);
	if (iter < 10000)
	{
		iter = 10000;
		if (!ASN1_INTEGER_set(kdf->iter, iter))
			goto err;
	}
	// ��������� ����
	key = blobCreate(32);
	if (!key)
		goto err;
	if (beltPBKDF2((octet*)key, (const octet*)pass, passlen, 
		(size_t)iter, salt, salt_len) != ERR_OK)
		goto err;
	// ������ ����
	ret = EVP_CipherInit_ex(ctx, 0, 0, (const octet*)key, 0, en_de);
err:
	blobClose(key);
	PBKDF2PARAM_free(kdf);
	return ret;
}

/*
*******************************************************************************
�����������
*******************************************************************************
*/

static int belt_pbkdf_nids[128];
static int belt_pbkdf_count;

#define BELT_PBKDF_REG(name, tmp)\
	(((tmp = NID_##name) != NID_undef) ?\
		belt_pbkdf_nids[belt_pbkdf_count++] = tmp :\
		(((tmp =\
			OBJ_create(OID_##name, SN_##name, LN_##name)) != NID_undef) ?\
			belt_pbkdf_nids[belt_pbkdf_count++] = tmp :\
			NID_undef))

/*
*******************************************************************************
����������� / ��������
*******************************************************************************
*/

int evpBeltPBKDF_bind(ENGINE* e)
{
	int tmp;
	// ���������������� ��������� � �������� nid'�
	if (BELT_PBKDF_REG(belt_pbkdf, tmp) == NID_undef)
		return 0;
	// ��� ���������
	return 1;
}

void evpBeltPBKDF_destroy()
{
}
