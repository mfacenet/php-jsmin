/*
 * Copyright (c) 2002-2009, Igor Brezac <igor@ypass.net>
 * http://www.ypass.net/
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * - Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* jsmin.c - www.crockford.com/javascript/jsmin.c
   2008-08-03

Copyright (c) 2002 Douglas Crockford  (www.crockford.com)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/php_smart_str.h"
#include "php_jsmin.h"

/* {{{ jsmin_functions[]
 *
 * Every user visible function must have an entry in jsmin_functions[].
 */
zend_function_entry jsmin_functions[] = {
	PHP_FE(jsmin,	NULL)		/* For testing, remove later. */
	{NULL, NULL, NULL}	/* Must be the last line in jsmin_functions[] */
};
/* }}} */

/* {{{ jsmin_module_entry
 */
zend_module_entry jsmin_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"jsmin",
	jsmin_functions,
	NULL,
	NULL,
	NULL,		/* Replace with NULL if there's nothing to do at request start */
	NULL,    /*PHP_RSHUTDOWN(jsmin),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(jsmin),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_JSMIN_VERSION, /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_JSMIN
ZEND_GET_MODULE(jsmin)
#endif

typedef struct {
        int theA;
        int theB;
        int theLookahead;
        int error;
        unsigned char *data;
        smart_str *buf;
} jsmin_ctx;

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
PHP_RSHUTDOWN_FUNCTION(jsmin)
{
	return SUCCESS;
}
 */
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(jsmin)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "jsmin support", "enabled");
	php_info_print_table_row(2, "jsmin version", PHP_JSMIN_VERSION);
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


/* isAlphanum -- return true if the character is a letter, digit, underscore,
        dollar sign, or non-ASCII character.
*/

static int
jsmin_isAlphanum(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c == '\\' ||
        c > 126);
}


/* get -- return the next character from stdin. Watch out for lookahead. If
        the character is a control character, translate it to a space or
        linefeed.
*/

static int
jsmin_get(jsmin_ctx *ctx)
{
    int c = ctx->theLookahead;
    ctx->theLookahead = 0;
    if (c == 0) {
        c = *ctx->data;
	++(ctx->data);
    }
    if (c >= ' ' || c == '\n' || c == 0) {
        return c;
    }
    if (c == '\r') {
        return '\n';
    }
    return ' ';
}


/* peek -- get the next character without getting it.
*/

static int
jsmin_peek(jsmin_ctx *ctx)
{
    ctx->theLookahead = jsmin_get(ctx);
    return ctx->theLookahead;
}


/* next -- get the next character, excluding comments. peek() is used to see
        if a '/' is followed by a '/' or '*'.
*/

static int
jsmin_next(jsmin_ctx *ctx)
{
    int c = jsmin_get(ctx);
    if  (c == '/') {
        switch (jsmin_peek(ctx)) {
        case '/':
            for (;;) {
                c = jsmin_get(ctx);
                if (c <= '\n') {
                    return c;
                }
            }
        case '*':
            jsmin_get(ctx);
            for (;;) {
                switch (jsmin_get(ctx)) {
                case '*':
                    if (jsmin_peek(ctx) == '/') {
                        jsmin_get(ctx);
                        return ' ';
                    }
                    break;
                case 0:
		    ctx->error = 1;
                    zend_error(E_WARNING, "[jsmin] unterminated comment, not minified.");
		    return;
                }
            }
        default:
            return c;
        }
    }
    return c;
}


/* action -- do something! What you do is determined by the argument:
        1   Output A. Copy B to A. Get the next B.
        2   Copy B to A. Get the next B. (Delete A).
        3   Get the next B. (Delete B).
   action treats a string as a single character. Wow!
   action recognizes a regular expression if it is preceded by ( or , or =.
*/

static void
jsmin_action(int d, jsmin_ctx *ctx)
{
    switch (d) {
    case 1:
        smart_str_appendl(ctx->buf, &ctx->theA, 1);
    case 2:
        ctx->theA = ctx->theB;
        if (ctx->theA == '\'' || ctx->theA == '"') {
            for (;;) {
		smart_str_appendl(ctx->buf, &ctx->theA, 1);
                ctx->theA = jsmin_get(ctx);
                if (ctx->theA == ctx->theB) {
                    break;
                }
                if (ctx->theA == '\\') {
		    smart_str_appendl(ctx->buf, &ctx->theA, 1);
                    ctx->theA = jsmin_get(ctx);
                }
                if (ctx->theA == 0) {
		    ctx->error = 1;
                    zend_error(E_WARNING, "[jsmin] unterminated string literal, not minified.");
		    return;
                }
            }
        }
    case 3:
        ctx->theB = jsmin_next(ctx);
        if (ctx->theB == '/' && (ctx->theA == '(' || ctx->theA == ',' || ctx->theA == '=' ||
                            ctx->theA == ':' || ctx->theA == '[' || ctx->theA == '!' ||
                            ctx->theA == '&' || ctx->theA == '|' || ctx->theA == '?' ||
                            ctx->theA == '{' || ctx->theA == '}' || ctx->theA == ';' ||
                            ctx->theA == '\n')) {
	    smart_str_appendl(ctx->buf, &ctx->theA, 1);
	    smart_str_appendl(ctx->buf, &ctx->theB, 1);
            for (;;) {
                ctx->theA = jsmin_get(ctx);
                if (ctx->theA == '/') {
                    break;
                }
                if (ctx->theA =='\\') {
		    smart_str_appendl(ctx->buf, &ctx->theA, 1);
                    ctx->theA = jsmin_get(ctx);
                }
                if (ctx->theA == 0) {
		    ctx->error = 1;
                    zend_error(E_WARNING, "[jsmin] unterminated Regular Expression literal, not minified.");
                    return;
                }
		smart_str_appendl(ctx->buf, &ctx->theA, 1);
            }
            ctx->theB = jsmin_next(ctx);
        }
    }
}


/* jsmin -- Copy the input to the output, deleting the characters which are
        insignificant to JavaScript. Comments will be removed. Tabs will be
        replaced with spaces. Carriage returns will be replaced with linefeeds.
        Most spaces and linefeeds will be removed.
*/

static void
jsmin(jsmin_ctx *ctx)
{
    ctx->theA = '\n';
    jsmin_action(3, ctx);
    while (ctx->theA != 0) {
	if (ctx->error) return;
        switch (ctx->theA) {
        case ' ':
            if (jsmin_isAlphanum(ctx->theB)) {
                jsmin_action(1, ctx);
            } else {
                jsmin_action(2, ctx);
            }
            break;
        case '\n':
            switch (ctx->theB) {
            case '{':
            case '[':
            case '(':
            case '+':
            case '-':
                jsmin_action(1, ctx);
                break;
            case ' ':
                jsmin_action(3, ctx);
                break;
            default:
                if (jsmin_isAlphanum(ctx->theB)) {
                    jsmin_action(1, ctx);
                } else {
                    jsmin_action(2, ctx);
                }
            }
            break;
        default:
            switch (ctx->theB) {
            case ' ':
                if (jsmin_isAlphanum(ctx->theA)) {
                    jsmin_action(1, ctx);
                    break;
                }
                jsmin_action(3, ctx);
                break;
            case '\n':
                switch (ctx->theA) {
                case '}':
                case ']':
                case ')':
                case '+':
                case '-':
                case '"':
                case '\'':
                    jsmin_action(1, ctx);
                    break;
                default:
                    if (jsmin_isAlphanum(ctx->theA)) {
                        jsmin_action(1, ctx);
                    } else {
                        jsmin_action(3, ctx);
                    }
                }
                break;
            default:
                jsmin_action(1, ctx);
                break;
            }
        }
    }
}

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_jsmin_compiled(string arg)
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(jsmin)
{
	unsigned char *arg = NULL;
	int arg_len;
	jsmin_ctx *ctx;
	smart_str buf = {0};

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &arg, &arg_len) == FAILURE) {
		return;
	}

	ctx = emalloc(sizeof(jsmin_ctx));
	memset(ctx, 0, sizeof(jsmin_ctx));

	ctx->data = arg;
	ctx->buf = &buf;
	ctx->error = 0;
	ctx->theLookahead = 0;

	jsmin(ctx);

	ZVAL_STRINGL(return_value, ctx->buf->c, ctx->buf->len, 1);
	// RETURN_STRINGL(buf.c, buf.len, 1);

	smart_str_free(ctx->buf);
	efree(ctx);
}


/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and 
   unfold functions in source code. See the corresponding marks just before 
   function definition, where the functions purpose is also documented. Please 
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
