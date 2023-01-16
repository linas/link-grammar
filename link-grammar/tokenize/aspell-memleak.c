
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aspell.h>

// Compile with
// cc aspell-memleak.c -laspell

int main()
{

	AspellConfig *config;
	AspellSpeller *speller;

	AspellCanHaveError *spell_err = NULL;

	config = new_aspell_config();
	aspell_config_replace(config, "lang", "en_US");

	spell_err = new_aspell_speller(config);
	speller = to_aspell_speller(spell_err);

	size_t k=0;
	char* word = "asdf";
	for (int l=0; l<1000000; l++)
	{
		/* Returns 1 is the word is in dict. */
		int found = aspell_speller_check(speller, word, -1);
		// printf("duude badword=%d\n", found);
		const AspellWordList *list = NULL;
		AspellStringEnumeration *elem = NULL;
		const char *aword = NULL;
		unsigned int size;
		list = aspell_speller_suggest(speller, word, -1);
		elem = aspell_word_list_elements(list);
		size = aspell_word_list_size(list);

		while ((aword = aspell_string_enumeration_next(elem)) != NULL)
		{
			// printf("Spell suggesion: %s\n", aword);
			k++;
		}
		delete_aspell_string_enumeration(elem);

		if (0 == l%20000)
		{
			printf("Loop count=%d spell suggests=%lu\n", l, k);
			malloc_stats();
		}
	}

	delete_aspell_speller(speller);
	delete_aspell_config(config);
}
