/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group, (c) 2008-2015 Dmitry Zenovich |
  | "runkit7" patches (c) 2015-2016 Tyson Andre                          |
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
  | Modified for php7 by Tyson Andre <tysonandre775@hotmail.com>         |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php_runkit.h"

#ifdef PHP_RUNKIT_MANIPULATION
// Copied from zend_compile.c, RC2
/* {{{ php_runkit_fetch_const
 */
static int php_runkit_fetch_const(zend_string *cname_zs, zend_constant **constant, char **found_cname TSRMLS_DC)
{
	char* cname = ZSTR_VAL(cname_zs);
	int cname_len = ZSTR_LEN(cname_zs);
	char *lcase = NULL;
	char *old_cname = cname;
	int constant_name_len = cname_len;

	char *separator;

	if (cname_len > 0 && cname[0] == '\\') {
		++cname;
		--cname_len;
	}

	separator = (char *) zend_memrchr(cname, '\\', cname_len);
	if (separator) {
		int nsname_len = separator - cname;
		char *constant_name = separator + 1;
		constant_name_len = cname_len - nsname_len - 1;

		lcase = emalloc(nsname_len + 1 + constant_name_len + 1);
		memcpy(lcase, cname, nsname_len + 1);
		memcpy(lcase + nsname_len + 1, constant_name, constant_name_len + 1);
		zend_str_tolower(lcase, nsname_len);
		cname = lcase;
	}
	// TODO: Figure out how to extract from zend_constants
	if ((*constant = zend_hash_str_find_ptr(EG(zend_constants), cname, cname_len)) == NULL) {
		if (!lcase) {
			lcase = estrndup(cname, cname_len);
			zend_str_tolower(lcase, cname_len);
		} else {
			zend_str_tolower(lcase + cname_len - constant_name_len, constant_name_len);
		}
		cname = lcase;
		if ((*constant = zend_hash_str_find_ptr(EG(zend_constants), cname, cname_len)) == NULL || ((*constant)->flags & CONST_CS)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Constant %s not found", old_cname);
			efree(lcase);
			return FAILURE;
		}
	}
	if (found_cname) {
		*found_cname = lcase ? lcase : estrndup(cname, cname_len);
	}

	return SUCCESS;
}
/* }}} */

#if PHP_VERSION_ID >= 70100
/* {{{ php_runkit_class_constant_ctor
Creates a class constant for a given class entry */
static zend_class_constant *php_runkit_class_constant_ctor(zval *value, zend_class_entry *ce, int access_type, zend_string *doc_comment) {
	zend_class_constant *c;
	// TODO add tests of modifying internal classes
	if (ce->type == ZEND_INTERNAL_CLASS) {
		// TODO: Internal classes can't have non-scalars(arrays, ASTs, etc). as constants.
		c = pemalloc(sizeof(zend_class_constant), 1);
	} else {
		c = zend_arena_alloc(&CG(arena), sizeof(zend_class_constant));
	}

	ZVAL_COPY_VALUE(&c->value, value);
	Z_ACCESS_FLAGS(c->value) = access_type;
	c->doc_comment = NULL;
	c->ce = ce;
	return c;
}
/* }}} */
#endif

static int php_runkit_class_constant_add_raw(zval *value, zend_class_entry *ce, zend_string *constname, int access_type, zend_string *doc_comment) {
#if PHP_VERSION_ID >= 70100
	zend_class_constant *c;
#else
	(void)access_type;  // suppress unused warning
#endif
	if (zend_hash_exists(&ce->constants_table, constname)) {
		return FAILURE;
	}

#if PHP_VERSION_ID >= 70100
	c = php_runkit_class_constant_ctor(value, ce, access_type, doc_comment);
	Z_TRY_ADDREF_P(value);
	if (!zend_hash_add_ptr(&ce->constants_table, constname, c)) {
		Z_TRY_DELREF_P(value);
		// TODO: delete
		return FAILURE;
	}
#else
	Z_TRY_ADDREF_P(value);
	if (!zend_hash_add(&ce->constants_table, constname, value)) {
		Z_TRY_DELREF_P(value);
		return FAILURE;
	}
#endif
	return SUCCESS;
}
/* {{{ php_runkit_update_children_consts_foreach
	Scans each element of the hash table */
void php_runkit_update_children_consts_foreach(HashTable *ht, zend_class_entry *parent_class, zval *c, zend_string *cname, int access_type)
{
	zend_class_entry *ce;
	ZEND_HASH_FOREACH_PTR(ht, ce) {
		php_runkit_update_children_consts(ce, parent_class, c, cname, access_type);
	} ZEND_HASH_FOREACH_END();
}
/* }}} */

/* {{{ php_runkit_update_children_consts
	Scan the class_table for children of the class just updated */
void php_runkit_update_children_consts(zend_class_entry *ce, zend_class_entry *parent_class, zval *value, zend_string *cname, int access_type)
{
	if (ce->parent != parent_class) {
		/* Not a child, ignore */
		return;
	}

	/* Process children of this child */
	php_runkit_update_children_consts_foreach(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), ce, value, cname, access_type);

	// TODO: Garbage collecting?
	zend_hash_del(&ce->constants_table, cname);
	if (php_runkit_class_constant_add_raw(value, ce, cname, access_type, NULL) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error updating child class");
		return;
	}
}
/* }}} */

/* {{{ php_runkit_class_constant_remove */
static int php_runkit_class_constant_remove(zend_string *classname, zend_string *constname) {
	zend_class_entry *ce;

	if ((ce = php_runkit_fetch_class(classname)) == NULL) {
		return FAILURE;
	}

	if (!zend_hash_exists(&ce->constants_table, constname)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Constant %s::%s does not exist", ZSTR_VAL(classname), ZSTR_VAL(constname));
		return FAILURE;
	}
	if (zend_hash_del(&ce->constants_table, constname)==FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove constant %s::%s", ZSTR_VAL(classname), ZSTR_VAL(constname));
		return FAILURE;
	}
	php_runkit_clear_all_functions_runtime_cache(TSRMLS_C);
	return SUCCESS;
}
/* }}}*/

/* {{{ php_runkit_global_constant_remove */
static int php_runkit_global_constant_remove(zend_string *constname TSRMLS_DC) {
	zend_constant *constant;
	char *found_constname;

	if (php_runkit_fetch_const(constname, &constant, &found_constname TSRMLS_CC) == FAILURE) {
		return FAILURE;
	}

	if (constant->module_number != PHP_USER_CONSTANT) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Only user defined constants may be removed.");
		return FAILURE;
	}

	if (zend_hash_str_del(EG(zend_constants), found_constname, ZSTR_LEN(constant->name)) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove constant");
		efree(found_constname);
		return FAILURE;
	}
	efree(found_constname);

	php_runkit_clear_all_functions_runtime_cache(TSRMLS_C);

	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_constant_remove
 */
static int php_runkit_constant_remove(zend_string* classname, zend_string* constname TSRMLS_DC)
{
	if (classname && ZSTR_LEN(classname) > 0) {
		return php_runkit_class_constant_remove(classname, constname);
	}
	return php_runkit_global_constant_remove(constname);
}
/* }}} */

/* {{{ php_runkit_has_constant_type */
static zend_bool php_runkit_has_constant_type(const zval *value) {
	switch (Z_TYPE_P(value)) {
		case IS_LONG:
		case IS_DOUBLE:
		case IS_STRING:
		case IS_TRUE:
		case IS_FALSE:
		case IS_RESOURCE:
		case IS_NULL:
			return 1;
		default:
			return 0;
	}
}
/* }}} */

/* {{{ php_runkit_global_constant_add */
static int php_runkit_global_constant_add(zend_string *constname, zval *value TSRMLS_DC) {
	zend_constant c;

	// TODO: Memory management properly
	if (ZSTR_LEN(constname) > 0 && ZSTR_VAL(constname)[0] == '\\') {
		constname = zend_string_init(ZSTR_VAL(constname) + 1, ZSTR_LEN(constname) - 1, 0);
	} else {
		zend_string_addref(constname);
	}

	/* Traditional global constant */
	c.value = *value;
	zval_copy_ctor(&c.value);
	c.flags = CONST_CS;
	c.name = constname;
	c.module_number = PHP_USER_CONSTANT;
	return zend_register_constant(&c TSRMLS_CC);
}
/* }}} */

/* {{{ php_runkit_class_constant_add */
static int php_runkit_class_constant_add(zend_string *classname, zend_string *constname, zval *value TSRMLS_DC) {
	int access_type = ZEND_ACC_PUBLIC;
	zend_class_entry *ce;
	if ((ce = php_runkit_fetch_class(classname)) == NULL) {
		return FAILURE;
	}

	// Mirror checks in zend_declare_class_constant_ex
	if (ce->ce_flags & ZEND_ACC_INTERFACE) {
		if (access_type != ZEND_ACC_PUBLIC) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Access type for new interface constant %s::%s must be public", ZSTR_VAL(ce->name), ZSTR_VAL(constname));
			return FAILURE;
		}
	}

	if (zend_string_equals_literal_ci(constname, "class")) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "A new class constant must not be called 'class'; it is reserved for class name fetching");
		return FAILURE;
	}

	// TODO: Is this too many references?
	// TODO: Could something similar to zend_update_class_constants(zend_class_entry *class_type)
	if (php_runkit_class_constant_add_raw(value, ce, constname, access_type, NULL) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add constant %s::%s to class definition", ZSTR_VAL(ce->name), ZSTR_VAL(constname));
		return FAILURE;
	}

	// TODO: Check access_type of private, don't update children if this is private.
	php_runkit_update_children_consts_foreach(RUNKIT_53_TSRMLS_PARAM(EG(class_table)), ce, value, constname, access_type);

	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_constant_add
       Adds a global or class constant value. This is a global constant if classname is empty or null. */
static int php_runkit_constant_add(zend_string* classname, zend_string* constname, zval *value TSRMLS_DC)
{
	if (!php_runkit_has_constant_type(value)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Constants may only evaluate to scalar values");
		return FAILURE;
	}

	if ((classname == NULL) || (ZSTR_LEN(classname) == 0)) {
		return php_runkit_global_constant_add(constname, value TSRMLS_CC);
	}

	return php_runkit_class_constant_add(classname, constname, value);
}
/* }}} */

/* *****************
   * Constants API *
   ***************** */

/* {{{ proto bool runkit_constant_redefine(string constname, mixed newvalue)
 */
PHP_FUNCTION(runkit_constant_redefine)
{
	zend_string *classname, *constname;
	zval *value;
	ZEND_RESULT_CODE result;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Sz", &constname, &value) == FAILURE) {
		RETURN_FALSE;
	}

	PHP_RUNKIT_SPLIT_PN(classname, constname);

	// Remove the constant; add a new constant.
	php_runkit_constant_remove(classname, constname TSRMLS_CC);
	result = php_runkit_constant_add(classname, constname, value TSRMLS_CC) == SUCCESS;
	PHP_RUNKIT_SPLIT_PN_CLEANUP(classname, constname);
	RETURN_BOOL(result);
}
/* }}} */

/* {{{ proto bool runkit_constant_remove(string constname)
 */
PHP_FUNCTION(runkit_constant_remove)
{
	zend_string *classname, *constname;
	ZEND_RESULT_CODE result;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &constname) == FAILURE) {
		RETURN_FALSE;
	}

	PHP_RUNKIT_SPLIT_PN(classname, constname);

	result = php_runkit_constant_remove(classname, constname TSRMLS_CC) == SUCCESS;
	PHP_RUNKIT_SPLIT_PN_CLEANUP(classname, constname)
	RETURN_BOOL(result);
}
/* }}} */

/* {{{ proto bool runkit_constant_add(string constname, mixed value)
	Similar to define(), but allows defining in class definitions as well
 */
PHP_FUNCTION(runkit_constant_add)
{
	zend_string *classname, *constname;
	zval *value;
	ZEND_RESULT_CODE result;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Sz", &constname, &value) == FAILURE) {
		RETURN_FALSE;
	}

	PHP_RUNKIT_SPLIT_PN(classname, constname);
	result = php_runkit_constant_add(classname, constname, value TSRMLS_CC) == SUCCESS;
	PHP_RUNKIT_SPLIT_PN_CLEANUP(classname, constname);
	RETURN_BOOL(result);
}
/* }}} */
#endif /* PHP_RUNKIT_MANIPULATION */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

