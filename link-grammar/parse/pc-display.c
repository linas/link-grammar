//
// See wg-display.c
// dot -Txlib file

void draw_pset(dyn_str *pcd, Parse_set * pset)
{
	dyn_strcat(pcd, "foo->bar;\n");
}

void display_parse_choice(extractor_t * pex)
{
	dyn_str *pcd = dyn_str_new();
	dyn_strcat(pcd, "graph {\n");

	draw_pset(pcd, pex->parse_set);
	dyn_strcat(pcd, "}\n");

	FILE* fh = fopen("/tmp/parse-choice.dot", "w");
	char* pcs = dyn_str_take(pcd);
	fputs(pcs, fh);
	free(pcs);
	fclose(fh);
}
