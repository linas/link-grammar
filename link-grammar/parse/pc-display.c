//
// See wg-display.c
// dot -Txlib file

static void draw_pchoice(dyn_str *pcd, Parse_choice * pc)
{
}

static int pset_id = 0;

static void draw_pset(dyn_str *pcd, Parse_set * pset)
{
	dyn_strcat(pcd, "foo->bar;\n");
	Parse_choice * pc = pset->first;
	while (pc)
	{
		pset_id ++;
		char buf[80];
		sprintf(buf, "\"%s %d\"", pc->md->word_string, pset_id);
		dyn_strcat(pcd, buf);
		dyn_strcat(pcd, ";\n");

		draw_pchoice(pcd, pc);
		pc = pc->next;
	}
}

void display_parse_choice(extractor_t * pex)
{
	dyn_str *pcd = dyn_str_new();
	dyn_strcat(pcd, "digraph {\n");

	draw_pset(pcd, pex->parse_set);
	dyn_strcat(pcd, "}\n");

	FILE* fh = fopen("/tmp/parse-choice.dot", "w");
	char* pcs = dyn_str_take(pcd);
	fputs(pcs, fh);
	free(pcs);
	fclose(fh);
}
