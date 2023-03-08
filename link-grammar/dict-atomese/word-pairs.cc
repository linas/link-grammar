/*
 * word-pairs.cc
 *
 * Create disjuncts consisting of optionals, from word-pairs.
 *
 * Copyright (c) 2022, 2023 Linas Vepstas <linasvepstas@gmail.com>
 */
#ifdef HAVE_ATOMESE

#include <sys/time.h>
#include <sys/resource.h>

#include <opencog/atomspace/AtomSpace.h>
#include <opencog/atoms/value/FloatValue.h>
#include <opencog/nlp/types/atom_types.h>
#undef STRINGIFY

extern "C" {
#include "../link-includes.h"              // For Dictionary
#include "../api-structures.h"             // For Sentence_s
#include "../dict-common/dict-common.h"    // for Dictionary_s
#include "../dict-common/dict-internals.h" // for dict_node_free_lookup()
#include "../dict-common/dict-utils.h"     // for size_of_expression()
#include "../dict-ram/dict-ram.h"
#include "../externs.h"                    // For verbosity
};

#include "local-as.h"

using namespace opencog;

/**
 * Returns the CPU usage time, summed over all threads, in seconds.
 * Most of the time lost in Atomese is going to be in some other thread.
 */
static double total_usage_time(void)
{
	struct rusage u;
	getrusage (RUSAGE_SELF, &u);
	return (u.ru_utime.tv_sec + ((double) u.ru_utime.tv_usec) / 1000000.0);
}

// Create a mini-dict, used only for caching word-pair dict-nodes.
Dictionary create_pair_cache_dict(Dictionary dict)
{
	Dictionary prca = (Dictionary) malloc(sizeof(struct Dictionary_s));
	memset(prca, 0, sizeof(struct Dictionary_s));

	/* Shared string-set */
	prca->string_set = dict->string_set;
	prca->name = string_set_add("word-pair cache", dict->string_set);

	/* Shared Exp_pool, too */
	prca->Exp_pool = dict->Exp_pool;

	return prca;
}

/// Return true if word-pairs for `germ` need to be fetched.
static bool need_pair_fetch(Local* local, const Handle& germ)
{
	if (nullptr == local->stnp) return false;

	const ValuePtr& fpv = germ->getValue(local->prk);
	if (fpv) return false;

	// Just tag it. We could tag it with a date. That way, we'd know
	// how old it is, to re-fetch it !?
	local->asp->set_value(germ, local->prk, createFloatValue(1.0));
	return true;
}

/// Get the word-pairs for `germ` from storage.
/// Return zero, if there are none; else return non-zero.
static size_t fetch_pairs(Local* local, const Handle& germ)
{
	double start = 0.0;
	if (D_USER_TIMES <= verbosity) start = total_usage_time();

	local->stnp->fetch_incoming_by_type(germ, LIST_LINK);
	local->stnp->barrier();

	size_t cnt = 0;
	HandleSeq rprs = germ->getIncomingSetByType(LIST_LINK);
	for (const Handle& rawpr : rprs)
	{
		local->stnp->fetch_incoming_by_type(rawpr, EDGE_LINK);
		cnt++;
	}
	local->stnp->barrier();

#define RES_COL_WIDTH 37
	if (D_USER_TIMES <= verbosity)
	{
		double now = total_usage_time();
		char s[128] = "";
		snprintf(s, sizeof(s), "Fetched %lu pairs: >>%s<<",
			cnt, germ->get_name().c_str());
		prt_error("Atomese: %-*s %6.2f seconds\n",
			RES_COL_WIDTH, s, now - start);
	}

	return cnt;
}

/// Get the word-pair cost. It's either a static cost, located at
/// `mikey`, or it's a dynamically-computed cost, built from a
/// formula at `miformula`.
static double pair_mi(Local* local, const Handle& evpr)
{
	double mi = local->pair_default;
	if (local->mikey)
	{
		const ValuePtr& mivp = evpr->getValue(local->mikey);
		if (mivp)
		{
			// MI is the second entry in the vector.
			const FloatValuePtr& fmivp = FloatValueCast(mivp);
			mi = fmivp->value()[local->pair_index];
		}
		return mi;
	}

	if (nullptr == local->miformula)
		return mi;

	// The formula expects a (ListLink left right) and that
	// is exactly the second Atom in the outgoing set of evpr.
	// TODO: perhaps in the future, the formula should take
	// evpr directly, instead?
	const AtomSpacePtr& asp = local->asp;
	Handle exout(asp->add_link(EXECUTION_OUTPUT_LINK,
		local->miformula, evpr->getOutgoingAtom(1)));
	ValuePtr midy = exout->execute(asp.get());

	// Same calculation as above.
	const FloatValuePtr& fmivp = FloatValueCast(midy);
	mi = fmivp->value()[local->pair_index];

	return mi;
}

/// Return true if there are any word-pairs for `germ`.
/// This will automatically fetch from storage, as needed.
///
/// This assumes the semi-hard-coded form
///    (Edge local->prp (List (Word ...) (Word ...)))
/// where `local->prp` is taken from `#define pair-predicate` in
/// the dict. It currently defaults to `(BondNode "ANY")`, although
/// past use included `(Predicate "word-pair")`.
///
static bool have_pairs(Local* local, const Handle& germ)
{
	if (need_pair_fetch(local, germ))
		fetch_pairs(local, germ);

	const AtomSpacePtr& asp = local->asp;
	const Handle& hpr = local->prp; // (BondNode "ANY")

	// Are there any pairs in the local AtomSpace?
	// If there's at least one, just return `true`.
	HandleSeq rprs = germ->getIncomingSetByType(LIST_LINK);
	for (const Handle& rawpr : rprs)
	{
		Handle evpr = asp->get_link(EDGE_LINK, hpr, rawpr);
		if (nullptr == evpr) continue;

		// The lookup will discard pairs with too low an MI.
		double mi = pair_mi(local, evpr);
		if (mi < local->pair_cutoff) continue;

		return true;
	}

	return false;
}

/// Return true if the given word occurs in some word-pair, else return
/// false. As a side-effect, word-pairs are loaded from storage.
static bool as_boolean_lookup(Dictionary dict, const char *s)
{
	Local* local = (Local*) (dict->as_server);

	Handle wrd = local->asp->add_node(WORD_NODE, s);

	// Are there any pairs that contain this word?
	if (have_pairs(local, wrd))
		return true;

	// Does this word belong to any classes?
	size_t nclass = wrd->getIncomingSetSizeByType(MEMBER_LINK);
	if (0 == nclass and local->stnp)
	{
		local->stnp->fetch_incoming_by_type(wrd, MEMBER_LINK);
		local->stnp->barrier();
	}

	for (const Handle& memb : wrd->getIncomingSetByType(MEMBER_LINK))
	{
		const Handle& wcl = memb->getOutgoingAtom(1);
		if (WORD_CLASS_NODE != wcl->get_type()) continue;

		// If there's at least one, return it.
		if (have_pairs(local, wcl))
			return true;
	}

	return false;
}

/// Return true if the given word occurs in some word-pair, else return
/// false. As a side-effect, word-pairs are loaded from storage.
bool pair_boolean_lookup(Dictionary dict, const char *s)
{
	Local* local = (Local*) (dict->as_server);

	// Don't bother going to the AtomSpace, if we've looked this up
	// word before. This will be faster, in all cases.
	{
		std::lock_guard<std::mutex> guard(local->dict_mutex);
		const auto& havew = local->have_pword.find(s);
		if (local->have_pword.end() != havew)
			return havew->second;
	}

	bool rc = as_boolean_lookup(dict, s);

	std::lock_guard<std::mutex> guard(local->dict_mutex);
	local->have_pword.insert({s,rc});
	return rc;
}


/// Create a list of connectors, one for each available word pair
/// containing the word in the germ. These are simply OR'ed together.
static Exp* make_pair_exprs(Dictionary dict, const Handle& germ)
{
	double start = 0.0;
	if (D_USER_TIMES <= verbosity) start = total_usage_time();

	Local* local = (Local*) (dict->as_server);
	const AtomSpacePtr& asp = local->asp;
	Exp* orhead = nullptr;

	const Handle& hpr = local->prp; // (BondNode "ANY")

	size_t cnt = 0;
	HandleSeq rprs = germ->getIncomingSetByType(LIST_LINK);
	for (const Handle& rawpr : rprs)
	{
		Handle evpr = asp->get_link(EDGE_LINK, hpr, rawpr);
		if (nullptr == evpr) continue;

		double mi = pair_mi(local, evpr);

		// If the MI is too low, just skip this.
		if (mi < local->pair_cutoff)
			continue;

		// Get the cached link-name for this pair.
		const std::string& slnk = cached_linkname(local, rawpr);

		// Direction is easy to determine: its either left or right.
		char cdir = '+';
		if (rawpr->getOutgoingAtom(1) == germ) cdir  = '-';

		// Create the connector
		Exp* eee = make_connector_node(dict,
			dict->Exp_pool, slnk.c_str(), cdir, false);

		double cost = (local->pair_scale * mi) + local->pair_offset;
		eee->cost = cost;

		or_enchain(dict->Exp_pool, orhead, eee);
		cnt ++;
	}

	lgdebug(D_USER_INFO, "Atomese: Created %lu of %lu pair exprs for >>%s<<\n",
		cnt, germ->getIncomingSetSizeByType(LIST_LINK),
		germ->get_name().c_str());

	if (D_USER_TIMES <= verbosity)
	{
		double now = total_usage_time();
		char s[128] = "";
		snprintf(s, sizeof(s), "Created %lu pairs: >>%s<<",
			cnt, germ->get_name().c_str());
		prt_error("Atomese: %-*s %6.2f seconds\n",
			RES_COL_WIDTH, s, now - start);
	}
	return orhead;
}

/// Get a list of connectors, one for each available word pair
/// containing the word in the germ. These are simply OR'ed together.
/// Get them from the local dictionary cache, if they're already
/// there; create them from scratch, polling the AtomSpace.
static Exp* get_pair_exprs(Dictionary dict, const Handle& germ)
{
	Local* local = (Local*) (dict->as_server);

	// Single big fat lock. The goal of this lock is to protect the
	// Exp_pool in `local->pair_dict`. We have to hold it, begining
	// to end, because two different threads might be trying to create
	// expressions for the same word, which then trip over a
	// duplicate_word error during the second dictionary insert.
	// Sadly, this wraps a big, fat slow Atomese section in the middle,
	// but I don't see a way out. Over time, lookups should become
	// increasingly rare, so this shouldn't matter, after a while.
	std::lock_guard<std::mutex> guard(local->dict_mutex);

	const char* wrd = germ->get_name().c_str();
	Dictionary prdct = local->pair_dict;
	Dict_node* dn = dict_node_lookup(prdct, wrd);

	if (dn)
	{
		lgdebug(D_USER_INFO, "Atomese: Found pairs in cache: >>%s<<\n", wrd);
		Exp* exp = dn->exp;
		dict_node_free_lookup(prdct, dn);
		return exp;
	}

	Exp* exp = make_pair_exprs(dict, germ);
	const char* ssc = string_set_add(wrd, dict->string_set);
	make_dn(prdct, exp, ssc);
	return exp;
}

/// Return word-pair expressions connecting the `germ` with any word
/// listed in `sent_words`.  If `sent_words` is empty, then return
/// all expressions for all word-pairs with `germ` in it. This
/// "pre-prunes" the set of all word-pairs to only those in this
/// sentence.
static Exp* get_sent_pair_exprs(Dictionary dict, const Handle& germ,
                                Pool_desc* pool,
                                const HandleSeq& sent_words)
{
	Exp* allexp = get_pair_exprs(dict, germ);
	if (0 == sent_words.size())
		return allexp;

	// Unary nodes are possible, in which case, it is just a connector.
	// Don't bother pruning.
	if (OR_type != allexp->type)
		return allexp;

	// Find all word-pairs involving the germ, and words in the
	// sentence. Then, look up the LG link name for these pairs.
	// Stick them into a hash table.
	Local* local = (Local*) (dict->as_server);
	std::unordered_set<std::string> links;
	for (const Handle& sentw : sent_words)
	{
		const Handle& lpr = local->asp->get_link(LIST_LINK, sentw, germ);
		if (lpr)
		{
			Handle lgc = get_lg_conn(local, lpr);
			if (lgc)
				links.insert(lgc->get_name());
		}
		const Handle& rpr = local->asp->get_link(LIST_LINK, germ, sentw);
		if (rpr)
		{
			Handle rgc = get_lg_conn(local, rpr);
			if (rgc)
				links.insert(rgc->get_name().c_str());
		}
	}

	Exp* sentex = nullptr;
	int nfound = 0;

	std::lock_guard<std::mutex> guard(local->dict_mutex);

	// The allexp is an OR_type expression. A linked list of connectors
	// follow, chained along `operand_next`.
	Exp* orch = allexp->operand_first;
	while (orch)
	{
		assert(CONNECTOR_type == orch->type, "unexpected expression!");

		if (links.end() != links.find(orch->condesc->string))
		{
			Exp* cpe = (Exp*) pool_alloc(pool);
			*cpe = *orch;
			cpe->operand_next = sentex;
			sentex = cpe;
			nfound ++;
		}
		orch = orch->operand_next;
	}

	const char* wrd = germ->get_name().c_str();
	lgdebug(D_USER_INFO,
		"Atomese: After pre-pruning, found %d sentence pairs for >>%s<<\n",
		nfound, wrd);

	// Avoid null-pointer deref.
	if (0 == nfound)
		return make_zeroary_node(pool);

	// Unary OR exps not allowed.
	if (2 > nfound)
		return sentex;

	// sentex is a linked list or length 2 or more, of connectors to
	// be or'ed together. Wrap them in an OR exp.
	Exp* orhead = Exp_create(pool);
	orhead->type = OR_type;
	orhead->operand_first = sentex;
	return orhead;
}

// ===============================================================
//
// Return the single expression (ANY+ or ANY-)
static Exp* make_any_conns(Dictionary dict, Pool_desc* pool)
{
	// Create a pair of ANY-links that can connect either left or right.
	Exp* aneg = make_connector_node(dict, pool, "ANY", '-', false);
	Exp* apos = make_connector_node(dict, pool, "ANY", '+', false);

	Local* local = (Local*) (dict->as_server);
	aneg->cost = local->any_default;
	apos->cost = local->any_default;

	return make_or_node(pool, aneg, apos);
}

// ===============================================================

/// Create exprs that consist of a Cartesian product of pairs.
/// Given a word, a lookup is made to find all word-pairs holding
/// that word. This is done by `make_pair_exprs()`, above. Then
/// this is ANDED against itself N times, and the result is returned.
/// The N is the `arity` argument.
///
/// For example, if `make_pair_exprs()` returns `(A+ or B- or C+)`
/// and arity is 3, then this will return `(A+ or B- or C+ or ())
/// and (A+ or B- or C+ or ()) and (A+ or B- or C+ or ())`. When
/// this is exploded into disjuncts, any combination is possible,
/// from size zero to three. That's why its a Cartesian product.
///
/// FYI, this is a work-around for the lack of a commmutative
/// multi-product. What we really want to do here is to have the
/// expression `(@A+ com @B- com @C+)` where `com` is a commutative
/// product.  The `@` sign denotes a multi-connector, so that `@A+`
/// is the same things as `(() or A+ or (A+ & A+) or ...)` and the
/// commutative product allows any of these to commute, i.e. so that
/// disjuncts such as `(A+ & C+ & A+ & C+)` are possible. But we do
/// not have such a commutative multi-product, and so we fake it with
/// a plain cartesian product. The only issue is that this eats up
/// RAM. At least RAM use is linear: it goes as `O(arity)`.  More
/// precisely, as `O(npairs x arity)`.
///
/// There's a problem, here. If the germ is a common word, e.g. 'the'
/// or perhaps punctuation, participating in 10K pairs, or more, then
/// the number of disjuncts created from the cartesian product becomes
/// npairs raised to power of arity, so, trillions or more. This won't
/// work, so we need to apply expression-pruning, first. The current
/// generic expression pruning is not powerful enough to cut this down
/// to size. So, instead, we pre-prune, in get_sent_pair_exprs(), and
/// work *only* with the words in the current sentence.  These are the
/// words passed in through `sent_words`. This is the local context.
/// If it is empty, then no pre-pruning is done.
///
/// The expressions will be created in the given Exp_pool, which should
/// be the Sentence::Exp_pool when pre-pruning (as those expressions are
/// necessarily Sentence-specific).
Exp* make_cart_pairs(Dictionary dict, const Handle& germ,
                     Pool_desc* pool,
                     const HandleSeq& sent_words,
                     int arity, bool with_any)
{
	if (0 >= arity) return nullptr;

	Exp* epr = get_sent_pair_exprs(dict, germ, pool, sent_words);
	if (nullptr == epr) return nullptr;

	// Tack on ANY connectors, if requested.
	if (with_any)
	{
		Exp* ap = make_any_conns(dict, pool);
		or_enchain(pool, epr, ap);
	}
	Exp* optex = make_optional_node(pool, epr);

	// If its 1-dimensional, we are done.
	if (1 == arity) return optex;

	Exp* andhead = nullptr;
	Exp* andtail = nullptr;
	and_enchain_right(pool, andhead, andtail, optex);

	for (int i=1; i< arity; i++)
	{
		Exp* opt = make_optional_node(pool, epr);
		and_enchain_right(pool, andhead, andtail, opt);
	}

	// Could verify that it all multiplies out as expected.
	// lg_assert(arity * size_of_expression(epr) ==
	//           size_of_expression(andhead));

	return andhead;
}

// ===============================================================

/// Create an expression having from one to four copies of
///    (ANY- or ANY+) & ... & (ANY- or ANY+)
///
/// The cost on each connector is the default any-cost from the
/// configuration.
///
/// Multi-connectors are NOT used, due to an issue with cost accounting
/// for multi-connectors. Basically, each additional use does not
/// increase that cost. Yet we do want cost-per-use. See
/// https://github.com/opencog/link-grammar/issues/1351 for details.
///
/// Using this expression is similar to the `any` language, in that it
/// will results in random planar parses. However, since this maxes out
/// at four connectors per word, this is .. different.
///
Exp* make_any_exprs(Dictionary dict, Pool_desc* pool)
{
	// (ANY+ or ANY-) or
	Exp* orhead = nullptr;
	or_enchain(pool, orhead, make_any_conns(dict, pool));

	// ((ANY+ or ANY-) & (ANY+ or ANY-)) or
	Exp* andhead = nullptr;
	Exp* andtail = nullptr;
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	or_enchain(pool, orhead, andhead);

	// three-fold .. ((ANY+ or ANY-) & ...) or
	andhead = nullptr;
	andtail = nullptr;
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	or_enchain(pool, orhead, andhead);

	// four-fold .. ((ANY+ or ANY-) & ...) or
	andhead = nullptr;
	andtail = nullptr;
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	and_enchain_left(pool, andhead, andtail, make_any_conns(dict, pool));
	or_enchain(pool, orhead, andhead);

	// Grand total of one to four connectors:
	// The total cost should be N times single-connector cost.
	return orhead;
}

// ===============================================================
#endif // HAVE_ATOMESE
