
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aspell.h>


int main()
{

	AspellConfig *config;
	AspellSpeller *speller;

	AspellCanHaveError *spell_err = NULL;

	config = new_aspell_config();
	aspell_config_replace(config, ASPELL_LANG_KEY, "en_US");

	spell_err = new_aspell_speller(aspell->config);
	speller = to_aspell_speller(spell_err);
}

/**
 * Free memory structures used with spell-checker 'chk'
 */
void spellcheck_destroy(void * chk)
{
	struct linkgrammar_aspell *aspell = (struct linkgrammar_aspell *)chk;
	if (aspell)
	{
		delete_aspell_speller(aspell->speller);
		delete_aspell_config(aspell->config);
		free(aspell);
		aspell = NULL;
	}
}

/**
 * Ask the spell-checker if the spelling looks good.
 * Return true if the spelling is good, else false.
 */
bool spellcheck_test(void * chk, const char * word)
{
	int val = 0;
	struct linkgrammar_aspell *aspell = (struct linkgrammar_aspell *)chk;
	if (aspell && aspell->speller)
	{
		/* this can return -1 on failure */
		val = aspell_speller_check(aspell->speller, word, -1);
	}
	return (val == 1);
}

// Despite having a thread-compatible API, it appears that apsell
// is not actually thread-safe. Bummer.
#if HAVE_PTHREAD
static pthread_mutex_t aspell_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* HAVE_PTHREAD */

int spellcheck_suggest(void * chk, char ***sug, const char * word)
{
	struct linkgrammar_aspell *aspell = (struct linkgrammar_aspell *)chk;
	if (!sug)
	{
		prt_error("Error: spellcheck_suggest: Corrupt pointer.\n");
		return 0;
	}

	if (aspell && aspell->speller)
	{
		const AspellWordList *list = NULL;
		AspellStringEnumeration *elem = NULL;
		const char *aword = NULL;
		unsigned int size, i;
		char **array = NULL;

		pthread_mutex_lock(&aspell_lock);
		list = aspell_speller_suggest(aspell->speller, word, -1);
		elem = aspell_word_list_elements(list);
		size = aspell_word_list_size(list);

		/* allocate an array of char* for returning back to link-parser */
		array = (char **)malloc(sizeof(char *) * size);
		if (!array)
		{
			prt_error("Error: spellcheck_suggest: Out of memory.\n");
			delete_aspell_string_enumeration(elem);
			pthread_mutex_unlock(&aspell_lock);
			return 0;
		}

		i = 0;
		while ((aword = aspell_string_enumeration_next(elem)) != NULL)
		{
			array[i++] = strdup(aword);
		}
		delete_aspell_string_enumeration(elem);
		*sug = array;
		pthread_mutex_unlock(&aspell_lock);
		return size;
	}
	return 0;
}

void spellcheck_free_suggest(void *chk, char **sug, int size)
{
	for (int i = 0; i < size; ++i) free(sug[i]);
	free(sug);
}

#endif /* #ifdef HAVE_ASPELL */
