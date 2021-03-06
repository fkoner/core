/*
   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.
*/

#ifndef CFENGINE_SYNTAX_H
#define CFENGINE_SYNTAX_H

#include "cf3.defs.h"

#include "sequence.h"
#include "writer.h"

#include <stdio.h>

/*
 * WARNING: This file is in need of serious cleanup.
 */


typedef enum
{
    SYNTAX_TYPE_MATCH_OK,

    SYNTAX_TYPE_MATCH_ERROR_UNEXPANDED,
    SYNTAX_TYPE_MATCH_ERROR_RANGE_BRACKETED,
    SYNTAX_TYPE_MATCH_ERROR_RANGE_MULTIPLE_ITEMS,
    SYNTAX_TYPE_MATCH_ERROR_GOT_SCALAR,
    SYNTAX_TYPE_MATCH_ERROR_GOT_LIST,

    SYNTAX_TYPE_MATCH_ERROR_SCALAR_OUT_OF_RANGE,

    SYNTAX_TYPE_MATCH_ERROR_STRING_UNIX_PERMISSION,

    SYNTAX_TYPE_MATCH_ERROR_INT_PARSE,
    SYNTAX_TYPE_MATCH_ERROR_INT_OUT_OF_RANGE,

    SYNTAX_TYPE_MATCH_ERROR_REAL_INF,
    SYNTAX_TYPE_MATCH_ERROR_REAL_OUT_OF_RANGE,

    SYNTAX_TYPE_MATCH_ERROR_OPTS_OUT_OF_RANGE,

    SYNTAX_TYPE_MATCH_ERROR_FNCALL_RETURN_TYPE,
    SYNTAX_TYPE_MATCH_ERROR_FNCALL_UNKNOWN,

    SYNTAX_TYPE_MATCH_ERROR_CONTEXT_OUT_OF_RANGE,

    SYNTAX_TYPE_MATCH_MAX
} SyntaxTypeMatch;

const char *SyntaxTypeMatchToString(SyntaxTypeMatch result);

int CheckParseVariableName(const char *name);
SyntaxTypeMatch CheckConstraintTypeMatch(const char *lval, Rval rval, DataType dt, const char *range, int level);
SyntaxTypeMatch CheckParseContext(const char *context, const char *range);
DataType StringDataType(EvalContext *ctx, const char *scopeid, const char *string);
DataType ExpectedDataType(const char *lvalname);
bool IsDataType(const char *s);
const PromiseTypeSyntax *PromiseTypeSyntaxLookup(const char *bundle_type, const char *promise_type_name);

const ConstraintSyntax *PromiseTypeSyntaxGetConstraintSyntax(const PromiseTypeSyntax *promise_type_syntax, const char *lval);

/**
 * @brief An array of ConstraintSyntax for the given body_type
 * @param body_type Type of body, e.g. 'contain'
 * @return NULL if not found
 */
const ConstraintSyntax *BodySyntaxLookup(const char *body_type);
const ConstraintSyntax *ControlBodySyntaxGet(const char *agent_type);


const ConstraintSyntax *BodySyntaxGetConstraintSyntax(const ConstraintSyntax *body_syntax, const char *lval);


#define ConstraintSyntaxNewNull() { NULL, DATA_TYPE_NONE, .range.validation_string = NULL }
#define ConstraintSyntaxNewBool(lval, description, default_value) { lval, DATA_TYPE_OPTION, .range.validation_string = CF_BOOL, description, default_value }

#define ConstraintSyntaxNewOption(lval, options, description, default_value) { lval, DATA_TYPE_OPTION, .range.validation_string = options, description, default_value }
#define ConstraintSyntaxNewOptionList(lval, item_range, description) { lval, DATA_TYPE_OPTION_LIST, .range.validation_string = item_range, description, NULL }

#define ConstraintSyntaxNewString(lval, regex, description, default_value) { lval, DATA_TYPE_STRING, .range.validation_string = regex, description, default_value }
#define ConstraintSyntaxNewStringList(lval, item_range, description) { lval, DATA_TYPE_STRING_LIST, .range.validation_string = item_range, description, NULL }

#define ConstraintSyntaxNewInt(lval, int_range, description, default_value) { lval, DATA_TYPE_INT, .range.validation_string = int_range, description, default_value }
#define ConstraintSyntaxNewIntRange(lval, int_range, description, default_value ) { lval , DATA_TYPE_INT_RANGE, .range.validation_string = int_range, description, default_value }
#define ConstraintSyntaxNewIntList(lval, description) { lval, DATA_TYPE_INT_LIST, .range.validation_string = CF_INTRANGE, description }

#define ConstraintSyntaxNewReal(lval, real_range, description, default_value) { lval, DATA_TYPE_REAL, .range.validation_string = real_range, description, default_value }
#define ConstraintSyntaxNewRealList(lval, description) { lval, DATA_TYPE_REAL_LIST, .range.validation_string = CF_REALRANGE, description }

#define ConstraintSyntaxNewContext(lval, description) { lval, DATA_TYPE_CONTEXT, .range.validation_string = CF_CLASSRANGE, description }
#define ConstraintSyntaxNewContextList(lval, description, default_value) { lval, DATA_TYPE_CONTEXT_LIST, .range.validation_string = CF_CLASSRANGE, description, default_value }

#define ConstraintSyntaxNewBody(lval, body_syntax, description) { lval, DATA_TYPE_BODY, .range.body_type_syntax = body_syntax, description }
#define ConstraintSyntaxNewBundle(lval, description) { lval, DATA_TYPE_BUNDLE, .range.validation_string = CF_BUNDLE, description }


#define ConstraintSetSyntaxNew(constraints, validation_fn) { constraints, validation_fn }
#define BodyTypeSyntaxNew(body_type, constraint_set) { body_type, constraint_set }


#define PromiseTypeSyntaxNew(agent_type, promise_type, constraint_set) { agent_type, promise_type, constraint_set }
#define PromiseTypeSyntaxNewNull() PromiseTypeSyntaxNew(NULL, NULL, ConstraintSetSyntaxNew(NULL, NULL))

/* print a specification of the CFEngine language */
void SyntaxPrintAsJson(Writer *writer);

#endif
