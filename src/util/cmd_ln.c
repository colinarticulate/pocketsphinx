/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 1999-2004 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced
 * Research Projects Agency and the National Science Foundation of the
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*
 * cmd_ln.c -- Command line argument parsing.
 *
 * **********************************************
 * CMU ARPA Speech Project
 *
 * Copyright (c) 1999 Carnegie Mellon University.
 * ALL RIGHTS RESERVED.
 * **********************************************
 *
 * HISTORY
 *
 * 10-Sep-1998 M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University
 *             Changed strcasecmp() call in cmp_name() to strcmp_nocase() call.
 *
 * 15-Jul-1997    M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University
 *         Added required arguments handling.
 *
 * 07-Dec-96    M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University
 *         Created, based on Eric's implementation.  Basically, combined several
 *        functions into one, eliminated validation, and simplified the interface.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _MSC_VER
#pragma warning (disable: 4996 4018)
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "sphinxbase/cmd_ln.h"
#include "sphinxbase/err.h"
#include "sphinxbase/ckd_alloc.h"
#include "sphinxbase/hash_table.h"
#include "sphinxbase/case.h"
#include "sphinxbase/strfuncs.h"

#include "pocketsphinx_internal.h"

static void
arg_log_r(cmd_ln_t *, arg_t const *, int32, int32);

static cmd_ln_t *
parse_options(cmd_ln_t *, const arg_t *, int32, char* [], int32);

/*
 * Find max length of name and default fields in the given defn array.
 * Return #items in defn array.
 */
static int32
arg_strlen(const arg_t * defn, int32 * namelen, int32 * deflen)
{
    int32 i, l;

    *namelen = *deflen = 0;
    for (i = 0; defn[i].name; i++) {
        l = strlen(defn[i].name);
        if (*namelen < l)
            *namelen = l;

        if (defn[i].deflt)
            l = strlen(defn[i].deflt);
        else
            l = strlen("(null)");
        /*      E_INFO("string default, %s , name %s, length %d\n",defn[i].deflt,defn[i].name,l); */
        if (*deflen < l)
            *deflen = l;
    }

    return i;
}


static int32
cmp_name(const void *a, const void *b)
{
    return (strcmp_nocase
            ((* (arg_t**) a)->name,
             (* (arg_t**) b)->name));
}

static arg_t const **
arg_sort(const arg_t * defn, int32 n)
{
    const arg_t ** pos;
    int32 i;

    pos = (arg_t const **) ckd_calloc(n, sizeof(arg_t *));
    for (i = 0; i < n; ++i)
        pos[i] = &defn[i];
    qsort(pos, n, sizeof(arg_t *), cmp_name);

    return pos;
}

static size_t
strnappend(char **dest, size_t *dest_allocation,
       const char *source, size_t n)
{
    size_t source_len, required_allocation;

    if (dest == NULL || dest_allocation == NULL)
        return -1;
    if (*dest == NULL && *dest_allocation != 0)
        return -1;
    if (source == NULL)
        return *dest_allocation;

    source_len = strlen(source);
    if (n && n < source_len)
        source_len = n;

    required_allocation = (*dest ? strlen(*dest) : 0) + source_len + 1;
    if (*dest_allocation < required_allocation) {
        if (*dest_allocation == 0) {
            *dest = (char *)ckd_calloc(required_allocation * 2, 1);
        } else {
            *dest = (char *)ckd_realloc(*dest, required_allocation * 2);
        }
        *dest_allocation = required_allocation * 2;
    }

    strncat(*dest, source, source_len);

    return *dest_allocation;
}

static size_t
strappend(char **dest, size_t *dest_allocation,
       const char *source)
{
    return strnappend(dest, dest_allocation, source, 0);
}

static char*
arg_resolve_env(const char *str)
{
    char *resolved_str = NULL;
    char env_name[100];
    const char *env_val;
    size_t alloced = 0;
    const char *i = str, *j;

    /* calculate required resolved_str size */
    do {
        j = strstr(i, "$(");
        if (j != NULL) {
            if (j != i) {
                strnappend(&resolved_str, &alloced, i, j - i);
                i = j;
            }
            j = strchr(i + 2, ')');
            if (j != NULL) {
                if (j - (i + 2) < 100) {
                    strncpy(env_name, i + 2, j - (i + 2));
                    env_name[j - (i + 2)] = '\0';
                    #if !defined(_WIN32_WCE)
                    env_val = getenv(env_name);
                    if (env_val)
                        strappend(&resolved_str, &alloced, env_val);
                    #else
                    env_val = 0;
                    #endif
                }
                i = j + 1;
            } else {
                /* unclosed, copy and skip */
                j = i + 2;
                strnappend(&resolved_str, &alloced, i, j - i);
                i = j;
            }
        } else {
            strappend(&resolved_str, &alloced, i);
        }
    } while(j != NULL);

    return resolved_str;
}

static void
arg_log_r(cmd_ln_t *cmdln, const arg_t * defn, int32 doc, int32 lineno)
{
    arg_t const **pos;
    int32 i, n;
    int32 namelen, deflen;
    cmd_ln_val_t const *vp;

    /* No definitions, do nothing. */
    if (defn == NULL)
        return;

    /* Find max lengths of name and default value fields, and #entries in defn */
    n = arg_strlen(defn, &namelen, &deflen);
    namelen += 4;
    deflen += 4;
    if (lineno)
        E_INFO("%-*s", namelen, "[NAME]");
    else
        E_INFOCONT("%-*s", namelen, "[NAME]");
    E_INFOCONT("%-*s", deflen, "[DEFLT]");
    if (doc) {
        E_INFOCONT("     [DESCR]\n");
    }
    else {
        E_INFOCONT("    [VALUE]\n");
    }

    /* Print current configuration, sorted by name */
    pos = arg_sort(defn, n);
    for (i = 0; i < n; i++) {
        if (lineno)
            E_INFO("%-*s", namelen, pos[i]->name);
        else
            E_INFOCONT("%-*s", namelen, pos[i]->name);
        if (pos[i]->deflt)
            E_INFOCONT("%-*s", deflen, pos[i]->deflt);
        else
            E_INFOCONT("%-*s", deflen, "");
        if (doc) {
            if (pos[i]->doc)
                E_INFOCONT("    %s", pos[i]->doc);
        }
        else {
            vp = cmd_ln_access_r(cmdln, pos[i]->name);
            if (vp) {
                switch (pos[i]->type) {
                case ARG_INTEGER:
                case REQARG_INTEGER:
                    E_INFOCONT("    %ld", vp->val.i);
                    break;
                case ARG_FLOATING:
                case REQARG_FLOATING:
                    E_INFOCONT("    %e", vp->val.fl);
                    break;
                case ARG_STRING:
                case REQARG_STRING:
                    if (vp->val.ptr)
                        E_INFOCONT("    %s", (char *)vp->val.ptr);
                    break;
                case ARG_BOOLEAN:
                case REQARG_BOOLEAN:
                    E_INFOCONT("    %s", vp->val.i ? "yes" : "no");
                    break;
                default:
                    E_ERROR("Unknown argument type: %d\n", pos[i]->type);
                }
            }
        }

        E_INFOCONT("\n");
    }
    ckd_free(pos);
    E_INFOCONT("\n");
}

static cmd_ln_val_t *
cmd_ln_val_init(int t, const char *name, const char *str)
{
    cmd_ln_val_t *v;
    anytype_t val;
    char *e_str;

    if (!str) {
        /* For lack of a better default value. */
        memset(&val, 0, sizeof(val));
    }
    else {
        int valid = 1;
        e_str = arg_resolve_env(str);

        switch (t) {
        case ARG_INTEGER:
        case REQARG_INTEGER:
            if (sscanf(e_str, "%ld", &val.i) != 1)
                valid = 0;
            break;
        case ARG_FLOATING:
        case REQARG_FLOATING:
            if (e_str == NULL || e_str[0] == 0)
            valid = 0;
            val.fl = atof_c(e_str);
            break;
        case ARG_BOOLEAN:
        case REQARG_BOOLEAN:
            if ((e_str[0] == 'y') || (e_str[0] == 't') ||
                (e_str[0] == 'Y') || (e_str[0] == 'T') || (e_str[0] == '1')) {
                val.i = TRUE;
            }
            else if ((e_str[0] == 'n') || (e_str[0] == 'f') ||
                     (e_str[0] == 'N') || (e_str[0] == 'F') |
                     (e_str[0] == '0')) {
                val.i = FALSE;
            }
            else {
                E_ERROR("Unparsed boolean value '%s'\n", str);
                valid = 0;
            }
            break;
        case ARG_STRING:
        case REQARG_STRING:
            val.ptr = ckd_salloc(e_str);
            break;
        default:
            E_ERROR("Unknown argument type: %d\n", t);
            valid = 0;
        }

        ckd_free(e_str);
        if (valid == 0)
            return NULL;
    }

    v = (cmd_ln_val_t *)ckd_calloc(1, sizeof(*v));
    memcpy(v, &val, sizeof(val));
    v->type = t;
    v->name = ckd_salloc(name);

    return v;
}

/*
 * Handles option parsing for cmd_ln_parse_file_r() and cmd_ln_init()
 * also takes care of storing argv.
 * DO NOT call it from cmd_ln_parse_r()
 */
static cmd_ln_t *
parse_options(cmd_ln_t *cmdln, const arg_t *defn, int32 argc, char* argv[], int32 strict)
{
    cmd_ln_t *new_cmdln;

    new_cmdln = cmd_ln_parse_r(cmdln, defn, argc, argv, strict);
    /* If this failed then clean up and return NULL. */
    if (new_cmdln == NULL) {
        int32 i;
        for (i = 0; i < argc; ++i)
            ckd_free(argv[i]);
        ckd_free(argv);
        return NULL;
    }

    /* Otherwise, we need to add the contents of f_argv to the new object. */
    if (new_cmdln == cmdln) {
        /* If we are adding to a previously passed-in cmdln, then
         * store our allocated strings in its f_argv. */
        new_cmdln->f_argv = (char **)ckd_realloc(new_cmdln->f_argv,
                                        (new_cmdln->f_argc + argc)
                                        * sizeof(*new_cmdln->f_argv));
        memcpy(new_cmdln->f_argv + new_cmdln->f_argc, argv,
               argc * sizeof(*argv));
        ckd_free(argv);
        new_cmdln->f_argc += argc;
    }
    else {
        /* Otherwise, store f_argc and f_argv. */
        new_cmdln->f_argc = argc;
        new_cmdln->f_argv = argv;
    }

    return new_cmdln;
}

void
cmd_ln_val_free(cmd_ln_val_t *val)
{
    if (val->type & ARG_STRING)
        ckd_free(val->val.ptr);
    ckd_free(val->name);
    ckd_free(val);
}


cmd_ln_t *
cmd_ln_parse_r(cmd_ln_t *inout_cmdln, const arg_t * defn,
               int32 argc, char *argv[], int strict)
{
    int32 i, j, n, argstart;
    hash_table_t *defidx = NULL;
    cmd_ln_t *cmdln;

    /* Construct command-line object */
    if (inout_cmdln == NULL) {
        cmdln = (cmd_ln_t*)ckd_calloc(1, sizeof(*cmdln));
        cmdln->refcount = 1;
    }
    else
        cmdln = inout_cmdln;
    cmdln->defn = defn;

    /* Build a hash table for argument definitions */
    defidx = hash_table_new(50, 0);
    if (defn) {
        for (n = 0; defn[n].name; n++) {
            void *v;

            v = hash_table_enter(defidx, defn[n].name, (void *)&defn[n]);
            if (strict && (v != &defn[n])) {
                E_ERROR("Duplicate argument name in definition: %s\n", defn[n].name);
                goto error;
            }
        }
    }
    else {
        /* No definitions. */
        n = 0;
    }

    /* Allocate memory for argument values */
    if (cmdln->ht == NULL)
        cmdln->ht = hash_table_new(n, 0 /* argument names are case-sensitive */ );


    /* skip argv[0] if it doesn't start with dash (starting with a
     * dash would only happen if called from parse_options()) */
    argstart = 0;
    if (argc > 0 && argv[0][0] != '-') {
        argstart = 1;
    }

    /* Parse command line arguments (name-value pairs) */
    for (j = argstart; j < argc; j += 2) {
        arg_t *argdef;
        cmd_ln_val_t *val;
        void *v;

        if (hash_table_lookup(defidx, argv[j], &v) < 0) {
            if (strict) {
                E_ERROR("Unknown argument name '%s'\n", argv[j]);
                goto error;
            }
            else if (defn == NULL)
                v = NULL;
            else
                continue;
        }
        argdef = (arg_t *)v;

        /* Enter argument value */
        if (j + 1 >= argc) {
            E_ERROR("Argument value for '%s' missing\n", argv[j]);
            goto error;
        }

        if (argdef == NULL)
            val = cmd_ln_val_init(ARG_STRING, argv[j], argv[j + 1]);
        else {
            if ((val = cmd_ln_val_init(argdef->type, argv[j], argv[j + 1])) == NULL) {
                E_ERROR("Bad argument value for %s: %s\n", argv[j],
                        argv[j + 1]);
                goto error;
            }
        }

        if ((v = hash_table_enter(cmdln->ht, val->name, (void *)val)) !=
            (void *)val)
        {
            if (strict) {
                cmd_ln_val_free(val);
                E_ERROR("Duplicate argument name in arguments: %s\n",
                        argdef->name);
                goto error;
            }
            else {
                v = hash_table_replace(cmdln->ht, val->name, (void *)val);
                cmd_ln_val_free((cmd_ln_val_t *)v);
            }
        }
    }

    /* Fill in default values, if any, for unspecified arguments */
    for (i = 0; i < n; i++) {
        cmd_ln_val_t *val;
        void *v;

        if (hash_table_lookup(cmdln->ht, defn[i].name, &v) < 0) {
            if ((val = cmd_ln_val_init(defn[i].type, defn[i].name, defn[i].deflt)) == NULL) {
                E_ERROR
                    ("Bad default argument value for %s: %s\n",
                     defn[i].name, defn[i].deflt);
                goto error;
            }
            hash_table_enter(cmdln->ht, val->name, (void *)val);
        }
    }

    /* Check for required arguments; exit if any missing */
    j = 0;
    for (i = 0; i < n; i++) {
        if (defn[i].type & ARG_REQUIRED) {
            void *v;
            if (hash_table_lookup(cmdln->ht, defn[i].name, &v) != 0)
                E_ERROR("Missing required argument %s\n", defn[i].name);
        }
    }
    if (j > 0) {
        goto error;
    }

    if (strict && argc == 1) {
        E_ERROR("No arguments given\n");
        if (defidx)
            hash_table_free(defidx);
        if (inout_cmdln == NULL)
            ps_config_free(cmdln);
        return NULL;
    }

    hash_table_free(defidx);
    return cmdln;

  error:
    if (defidx)
        hash_table_free(defidx);
    if (inout_cmdln == NULL)
        ps_config_free(cmdln);
    E_ERROR("Failed to parse arguments list\n");
    return NULL;
}

cmd_ln_t *
cmd_ln_init(cmd_ln_t *inout_cmdln, const arg_t *defn, int32 strict, ...)
{
    va_list args;
    const char *arg, *val;
    char **f_argv;
    int32 f_argc;

    va_start(args, strict);
    f_argc = 0;
    while ((arg = va_arg(args, const char *))) {
        ++f_argc;
        val = va_arg(args, const char*);
        if (val == NULL) {
            E_ERROR("Number of arguments must be even!\n");
            return NULL;
        }
        ++f_argc;
    }
    va_end(args);

    /* Now allocate f_argv */
    f_argv = (char**)ckd_calloc(f_argc, sizeof(*f_argv));
    va_start(args, strict);
    f_argc = 0;
    while ((arg = va_arg(args, const char *))) {
        f_argv[f_argc] = ckd_salloc(arg);
        ++f_argc;
        val = va_arg(args, const char*);
        f_argv[f_argc] = ckd_salloc(val);
        ++f_argc;
    }
    va_end(args);

    return parse_options(inout_cmdln, defn, f_argc, f_argv, strict);
}

cmd_ln_t *
cmd_ln_parse_file_r(cmd_ln_t *inout_cmdln, const arg_t * defn, const char *filename, int32 strict)
{
    FILE *file;
    int argc;
    int argv_size;
    char *str;
    int arg_max_length = 512;
    int len = 0;
    int quoting, ch;
    char **f_argv;
    int rv = 0;
    const char separator[] = " \t\r\n";

    if ((file = fopen(filename, "r")) == NULL) {
        E_ERROR("Cannot open configuration file %s for reading\n",
                filename);
        return NULL;
    }

    ch = fgetc(file);
    /* Skip to the next interesting character */
    for (; ch != EOF && strchr(separator, ch); ch = fgetc(file)) ;

    if (ch == EOF) {
        fclose(file);
        return NULL;
    }

    /*
     * Initialize default argv, argc, and argv_size.
     */
    argv_size = 30;
    argc = 0;
    f_argv = (char **)ckd_calloc(argv_size, sizeof(char *));
    /* Silently make room for \0 */
    str = (char* )ckd_calloc(arg_max_length + 1, sizeof(char));
    quoting = 0;

    do {
        /* Handle arguments that are commented out */
        if (len == 0 && argc % 2 == 0) {
            while (ch == '#') {
                /* Skip everything until newline */
                for (ch = fgetc(file); ch != EOF && ch != '\n'; ch = fgetc(file)) ;
                /* Skip to the next interesting character */
                for (ch = fgetc(file); ch != EOF && strchr(separator, ch); ch = fgetc(file)) ;
            }

            /* Check if we are at the last line (without anything interesting in it) */
            if (ch == EOF)
                break;
        }

        /* Handle quoted arguments */
        if (ch == '"' || ch == '\'') {
            if (quoting == ch) /* End a quoted section with the same type */
                quoting = 0;
            else if (quoting) {
                E_ERROR("Nesting quotations is not supported!\n");
                rv = 1;
                break;
            }
            else
                quoting = ch; /* Start a quoted section */
        }
        else if (ch == EOF || (!quoting && strchr(separator, ch))) {
            /* Reallocate argv so it is big enough to contain all the arguments */
            if (argc >= argv_size) {
                char **tmp_argv;
                if (!(tmp_argv =
                       (char **)ckd_realloc(f_argv, argv_size * 2 * sizeof(char *)))) {
                    rv = 1;
                    break;
                }
                f_argv = tmp_argv;
                argv_size *= 2;
            }

            /* Add the string to the list of arguments */
            f_argv[argc] = ckd_salloc(str);
            len = 0;
            str[0] = '\0';
            argc++;

            if (quoting)
                E_WARN("Unclosed quotation, having EOF close it...\n");

            /* Skip to the next interesting character */
            for (; ch != EOF && strchr(separator, ch); ch = fgetc(file)) ;

            if (ch == EOF)
                break;

            /* We already have the next character */
            continue;
        }
        else {
            if (len >= arg_max_length) {
                /* Make room for more chars (including the \0 !) */
                char *tmp_str = str;
                if ((tmp_str = (char *)ckd_realloc(str, (1 + arg_max_length * 2) * sizeof(char))) == NULL) {
                    rv = 1;
                    break;
                }
                str = tmp_str;
                arg_max_length *= 2;
            }
            /* Add the char to the argument string */
            str[len++] = ch;
            /* Always null terminate */
            str[len] = '\0';
        }

        ch = fgetc(file);
    } while (1);

    fclose(file);

    ckd_free(str);

    if (rv) {
        for (ch = 0; ch < argc; ++ch)
            ckd_free(f_argv[ch]);
        ckd_free(f_argv);
        return NULL;
    }

    return parse_options(inout_cmdln, defn, argc, f_argv, strict);
}

void
cmd_ln_log_help_r(cmd_ln_t *cmdln, arg_t const* defn)
{
    if (defn == NULL)
        return;
    E_INFO("Arguments list definition:\n");
    if (cmdln == NULL) {
        cmdln = cmd_ln_parse_r(NULL, defn, 0, NULL, FALSE);
        arg_log_r(cmdln, defn, TRUE, FALSE);
        ps_config_free(cmdln);
    }
    else
        arg_log_r(cmdln, defn, TRUE, FALSE);
}

void
cmd_ln_log_values_r(cmd_ln_t *cmdln, arg_t const* defn)
{
    if (defn == NULL)
        return;
    E_INFO("Current configuration:\n");
    arg_log_r(cmdln, defn, FALSE, FALSE);
}

int
cmd_ln_exists_r(cmd_ln_t *cmdln, const char *name)
{
    void *val;
    if (cmdln == NULL)
        return FALSE;
    return (hash_table_lookup(cmdln->ht, name, &val) == 0);
}

cmd_ln_val_t *
cmd_ln_access_r(cmd_ln_t *cmdln, const char *name)
{
    void *val;
    if (hash_table_lookup(cmdln->ht, name, &val) < 0) {
        E_ERROR("Unknown argument: %s\n", name);
        return NULL;
    }
    return (cmd_ln_val_t *)val;
}

int
cmd_ln_type_r(cmd_ln_t *cmdln, char const *name)
{
    cmd_ln_val_t *val = cmd_ln_access_r(cmdln, name);
    if (val == NULL)
        return 0;
    return val->type;
}


char const *
ps_config_str(cmd_ln_t *cmdln, char const *name)
{
    cmd_ln_val_t *val;
    val = cmd_ln_access_r(cmdln, name);
    if (val == NULL)
        return NULL;
    if (!(val->type & ARG_STRING)) {
        E_ERROR("Argument %s does not have string type\n", name);
        return NULL;
    }
    return (char const *)val->val.ptr;
}

long
ps_config_int(cmd_ln_t *cmdln, char const *name)
{
    cmd_ln_val_t *val;
    val = cmd_ln_access_r(cmdln, name);
    if (val == NULL)
        return 0L;
    if (!(val->type & (ARG_INTEGER | ARG_BOOLEAN))) {
        E_ERROR("Argument %s does not have integer type\n", name);
        return 0L;
    }
    return val->val.i;
}

double
ps_config_float(cmd_ln_t *cmdln, char const *name)
{
    cmd_ln_val_t *val;
    val = cmd_ln_access_r(cmdln, name);
    if (val == NULL)
        return 0.0;
    if (!(val->type & ARG_FLOATING)) {
        E_ERROR("Argument %s does not have floating-point type\n", name);
        return 0.0;
    }
    return val->val.fl;
}

void
ps_config_set_str(cmd_ln_t *cmdln, char const *name, char const *str)
{
    cmd_ln_val_t *val;
    val = cmd_ln_access_r(cmdln, name);
    if (val == NULL) {
        E_ERROR("Unknown argument: %s\n", name);
        return;
    }
    if (!(val->type & ARG_STRING)) {
        E_ERROR("Argument %s does not have string type\n", name);
        return;
    }
    ckd_free(val->val.ptr);
    val->val.ptr = ckd_salloc(str);
}

void
cmd_ln_set_str_extra_r(cmd_ln_t *cmdln, char const *name, char const *str)
{
    cmd_ln_val_t *val;
    if (hash_table_lookup(cmdln->ht, name, (void **)&val) < 0) {
	val = cmd_ln_val_init(ARG_STRING, name, str);
	hash_table_enter(cmdln->ht, val->name, (void *)val);
    } else {
        if (!(val->type & ARG_STRING)) {
            E_ERROR("Argument %s does not have string type\n", name);
            return;
        }
        ckd_free(val->val.ptr);
        val->val.ptr = ckd_salloc(str);
    }
}

void
cmd_ln_set_int_r(cmd_ln_t *cmdln, char const *name, long iv)
{
    cmd_ln_val_t *val;
    val = cmd_ln_access_r(cmdln, name);
    if (val == NULL) {
        E_ERROR("Unknown argument: %s\n", name);
        return;
    }
    if (!(val->type & (ARG_INTEGER | ARG_BOOLEAN))) {
        E_ERROR("Argument %s does not have integer type\n", name);
        return;
    }
    val->val.i = iv;
}

void
cmd_ln_set_float_r(cmd_ln_t *cmdln, char const *name, double fv)
{
    cmd_ln_val_t *val;
    val = cmd_ln_access_r(cmdln, name);
    if (val == NULL) {
        E_ERROR("Unknown argument: %s\n", name);
        return;
    }
    if (!(val->type & ARG_FLOATING)) {
        E_ERROR("Argument %s does not have floating-point type\n", name);
        return;
    }
    val->val.fl = fv;
}

cmd_ln_t *
ps_config_retain(cmd_ln_t *cmdln)
{
    ++cmdln->refcount;
    return cmdln;
}

int
ps_config_free(cmd_ln_t *cmdln)
{
    if (cmdln == NULL)
        return 0;
    if (--cmdln->refcount > 0)
        return cmdln->refcount;

    if (cmdln->ht) {
        glist_t entries;
        gnode_t *gn;
        int32 n;

        entries = hash_table_tolist(cmdln->ht, &n);
        for (gn = entries; gn; gn = gnode_next(gn)) {
            hash_entry_t *e = (hash_entry_t *)gnode_ptr(gn);
            cmd_ln_val_free((cmd_ln_val_t *)e->val);
        }
        glist_free(entries);
        hash_table_free(cmdln->ht);
        cmdln->ht = NULL;
    }

    if (cmdln->f_argv) {
        int32 i;
        for (i = 0; i < (int32)cmdln->f_argc; ++i) {
            ckd_free(cmdln->f_argv[i]);
        }
        ckd_free(cmdln->f_argv);
        cmdln->f_argv = NULL;
        cmdln->f_argc = 0;
    }
    ckd_free(cmdln);
    return 0;
}

/* vim: set ts=4 sw=4: */
