//
// See wg-display.c
// dot -Txlib file

#include "print/print-util.h"

// Append a unique string for each parse choice.
static void pchoice_node(dyn_str *pcd, Parse_choice * pc)
{
	char* wrd = strdupa(pc->md->word_string);
	patch_subscript_mark(wrd);

	char buf[80];
	sprintf(buf, "\"%s %lx\" ", wrd, ((uint64_t)pc) & 0xffffff);
	dyn_strcat(pcd, buf);
}

// #define DRAW_NUL

// Append the main pset name, or null if null.
static void draw_pset_name(dyn_str *pcd, Parse_set * pset, const char* lr)
{
	if (NULL == pset) return;  // Can't ever happen
	if (NULL == pset->first)
	{
#ifdef DRAW_NUL
		char buf[80];
		sprintf(buf, "\"nu%s %lx\" ", lr, ((uint64_t)pset) & 0xffff);
		dyn_strcat(pcd, buf);
#endif
		return;
	}

	pchoice_node(pcd, pset->first);
}

// Draw horizontal set of parse choices
static void draw_pset_horizontal(dyn_str *pcd, Parse_set * pset)
{
	if (pset->first->done) return;
	pset->first->done = true;

	// Only if two or more
	if (NULL == pset->first->next) return;

	// Horizontal row for parse choices
	dyn_strcat(pcd, "    subgraph HZ { rank=same \n");

	dyn_strcat(pcd, "        ");
	Parse_choice * pc = pset->first;
	while (pc)
	{
		pchoice_node(pcd, pc);
		pc = pc->next;

		if (pc)
			dyn_strcat(pcd, " -> ");
	}
	dyn_strcat(pcd, " [label=next]");
	dyn_strcat(pcd, ";\n    };\n");
}

static void draw_pset_recursive(dyn_str *, Parse_set *);

static void draw_pchoice(dyn_str *pcd, Parse_choice * pc)
{
	if (pc->dolr) return;
	pc->dolr = true;

	bool eith = pc->set[0]->first || pc->set[1]->first;
	if (!eith) return;

	// Draw the left and right sides of binary tree.
	dyn_strcat(pcd, "    subgraph LR { ");
	pchoice_node(pcd, pc);
	dyn_strcat(pcd, " -> ");

	bool both = pc->set[0]->first && pc->set[1]->first;
#ifdef DRAW_NUL
	both=true;
#endif

	if (both) dyn_strcat(pcd, "{ rank=same ");
	draw_pset_name(pcd, pc->set[0], "l");
	draw_pset_name(pcd, pc->set[1], "r");
	if (both) dyn_strcat(pcd, "}");
	// dyn_strcat(pcd, " [label=lr]");
	dyn_strcat(pcd, "};\n");

	dyn_strcat(pcd, "    subgraph RECURSE {");
	draw_pset_recursive(pcd, pc->set[0]);
	draw_pset_recursive(pcd, pc->set[1]);
	dyn_strcat(pcd, "};\n");
}

static void draw_pset_recursive(dyn_str *pcd, Parse_set * pset)
{
	if (NULL == pset->first) return;

	// Draw the horizontal part first.
	draw_pset_horizontal(pcd, pset);

	// Draw the binary pairs.
	Parse_choice * pc = pset->first;
	while (pc)
	{
		draw_pchoice(pcd, pc);
		pc = pc->next;
	}
}

void display_parse_choice(extractor_t * pex)
{
	dyn_str *pcd = dyn_str_new();

	dyn_strcat(pcd, "digraph {\n");
	draw_pset_recursive(pcd, pex->parse_set);
	dyn_strcat(pcd, "}\n");

	FILE* fh = fopen("/tmp/parse-choice.dot", "w");
	char* pcs = dyn_str_take(pcd);
	fputs(pcs, fh);
	free(pcs);
	fclose(fh);
}
