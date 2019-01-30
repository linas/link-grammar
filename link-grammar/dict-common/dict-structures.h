/*************************************************************************/
/* Copyright (c) 2004                                                    */
/* Daniel Sleator, David Temperley, and John Lafferty                    */
/* Copyright (c) 2013 Linas Vepstas                                      */
/* All rights reserved                                                   */
/*                                                                       */
/* Use of the link grammar parsing system is subject to the terms of the */
/* license set forth in the LICENSE file included with this software.    */
/* This license allows free redistribution and use in source and binary  */
/* forms, with or without modification, subject to certain conditions.   */
/*                                                                       */
/*************************************************************************/

#ifndef _LG_DICT_STRUCTURES_H_
#define _LG_DICT_STRUCTURES_H_

#include "link-grammar/link-features.h"
#include "link-includes.h"

LINK_BEGIN_DECLS

/* Forward decls */
typedef struct Dict_node_struct Dict_node;
typedef struct Exp_struct Exp;
typedef struct E_list_struct E_list;
typedef struct Word_file_struct Word_file;
typedef struct condesc_struct condesc_t;

/**
 * Types of Exp_struct structures
 */
typedef enum
{
	OR_type = 1,  // Exclusive-choice OR
	AND_type,     // non-commuting AND ("with")
	PAR_type,     // Exclusive-AND
	CONNECTOR_type
} Exp_type;

/**
 * The E_list and Exp structures defined below comprise the expression
 * trees that are stored in the dictionary.  The expression has a type
 * (OR_type, AND_type or CONNECTOR_type).  If it is not a terminal it
 * has a list (an E_list) of children. Else "condesc" is the connector
 * descriptor, when "dir" indicates the connector direction.
 */
struct Exp_struct
{
	Exp * next;    /* Used only for memory management, for freeing */
	Exp_type type; /* One of three types: AND, OR, or connector. */
	char dir;      /* The connector connects to: '-': the left; '+': the right */
	bool multi;    /* TRUE if a multi-connector (for connector)  */
	union {
		E_list * l;           /* Only needed for non-terminals */
		condesc_t * condesc;  /* Only needed if it's a connector */
	} u;
	double cost;   /* The cost of using this expression.
	                  Only used for non-terminals */
};

struct E_list_struct
{
	E_list * next;
	Exp * e;
};

/* API to access the above structure. */
static inline Exp_type lg_exp_get_type(const Exp* exp) { return exp->type; }
static inline char lg_exp_get_dir(const Exp* exp) { return exp->dir; }
static inline bool lg_exp_get_multi(const Exp* exp) { return exp->multi; }
const char* lg_exp_get_string(const Exp*);
static inline double lg_exp_get_cost(const Exp* exp) { return exp->cost; }

/**
 * The dictionary is stored as a binary tree comprised of the following
 * nodes.  A list of these (via right pointers) is used to return
 * the result of a dictionary lookup.
 */
struct Dict_node_struct
{
	const char * string;  /* The word itself */
	Word_file * file;     /* The file the word came from (NULL if dict file) */
	Exp       * exp;
	Dict_node *left, *right;
};

LINK_END_DECLS

#endif /* _LG_DICT_STRUCTURES_H_ */
