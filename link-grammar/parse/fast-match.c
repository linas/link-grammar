/**************************************************************************/
/* Copyright (c) 2004                                                     */
/* Daniel Sleator, David Temperley, and John Lafferty                     */
/* Copyright (c) 2014 Linas Vepstas                                       */
/* All rights reserved                                                    */
/*                                                                        */
/* Use of the link grammar parsing system is subject to the terms of the  */
/* license set forth in the LICENSE file included with this software.     */
/* This license allows free redistribution and use in source and binary   */
/* forms, with or without modification, subject to certain conditions.    */
/*                                                                        */
/**************************************************************************/

#include "api-structures.h"  // For Sentence_s
#include "connectors.h"
#include "disjunct-utils.h"
#include "fast-match.h"
#include "string-set.h"
#include "dict-common/dict-common.h"   // For contable
#include "tokenize/word-structures.h"  // For Word_struct
#include "tokenize/wordgraph.h"
#include "tokenize/tok-structures.h" // XXX TODO provide gword access methods!
#include "utilities.h"                  // UNREACHABLE

/**
 * The entire goal of this file is provide a fast lookup of all of the
 * disjuncts on a given word that might be able to connect to a given
 * connector on the left or the right. The main entry point is
 * form_match_list(), which performs this lookup.
 *
 * The lookup is fast, because it uses a precomputed lookup table to
 * find the match candidates.  The lookup table is stocked by looking
 * at all disjuncts on all words, and sorting them into bins organized
 * by connectors they could potentially connect to.  The lookup table
 * is created by calling the alloc_fast_matcher() function.
 *
 * free_fast_matcher() is used to free the matcher.
 * form_match_list() manages its memory as a "stack" - match-lists are
 * pushed on this stack. The said stack size gets over 2048 entries only
 * for long and/or complex sentences.
 * pop_match_list() releases the memory that form_match_list() returned
 * by unwinding this stack.
 */

#define MATCH_LIST_SIZE_INIT 4096 /* the initial size of the match-list stack */
#define MATCH_LIST_SIZE_INC 2     /* match-list stack increase size factor */

/**
 * Returns the number of disjuncts in the list that have non-null
 * left connector lists.
 */
static int left_disjunct_list_length(const Disjunct * d)
{
	int i;
	for (i=0; d!=NULL; d=d->next) {
		if (d->left != NULL) i++;
	}
	return i;
}

static int right_disjunct_list_length(const Disjunct * d)
{
	int i;
	for (i=0; d!=NULL; d=d->next) {
		if (d->right != NULL) i++;
	}
	return i;
}

/**
 * Push a match-list element into the match-list array.
 */
static void push_match_list_element(fast_matcher_t *ctxt, Disjunct *d)
{
	if (ctxt->match_list_end >= ctxt->match_list_size)
	{
		ctxt->match_list_size *= MATCH_LIST_SIZE_INC;
		/* XXX the realloc clobbers xalloc count */
		ctxt->match_list = realloc(ctxt->match_list,
		                      ctxt->match_list_size * sizeof(*ctxt->match_list));
	}

	ctxt->match_list[ctxt->match_list_end++] = d;
}

/**
 * Free all of the hash tables and Match_nodes
 */
void free_fast_matcher(Sentence sent, fast_matcher_t *mchxt)
{
	if (NULL == mchxt) return;

	for (WordIdx w = 0; w < mchxt->size; w++)
	{
		xfree((char *)mchxt->l_table[w], mchxt->l_table_size[w] * sizeof (Match_node *));
		xfree((char *)mchxt->r_table[w], mchxt->r_table_size[w] * sizeof (Match_node *));
	}

	free(mchxt->match_list);
	lgdebug(6, "Sentence size %zu, match_list_size %zu\n",
	        mchxt->size, mchxt->match_list_size);

	xfree(mchxt->l_table_size, mchxt->size * sizeof(unsigned int));
	xfree(mchxt->l_table, mchxt->size * sizeof(Match_node **));
	xfree(mchxt, sizeof(fast_matcher_t));
}

/**
 * Adds the match node m to the sorted list of match nodes l.
 * Makes the list sorted from smallest to largest.
 */
static Match_node * add_to_right_table_list(Match_node * m, Match_node * l)
{
	Match_node *p, *prev;

	if (l == NULL) return m;

	/* Insert m at head of list */
	if ((m->d->right->nearest_word) <= (l->d->right->nearest_word))
	{
		m->next = l;
		return m;
	}

	/* Walk list to insertion point */
	prev = l;
	p = prev->next;
	while (p != NULL && ((m->d->right->nearest_word) > (p->d->right->nearest_word)))
	{
		prev = p;
		p = p->next;
	}

	m->next = p;
	prev->next = m;

	return l;  /* return pointer to original head */
}

/**
 * Adds the match node m to the sorted list of match nodes l.
 * Makes the list sorted from largest to smallest
 */
static Match_node * add_to_left_table_list(Match_node * m, Match_node * l)
{
	Match_node *p, *prev;

	if (l == NULL) return m;

	/* Insert m at head of list */
	if ((m->d->left->nearest_word) >= (l->d->left->nearest_word))
	{
		m->next = l;
		return m;
	}

	/* Walk list to insertion point */
	prev = l;
	p = prev->next;
	while (p != NULL && ((m->d->left->nearest_word) < (p->d->left->nearest_word)))
	{
		prev = p;
		p = p->next;
	}

	m->next = p;
	prev->next = m;

	return l;  /* return pointer to original head */
}

/**
 * Compare only the uppercase part of two connectors.
 * Return true if they are the same, else false.
 */
static bool con_uc_eq(const Connector *c1, const Connector *c2)
{
	return (connector_uc_num(c1) == connector_uc_num(c2));
}

static Match_node **get_match_table_entry(unsigned int size, Match_node **t,
                                          Connector * c, int dir)
{
	unsigned int h, s;

	s = h = connector_uc_hash(c) & (size-1);

	if (dir == 1) {
		while (NULL != t[h])
		{
			if (con_uc_eq(t[h]->d->right, c)) break;

			/* Increment and try again. Every hash bucket MUST have
			 * a unique upper-case part, since later on, we only
			 * compare the lower-case parts, assuming upper-case
			 * parts are already equal. So just look for the next
			 * unused hash bucket.
			 */
			h = (h + 1) & (size-1);
			if (h == s) return NULL;
		}
	}
	else
	{
		while (NULL != t[h])
		{
			if (con_uc_eq(t[h]->d->left, c)) break;
			h = (h + 1) & (size-1);
			if (h == s) return NULL;
		}
	}

	return &t[h];
}

/**
 * The disjunct d (whose left or right pointer points to c) is put
 * into the appropriate hash table
 * dir =  1, we're putting this into a right table.
 * dir = -1, we're putting this into a left table.
 */
static void put_into_match_table(Sentence sent, unsigned int size,
                                 Match_node ** t, Disjunct * d,
                                 Connector * c, int dir)
{
	Match_node *m, **xl;

	m = pool_alloc(sent->fm_Match_node);
	m->next = NULL;
	m->d = d;

	xl = get_match_table_entry(size, t, c, dir);
	assert(NULL != xl, "get_match_table_entry: Overflow");
	if (dir == 1) {
		*xl = add_to_right_table_list(m, *xl);
	}
	else
	{
		*xl = add_to_left_table_list(m, *xl);
	}
}

fast_matcher_t* alloc_fast_matcher(const Sentence sent)
{
	unsigned int size;
	size_t w;
	size_t len;
	Match_node ** t;
	Disjunct * d;
	fast_matcher_t *ctxt;

	ctxt = (fast_matcher_t *) xalloc(sizeof(fast_matcher_t));
	ctxt->size = sent->length;
	ctxt->l_table_size = xalloc(2 * sent->length * sizeof(unsigned int));
	ctxt->r_table_size = ctxt->l_table_size + sent->length;
	ctxt->l_table = xalloc(2 * sent->length * sizeof(Match_node **));
	ctxt->r_table = ctxt->l_table + sent->length;
	memset(ctxt->l_table, 0, 2 * sent->length * sizeof(Match_node **));

	ctxt->match_list_size = MATCH_LIST_SIZE_INIT;
	ctxt->match_list = xalloc(ctxt->match_list_size * sizeof(*ctxt->match_list));
	ctxt->match_list_end = 0;

	if (NULL != sent->fm_Match_node)
	{
		pool_reuse(sent->fm_Match_node);
	}
	else
	{
		sent->fm_Match_node =
			pool_new(__func__, "Match_node",
			         /*num_elements*/2048, sizeof(Match_node),
			         /*zero_out*/false, /*align*/true, /*exact*/false);
	}

	for (w=0; w<sent->length; w++)
	{
		len = left_disjunct_list_length(sent->word[w].d);
		len = MIN(sent->dict->contable.num_con, len);
		size = next_power_of_two_up(len);
		ctxt->l_table_size[w] = size;
		t = ctxt->l_table[w] = (Match_node **) xalloc(size * sizeof(Match_node *));
		memset(t, 0, size * sizeof(Match_node *));

		for (d = sent->word[w].d; d != NULL; d = d->next)
		{
			if (d->left != NULL)
			{
				//printf("%s %d\n", connector_string(d->left), d->left->length_limit);
				put_into_match_table(sent, size, t, d, d->left, -1);
			}
		}

		len = right_disjunct_list_length(sent->word[w].d);
		len = MIN(sent->dict->contable.num_con, len);
		size = next_power_of_two_up(len);
		ctxt->r_table_size[w] = size;
		t = ctxt->r_table[w] = (Match_node **) xalloc(size * sizeof(Match_node *));
		memset(t, 0, size * sizeof(Match_node *));

		for (d = sent->word[w].d; d != NULL; d = d->next)
		{
			if (d->right != NULL)
			{
				//printf("%s %d\n", connector_string(d->right), d->right->length_limit);
				put_into_match_table(sent, size, t, d, d->right, 1);
			}
		}
	}

	return ctxt;
}

#if 0
/**
 * Print statistics on various connector matching aspects.
 * A summary can be found by the shell commands:
 * link-parser < file.batch | grep match_stats: | sort | uniq -c
 */
static void match_stats(Connector *c1, Connector *c2)
{
	if (NULL == c1) printf("match_stats: cache\n");
	if (NULL == c2) return;
	if ((1 == c1->uc_start) && (1 == c2->uc_start) &&
	    (c1->string[0] == c2->string[0]))
	{
		printf("match_stats: h/d mismatch\n");
	}

	if (0 == c1->lc_start) printf("match_stats: no lc (c1)\n");
	if (0 == c2->lc_start) printf("match_stats: no lc (c2)\n");

	if (string_set_cmp(c1->string, c2->string)) printf("match_stats: same\n");

	const char *a = &c1->string[c1->lc_start];
	const char *b = &c2->string[c2->lc_start];
	do
	{
		if (*a != *b && (*a != '*') && (*b != '*')) printf("match_stats: lc false\n");
		a++;
		b++;
	} while (*a != '\0' && *b != '\0');
	printf("match_stats: lc true\n");
}
#else
#define match_stats(a, b)
#endif

#ifdef DEBUG
#undef N
#define N(c) (c?connector_string(c):"")
#define S(c) (c?c->suffix_id:0)

/**
 * Print the match list, including connector match indications.
 * Usage: link-parser -verbosity=9 -debug=print_match_list
 * Output format:
 * MATCH_NODE list_id:  lw>lc   [=]   left<w>right   [=]    rc<rw
 *
 * Regretfully this version doesn't indicate which match shortcut has been
 * used, and which nodes are from mr or ml or both (the full print version
 * clutters the source code very much, as it needs to be inserted in plenty
 * of places.)
 */
static void print_match_list(fast_matcher_t *ctxt, int id, size_t mlb, int w,
                             Connector *lc, int lw,
                             Connector *rc, int rw)
{
	if (!verbosity_level(9)) return;
	Disjunct **m = &ctxt->match_list[mlb];

	for (; NULL != *m; m++)
	{
		Disjunct *d = *m;

		prt_error("MATCH_NODE %5d: (%5d) %02d>%-9s %c %9s<%02d>%-9s %c %9s<%02d (%5d)\n",
		       id, S(lc), lw ,N(lc), d->match_left ? '=': ' ',
		       N(d->left), w, N(d->right),
		       d->match_right? '=' : ' ', N(rc), rw, S(rc));
	}
}
#else
#define print_match_list(...)
#endif

typedef struct
{
	const condesc_t *desc;
	bool match;
} match_cache;

/**
 * Match the lower-case parts of connectors, and the head-dependent,
 * using a cache of the most recent compare.  Due to the way disjuncts
 * are written, we are often asked to compare to the same connector
 * 3 or 4 times in a row. So if we already did that compare, just use
 * the cached result. (i.e. the caching here is almost trivial, but it
 * works well).
 */
static bool do_match_with_cache(Connector *a, Connector *b, match_cache *c_con)
{
	/* The following uses a string-set compare - string_set_cmp() cannot
	 * be used here because c_con->string may be NULL. */
	match_stats(c_con->string == a->string ? NULL : a, NULL);
	UNREACHABLE(connector_desc(a) == NULL); // clang static analyzer suppression.
	if (c_con->desc == connector_desc(a))
	{
		/* The match_cache desc field is initialized to NULL, and this is
		 * enough because the connector desc filed cannot be NULL, as it
		 * actually fetched a non-empty match list. */
		PRAGMA_MAYBE_UNINITIALIZED
		return c_con->match;
		PRAGMA_END
	}

	/* No cache match. Check if the connectors match and cache the result.
	 * We know that the uc parts of the connectors are the same, because
	 * we fetch the matching lists according to the uc part or the
	 * connectors to be matched. So the uc parts are not checked here. */
	c_con->match = lc_easy_match(connector_desc(a), connector_desc(b));
	c_con->desc = connector_desc(a);

	return c_con->match;
}

typedef struct
{
	const Gword *gword;
	bool same_alternative;
} gword_cache;

/**
 * Return true iff c1 and c2 are from the same alternative.
 * An optimization for English checks if one of the connectors belongs
 * to an original sentence word (c2 is checked first for an inline
 * optimization opportunity).
 * If a wordgraph word of the checked connector is the same
 * as of the previously checked one, use the cached result.
 * (The first wordgraph word is used for cache validity indication,
 * but there is only one most of the times anyway.)
 */
#define ALT_CONNECTION_POSSIBLE
#define OPTIMIZE_EN
static bool alt_connection_possible(Connector *c1, Connector *c2,
                                    gword_cache *c_con)
{
#ifdef ALT_CONNECTION_POSSIBLE
	bool same_alternative = false;

#ifdef OPTIMIZE_EN
	/* Try a shortcut first. */
	if ((c2->originating_gword->o_gword->hier_depth == 0) ||
	    (c1->originating_gword->o_gword->hier_depth == 0))
	{
			return true;
	}
#endif /* OPTIMIZE_EN */

	if (c1->originating_gword->o_gword == c_con->gword)
		return c_con->same_alternative;

	/* Each of the loops is of one iteration most of the times. */
	for (const gword_set *ga = c1->originating_gword; NULL != ga; ga = ga->next) {
		for (const gword_set *gb = c2->originating_gword; NULL != gb; gb = gb->next) {
			if (in_same_alternative(ga->o_gword, gb->o_gword)) {
				 same_alternative = true;
				 break;
			}
		}
		if (same_alternative) break;
	}

	c_con->same_alternative = same_alternative;
	c_con->gword = c1->originating_gword->o_gword;


	return same_alternative;
#else
	return true;
#endif /* ALT_CONNECTION_POSSIBLE */
}

/**
 * Add match-list termination element (NULL).
 * Optionally print it (for debug).
 * Return the match list start.
 */
static size_t terminate_match_list(fast_matcher_t *ctxt, int id,
                             size_t ml_start, int w,
                             Connector *lc, int lw,
                             Connector *rc, int rw)
{
	push_match_list_element(ctxt, NULL);
	print_match_list(ctxt, id, ml_start, w, lc, lw, rc, rw);
	return ml_start;
}

/**
 * Forms and returns a list of disjuncts coming from word w, that
 * actually matches lc or rc or both. The lw and rw are the words from
 * which lc and rc came respectively.
 *
 * The list is returned in an array of Match_nodes.  This list
 * contains no duplicates, because when processing the ml list, only
 * elements whose match_left is true are included, and such elements are
 * not included again when processing the mr list.
 *
 * Note that if both lc and rc match the corresponding connectors of w,
 * match_left is set to true when the ml list is processed and the
 * disjunct is then added to the result list, and match_right of the
 * same disjunct is set to true when the mr list is processed, and this
 * disjunct is not added again.
 *
 * "lc optimization" (see below) marks here a special optimization for the
 * left connector (lc): If it is non-NULL and doesn't match a disjunct,
 * there is no need to try to match the right connector (rc), since the
 * linkage count would be 0 in any case. See do_count() for details. In
 * that case we can skip the corresponding match list element, or even
 * immediately return an empty match list if there are no matches for lc.
 */
size_t
form_match_list(fast_matcher_t *ctxt, int w,
                Connector *lc, int lw,
                Connector *rc, int rw)
{
	Match_node *mx, *mr_end, **mxp;
	size_t front = get_match_list_position(ctxt);
	Match_node *ml = NULL, *mr = NULL;
	match_cache mc;
	gword_cache gc;

	gc.same_alternative = false;

	/* Get the lists of candidate matching disjuncts of word w for lc and
	 * rc.  Consider each of these lists only if the length_limit of lc
	 * rc and also w, is not greater then the distance between their word
	 * and the word w. */
	if ((lc != NULL) && ((w - lw) <= lc->length_limit))
	{
		mxp = get_match_table_entry(ctxt->l_table_size[w], ctxt->l_table[w], lc, -1);
		if (NULL != mxp) ml = *mxp;
	}
	if ((lc != NULL) && (ml == NULL)) /* lc optimization */
		return terminate_match_list(ctxt, -1, front, w, lc, lw, rc, rw);

	if ((rc != NULL) && ((rw - w) <= rc->length_limit))
	{
		mxp = get_match_table_entry(ctxt->r_table_size[w], ctxt->r_table[w], rc, 1);
		if (NULL != mxp) mr = *mxp;
	}

	if ((ml == NULL) && (mr == NULL))
		return terminate_match_list(ctxt, -2, front, w, lc, lw, rc, rw);

#ifdef VERIFY_MATCH_LIST
	static int id = 0;
	int lid = ++id; /* A local copy, for multi-threading support. */
#endif

	for (mx = mr; mx != NULL; mx = mx->next)
	{
		if (mx->d->right->nearest_word > rw) break;
		mx->d->match_left = false;
	}
	mr_end = mx;

	/* Construct the list of things that could match the left. */
	mc.desc = NULL;
	gc.gword = NULL;
	for (mx = ml; mx != NULL; mx = mx->next)
	{
		if (mx->d->left->nearest_word < lw) break;
		if ((w - lw) > mx->d->left->length_limit) continue;

		mx->d->match_left = do_match_with_cache(mx->d->left, lc, &mc) &&
		                    alt_connection_possible(mx->d->left, lc, &gc);
		if (!mx->d->match_left) continue;
		mx->d->match_right = false;

#ifdef VERIFY_MATCH_LIST
		mx->d->match_id = lid;
#endif
		push_match_list_element(ctxt, mx->d);
	}

	if ((lc != NULL) && is_no_match_list(ctxt, front)) /* lc optimization */
		return terminate_match_list(ctxt, -3, front, w, lc, lw, rc, rw);

	/* Append the list of things that could match the right.
	 * Note that it is important to set here match_right correctly even
	 * if we are going to skip this element here because its match_left
	 * is true, since then it means it is already included in the match
	 * list. */
	mc.desc = NULL;
	gc.gword = NULL;
	for (mx = mr; mx != mr_end; mx = mx->next)
	{
		if ((rw - w) > mx->d->right->length_limit) continue;

		if ((lc != NULL) && !mx->d->match_left) continue; /* lc optimization */
		mx->d->match_right = do_match_with_cache(mx->d->right, rc, &mc) &&
			                  alt_connection_possible(mx->d->right, rc, &gc);
		if (!mx->d->match_right || mx->d->match_left) continue;

#ifdef VERIFY_MATCH_LIST
		mx->d->match_id = lid;
#endif
		push_match_list_element(ctxt, mx->d);
	}

	return terminate_match_list(ctxt, lid, front, w, lc, lw, rc, rw);
}
