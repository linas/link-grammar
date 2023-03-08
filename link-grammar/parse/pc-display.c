//
// See wg-display.c
// dot -Txlib file

#include "print/print-util.h"

static void pchoice_node(dyn_str *pcd, Parse_choice * pc)
{
	char* wrd = strdupa(pc->md->word_string);
	patch_subscript_mark(wrd);

	char buf[80];
	sprintf(buf, "\"%s %lx\"", wrd, ((uint64_t)pc) & 0xffffff);
	dyn_strcat(pcd, buf);
}

static void draw_pset(dyn_str *, Parse_set *);
static void draw_pset_recursive(dyn_str *, Parse_set *);

static void draw_pchoice(dyn_str *pcd, Parse_choice * pc)
{
	pchoice_node(pcd, pc);

	if (pc->set[0] || pc->set[1])
	{
		dyn_strcat(pcd, " -> { \n");
		draw_pset(pcd, pc->set[0]);
		draw_pset(pcd, pc->set[1]);
		dyn_strcat(pcd, " }");
	}
	dyn_strcat(pcd, ";\n");

	draw_pset_recursive(pcd, pc->set[0]);
	draw_pset_recursive(pcd, pc->set[1]);
}

int nid = 0;

static void draw_pset(dyn_str *pcd, Parse_set * pset)
{
	if (NULL == pset) return;  // Can't ever happen
	if (NULL == pset->first)
	{
		nid ++;
		char buf[80];
		sprintf(buf, "\"%d\"", nid);
		dyn_strcat(pcd, buf);
		return;
	}

	// Horizontal row for parse choices
	dyn_strcat(pcd, "    subgraph HZ {\n");

	// First tell system these are all on same row
	Parse_choice * pc = pset->first;
	dyn_strcat(pcd, "        { rank=same ");
	while (pc)
	{
		pchoice_node(pcd, pc);
		dyn_strcat(pcd, " ");
		pc = pc->next;
	}
	dyn_strcat(pcd, "}\n");

	// Now draw the horizontal list
	dyn_strcat(pcd, "        ");
	pc = pset->first;
	while (pc)
	{
		pchoice_node(pcd, pc);
		pc = pc->next;

		if (pc)
			dyn_strcat(pcd, " -> ");
	}
	dyn_strcat(pcd, ";\n    };\n");
}

static void draw_pset_recursive(dyn_str *pcd, Parse_set * pset)
{
	if (NULL == pset->first) return;

	// Now draw the vertical tree
	dyn_strcat(pcd, "subgraph {\n");
	draw_pset(pcd, pset);

	// dyn_strcat(pcd, "rankdir=\"LR\";\n");
	Parse_choice * pc = pset->first;
	while (pc)
	{
		draw_pchoice(pcd, pc);
		pc = pc->next;
	}
	dyn_strcat(pcd, "}\n");
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
