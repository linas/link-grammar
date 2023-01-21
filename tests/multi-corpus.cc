/***************************************************************************/
/* Copyright (c) 2014, 2023 Linas Vepstas                                  */
/* All rights reserved                                                     */
/*                                                                         */
/* Use of the link grammar parsing system is subject to the terms of the   */
/* license set forth in the LICENSE file included with this software.      */
/* This license allows free redistribution and use in source and binary    */
/* forms, with or without modification, subject to certain conditions.     */
/*                                                                         */
/***************************************************************************/

// This implements a multi-threaded unit test for parsing a larger corpus.

#include <thread>
#include <vector>
#include <atomic>

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include "link-grammar/link-includes.h"

static std::atomic_int parse_count; // Just to validate that it can parse.

static void parse_one_sent(Dictionary dict, Parse_Options opts, const char *sent_str)
{
	Sentence sent = sentence_create(sent_str, dict);
	if (!sent) {
		fprintf (stderr, "Fatal error: Unable to create parser\n");
		exit(2);
	}
	sentence_split(sent, opts);
	int num_linkages = sentence_parse(sent, opts);
#if 0
	if (num_linkages <= 0) {
		fprintf (stderr, "Fatal error: Unable to parse sentence\n");
		exit(3);
	}
#endif
	if (0 < num_linkages)
	{
		parse_count++;
		if (10 < num_linkages) num_linkages = 10;

		for (int li = 0; li<num_linkages; li++)
		{
			Linkage linkage = linkage_create(li, sent, opts);

			// Short diagram, it wraps.
			char * str = linkage_print_diagram(linkage, true, 50);
			linkage_free_diagram(str);
			str = linkage_print_links_and_domains(linkage);
			linkage_free_links_and_domains(str);
			str = linkage_print_disjuncts(linkage);
			linkage_free_disjuncts(str);
			str = linkage_print_constituent_tree(linkage, SINGLE_LINE);
			linkage_free_constituent_tree_str(str);
			linkage_delete(linkage);
		}
	}
	sentence_delete(sent);
}

static void parse_sents(Dictionary dict, Parse_Options opts,
                        int thread_no, int num_threads,
                        const char ** sents, int nsents, int niter)
{
	for (int j=0; j<niter; j ++)
	{
		for (int i=thread_no+j; i < nsents; i += num_threads)
		{
			parse_one_sent(dict, opts, sents[i]);
		}
	}
}

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "en_US.UTF-8");

	dictionary_set_data_dir(DICTIONARY_DIR "/data");
	Dictionary dicte = dictionary_create_lang("en");
	if (!dicte or !dictr) {
		fprintf (stderr, "Fatal error: Unable to open the dictionary\n");
		exit(1);
	}

	const char *sents[] = {
	};
	int nsents = sizeof(sents) / sizeof(const char *);

	const int n_threads = 10;
	const int niter = 500;
	Parse_Options opts[n_threads];

	printf("Creating %d threads, each parsing %d sentences\n",
		 n_threads, niter);
	std::vector<std::thread> thread_pool;
	for (int i=0; i < n_threads; i++)
	{
		opts[i] = parse_options_create();
		thread_pool.push_back(std::thread(parse_sents, dicte, opts[i],
			i, n_threads, sents, nsents, niter));
	}

	// Wait for all threads to complete
	for (std::thread& t : thread_pool) t.join();
	const int pcnt = parse_count;
	if (0 == pcnt)
	{
		printf("Fatal error: Nothing got parsed\n");
		exit(2);
	}
	printf("Done with multi-threaded parsing (stat: %d full parses)\n", pcnt);


	for (int i=0; i < n_threads; i++)
		parse_options_delete(opts[i]);

	dictionary_delete(dicte);
	return 0;
}
