//
// See wg-display.c
// dot -Txlib file

static void pchoice_node(dyn_str *pcd, Parse_choice * pc)
{
	char buf[80];
	sprintf(buf, "\"%s %d %d\"", pc->md->word_string, pc->l_id, pc->r_id);
	dyn_strcat(pcd, buf);
}

static void draw_pset(dyn_str *, Parse_set *);

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
}

static void draw_pset(dyn_str *pcd, Parse_set * pset)
{
	if (!pset) return;

	Parse_choice * pc = pset->first;
	dyn_strcat(pcd, "{ rank=same ");
	while (pc)
	{
		pchoice_node(pcd, pc);
		dyn_strcat(pcd, " ");
		pc = pc->next;
	}
	dyn_strcat(pcd, "}\n");

	pc = pset->first;
	while (pc)
	{
		draw_pchoice(pcd, pc);

		if (pc->next)
		{
			pchoice_node(pcd, pc);
			dyn_strcat(pcd, " -> ");
			pchoice_node(pcd, pc->next);
			dyn_strcat(pcd, ";\n");
		}
		pc = pc->next;
	}
}

void display_parse_choice(extractor_t * pex)
{
	dyn_str *pcd = dyn_str_new();
	dyn_strcat(pcd, "digraph {\n");
	// dyn_strcat(pcd, "rankdir=\"LR\";\n");

	draw_pset(pcd, pex->parse_set);
	dyn_strcat(pcd, "}\n");

	FILE* fh = fopen("/tmp/parse-choice.dot", "w");
	char* pcs = dyn_str_take(pcd);
	fputs(pcs, fh);
	free(pcs);
	fclose(fh);
}
