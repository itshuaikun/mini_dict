#ifndef MINI_DICT_LDOCE_READER_FFI_H
#define MINI_DICT_LDOCE_READER_FFI_H

#include <stddef.h>

typedef struct MiniDictLdoceReader MiniDictLdoceReader;
typedef struct MiniDictLdoceLookup MiniDictLdoceLookup;
typedef struct MiniDictLdoceAsset MiniDictLdoceAsset;

MiniDictLdoceReader *mini_dict_ldoce_reader_open(const char *mdx_path,
                                                 const char *mdd_path,
                                                 char **error_out);
void mini_dict_ldoce_reader_free(MiniDictLdoceReader *reader);

MiniDictLdoceLookup *mini_dict_ldoce_reader_lookup(MiniDictLdoceReader *reader,
                                                   const char *query);
int mini_dict_ldoce_lookup_status(const MiniDictLdoceLookup *result);
const char *mini_dict_ldoce_lookup_key(const MiniDictLdoceLookup *result);
const char *mini_dict_ldoce_lookup_html(const MiniDictLdoceLookup *result);
const char *mini_dict_ldoce_lookup_message(const MiniDictLdoceLookup *result);
void mini_dict_ldoce_lookup_free(MiniDictLdoceLookup *result);

MiniDictLdoceAsset *mini_dict_ldoce_reader_lookup_asset(MiniDictLdoceReader *reader,
                                                        const char *key);
int mini_dict_ldoce_asset_status(const MiniDictLdoceAsset *result);
const char *mini_dict_ldoce_asset_key(const MiniDictLdoceAsset *result);
const unsigned char *mini_dict_ldoce_asset_data(const MiniDictLdoceAsset *result);
size_t mini_dict_ldoce_asset_len(const MiniDictLdoceAsset *result);
const char *mini_dict_ldoce_asset_message(const MiniDictLdoceAsset *result);
void mini_dict_ldoce_asset_free(MiniDictLdoceAsset *result);

void mini_dict_ldoce_string_free(char *value);

#endif
