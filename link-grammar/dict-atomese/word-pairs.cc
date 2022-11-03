/*
 * word-pairs.cc
 *
 * Create disjuncts consisting of optionals, from word-pairs.
 *
 * Copyright (c) 2022 Linas Vepstas <linasvepstas@gmail.com>
 */
#ifdef HAVE_ATOMESE

#include <opencog/atomspace/AtomSpace.h>
#include <opencog/atoms/value/FloatValue.h>
#include <opencog/nlp/types/atom_types.h>
#undef STRINGIFY

extern "C" {
#include "../link-includes.h"            // For Dictionary
#include "../dict-common/dict-common.h"  // for Dictionary_s
#include "../dict-common/dict-utils.h"   // for size_of_expression()
#include "../dict-ram/dict-ram.h"
};

#include "local-as.h"

using namespace opencog;

/// Return true if word-pairs for `germ` need to be fetched.
static bool need_pair_fetch(Local* local, const Handle& germ)
{
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
	local->stnp->fetch_incoming_by_type(germ, LIST_LINK);
	local->stnp->barrier();

	size_t cnt = 0;
	HandleSeq rprs = germ->getIncomingSetByType(LIST_LINK);
	for (const Handle& rawpr : rprs)
	{
		local->stnp->fetch_incoming_by_type(rawpr, EVALUATION_LINK);
		cnt++;
	}
	return cnt;
}

/// Return true if there are any word-pairs for `germ`.
/// The will automatically fetch from storage, as needed.
static bool have_pairs(Local* local, const Handle& germ)
{
	if (need_pair_fetch(local, germ))
		fetch_pairs(local, germ);

	const AtomSpacePtr& asp = local->asp;
	const Handle& hpr = local->prp; // (Predicate "word-pair")

	// Are there any pairs in the local AtomSpace?
	// If there's at least one, just return `true`.
	HandleSeq rprs = germ->getIncomingSetByType(LIST_LINK);
	for (const Handle& rawpr : rprs)
	{
		Handle evpr = asp->get_link(EVALUATION_LINK, hpr, rawpr);
		if (evpr)
		{
			// Verify that its an actual word-pair.
			if (evpr->getOutgoingAtom(0) == local->prp)
				return true;
		}
	}

	return false;
}

/// Return true if the given word occurs in some word-pair, else return
/// false. As a side-effect, word-pairs are loaded from storage.
bool pair_boolean_lookup(Dictionary dict, const char *s)
{
	Local* local = (Local*) (dict->as_server);
	Handle wrd = local->asp->add_node(WORD_NODE, s);

	// Are there any pairs that contain this word?
	bool have_word = have_pairs(local, wrd);

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
		if (have_pairs(local, wcl)) return true;
	}

	return have_word;
}

/// Create a list of connectors, one for each available word pair
/// containing the word in the germ. These are simply OR'ed together.
Exp* make_pair_exprs(Dictionary dict, const Handle& germ)
{
	Local* local = (Local*) (dict->as_server);
	const AtomSpacePtr& asp = local->asp;
	Exp* orhead = nullptr;

	const Handle& hpr = local->prp; // (Predicate "pair")

	HandleSeq rprs = germ->getIncomingSetByType(LIST_LINK);
	for (const Handle& rawpr : rprs)
	{
		Handle evpr = asp->get_link(EVALUATION_LINK, hpr, rawpr);
		if (nullptr == evpr) continue;

		double cost = local->pair_default;
		const ValuePtr& mivp = evpr->getValue(local->mikp);
		if (mivp)
		{
			// MI is the second entry in the vector.
			const FloatValuePtr& fmivp = FloatValueCast(mivp);
			double mi = fmivp->value()[local->pair_index];
			cost = (local->pair_scale * mi) + local->pair_offset;
		}
		// If the cost is too high, just skip this.
		if (local->pair_cutoff <= cost)
			continue;

		// Get the cached link-name for this pair.
		const std::string& slnk = cached_linkname(local, rawpr);

		// Direction is easy to determine: its either left or right.
		char cdir = '+';
		if (rawpr->getOutgoingAtom(1) == germ) cdir  = '-';

		// Create the connector
		Exp* eee = make_connector_node(dict,
			dict->Exp_pool, slnk.c_str(), cdir, true);

		eee->cost = cost;

		or_enchain(dict, orhead, eee);
	}

	return orhead;
}

// ===============================================================

/// Create exprs that are cartesian products of ANY links. The
/// corresponding disjuncts will have `arity` number of connectors.
/// If these are used all by themselves, the resulting parses will
/// be random planar graphs; i.e. will be equivalent to the `any`
/// language parses.
Exp* make_any_exprs(Dictionary dict)
{
	// Create a pair of ANY-links that can connect either left or right.
	Exp* aneg = make_connector_node(dict, dict->Exp_pool, "ANY", '-', true);
	Exp* apos = make_connector_node(dict, dict->Exp_pool, "ANY", '+', true);

	Local* local = (Local*) (dict->as_server);
	aneg->cost = local->any_default;
	apos->cost = local->any_default;

	Exp* any = make_or_node(dict->Exp_pool, aneg, apos);
	Exp* optex = make_optional_node(dict->Exp_pool, any);

	return optex;
}

#endif // HAVE_ATOMESE
