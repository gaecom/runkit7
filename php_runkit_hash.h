/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group, (c) 2008-2015 Dmitry Zenovich |
  | Patches (c) 2015-2016 Tyson Andre                                    |
  +----------------------------------------------------------------------+
  | This source file is subject to the new BSD license,                  |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.opensource.org/licenses/BSD-3-Clause                      |
  | If you did not receive a copy of the license and are unable to       |
  | obtain it through the world-wide-web, please send a note to          |
  | dzenovich@gmail.com so we can mail you a copy immediately.           |
  +----------------------------------------------------------------------+
  | Author: Sara Golemon <pollita@php.net>                               |
  | Modified by Dmitry Zenovich <dzenovich@gmail.com>                    |
  | Modified for php7 "runkit7" by Tyson Andre<tysonandre775@hotmail.com>|
  +----------------------------------------------------------------------+
*/

#ifndef PHP_RUNKIT_HASH_H
#define PHP_RUNKIT_HASH_H

/* {{{ php_runkit_hash_get_bucket */
// Copied from zend_hash_find_bucket of zend_hash.c and modified slightly.
inline static Bucket *php_runkit_hash_get_bucket(HashTable *ht, zend_string* key) {
	uint32_t nIndex;
	uint32_t idx;
  uint32_t h;
	Bucket *p, *arData;

  h = ZSTR_HASH(key);
	arData = ht->arData;
	nIndex = h | ht->nTableMask;
	idx = HT_HASH_EX(arData, nIndex);
	while (EXPECTED(idx != HT_INVALID_IDX)) {
		p = HT_HASH_TO_BUCKET_EX(arData, idx);
		if (EXPECTED(p->key == key)) { /* check for the same interned string */
			return p;
		} else if (EXPECTED(p->h == h) &&
		     EXPECTED(p->key) &&
		     EXPECTED(ZSTR_LEN(p->key) == ZSTR_LEN(key)) &&
		     EXPECTED(memcmp(ZSTR_VAL(p->key), ZSTR_VAL(key), ZSTR_LEN(key)) == 0)) {
			return p;
		}
		idx = Z_NEXT(p->val);
	}
	return NULL;
}
/* }}} */

/* {{{ php_runkit_hash_move_to_front */
inline static void php_runkit_hash_move_to_front(HashTable *ht, Bucket *p) {
	if (!p) return;

  // TODO: It appears that HT_HASH is the index of the next element in the chained list (negative index)
  // and Z_NEXT will point to the old value for this element.

  // TODO: Actually implement move-to-front, even if it isn't efficient (E.g. repeatedly insert)
  return;
}
/* }}} */

#endif
