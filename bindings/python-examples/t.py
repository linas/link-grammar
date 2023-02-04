#!/usr/bin/env python3
# coding: utf8
"""Python3 link-grammar test script"""

import sys, os, re
import locale
import unittest

# assertRaisesRegexp and assertRegexpMatches have been renamed in
# unittest for python 3, but not in python 2 (at least yet).
if hasattr(unittest.TestCase, 'assertRaisesRegex'):
    unittest.TestCase.assertRaisesRegexp = unittest.TestCase.assertRaisesRegex
    unittest.TestCase.assertRegexpMatches = unittest.TestCase.assertRegex

import lg_testutils # Found in the same directory of this test script

# Show information on this program run
print('Running by:', sys.executable)
print('Running {} in:'.format(sys.argv[0]), os.getcwd())
for v in 'PYTHONPATH', 'srcdir', 'LINK_GRAMMAR_DATA':
    print('{}={}'.format(v, os.environ.get(v)))
#===

from linkgrammar import (Sentence, Linkage, ParseOptions, Link, Dictionary,
                         LG_Error, LG_DictionaryError, LG_TimerExhausted,
                         Clinkgrammar as clg)

print(clg.linkgrammar_get_configuration())
NO_SQLITE_ERROR = ''
# USE_SQLITE is currently not used in the MSVC build.
if re.search(r'_MSC_FULL_VER', clg.linkgrammar_get_configuration()) and \
   not re.search(r'USE_SQLITE', clg.linkgrammar_get_configuration()):
    NO_SQLITE_ERROR = 'Library is not configures with SQLite support'

NOT_COMPILED_WITH_PCRE2 = ''
if not re.search(r'HAVE_PCRE2_H', clg.linkgrammar_get_configuration()):
   NOT_COMPILED_WITH_PCRE2 = 'Library not configured with PCRE2 support'

# Show the location and version of the bindings modules
for imported_module in 'linkgrammar$', 'clinkgrammar', '_clinkgrammar', 'lg_testutils':
    module_found = False
    for module in sys.modules:
        if re.search(r'^(linkgrammar\.)?' + imported_module, module):
            print("Using", sys.modules[module], end='')
            if hasattr(sys.modules[module], '__version__'):
                print(' version', sys.modules[module].__version__, end='')
            print()
            module_found = True
    if not module_found:
        print("Warning: Module", imported_module, "not loaded.")

sys.stdout.flush()
#===

def setUpModule():
    unittest.TestCase.maxDiff = None

    datadir = os.getenv("LINK_GRAMMAR_DATA", "")
    if datadir:
        clg.dictionary_set_data_dir(datadir)

    clg.test_data_srcdir = os.getenv("srcdir", os.path.dirname(sys.argv[0]))
    if clg.test_data_srcdir:
        clg.test_data_srcdir += "/"

# The tests are run in alphabetical order....

class IWordPositionTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.d_en = Dictionary(lang='en')

    @classmethod
    def tearDownClass(cls):
        del cls.d_en

    def test_en_spell_word_positions(self):
        po = ParseOptions(spell_guess=99)
        if po.spell_guess == 0:
            raise unittest.SkipTest("Library is not configured with spell guess")
        linkage_testfile(self, self.d_en, po, 'pos-spell')


# Tests are run in alphabetical order; do the language tests last.



def sm(s):
    """
    Naive '.' replacement by SUBSCRIPT_MARK (enough for the tests here).
    """
    SUBSCRIPT_MARK = '\3'
    return s.replace('.', SUBSCRIPT_MARK)



#############################################################################

# Note on the linkage order of next(linkage):
# tests.py uses add_eqcost_linkage_order(), which sorts linkages with
# identical metrics according to their diagram character values. Thus the
# order of linkages with the identical metrics doesn't depend on their
# linkage order of the LG library, which may vary across releases.

def linkage_testfile(self, lgdict, popt, desc=''):
    """
    Reads sentences and their corresponding
    linkage diagrams / constituent printings.
    """
    self.__class__.longMessage = True
    if desc != '':
        desc = desc + '-'
    testfile = clg.test_data_srcdir + "parses-" + desc + clg.dictionary_get_lang(lgdict._obj) + ".txt"
    diagram = None
    constituents = None
    wordpos = None
    sent = None
    lineno = 0
    last_opcode = None
    self.lcnt = 0

    def getwordpos(lkg):
        words_char = []
        words_byte = []
        # self.lcnt += 1
        # print("duuude in py testcsase its" + str(self.lcnt), flush=True)
        for wi, w in enumerate(lkg.words()):
            # print ("diiide its "+str(wi))
            # print ("diiide its "+str(w))
            words_char.append(w + str((int(linkage.word_char_start(wi)), int(linkage.word_char_end(wi)))))
            words_byte.append(w + str((int(linkage.word_byte_start(wi)), int(linkage.word_byte_end(wi)))))
        return ' '.join(words_char) + '\n' + ' '.join(words_byte) + '\n'

    # Function code and file format sanity check
    def validate_opcode(opcode):
        if opcode != ord('O'):
            self.assertFalse(diagram, "at {}:{}: Unfinished diagram entry".format(testfile, lineno))
        if opcode != ord('C'):
            self.assertFalse(constituents, "at {}:{}: Unfinished constituents entry".format(testfile, lineno))
        if opcode != ord('P'):
            self.assertFalse(wordpos, "at {}:{}: Unfinished word-position entry".format(testfile, lineno))

    with open(testfile, 'rb') as _:
        parses = _.readlines()

    for line in parses:
        lineno += 1
        line = line.decode('utf-8')

        validate_opcode(ord(line[0])) # Use ord() for python2/3 compatibility
        if line[0] in 'INOCP':
            last_opcode = line[0]

        # Lines starting with I are the input sentences
        if line[0] == 'I':
            sent = line[1:].rstrip('\r\n') # Strip whitespace before RIGHT-WALL (for P)
            diagram = ""
            constituents = ""
            wordpos = ""
            if popt.verbosity > 1:
                print('Sentence:', sent)
            linkages = Sentence(sent, lgdict, popt).parse()
            linkage = next(linkages, None)
            print("duuuuuuuuuuuuuuuuuuuuude IIIIIIIIIIIIIIIIIIII", flush=True)

        # Generate the next linkage of the last input sentence
        elif line[0] == 'N':
            diagram = ""
            constituents = ""
            wordpos = ""
            linkage = next(linkages, None)
            print("duuuuuuuuuuuuuuuuuuuuude NNNNNNNNNNNNNN", flush=True)
            self.assertTrue(linkage, "at {}:{}: Sentence has too few linkages".format(testfile, lineno))

        # Lines starting with O are the parse diagram
        # It ends with an empty line
        elif line[0] == 'O':
            diagram += line[1:]
            if line[1] == '\n':
                if diagram == 'C\nC\n':
                    self.assertFalse(linkage)
                    diagram = None
                elif len(diagram) > 2:
                    self.assertTrue(linkage, "at {}:{}: Sentence has no linkages".format(testfile, lineno))
                    self.assertEqual(linkage.diagram(), diagram, "at {}:{}".format(testfile, lineno))
                    diagram = None

        # Lines starting with C are the constituent output (type 1)
        # It ends with an empty line
        elif line[0] == 'C':
            if line[1] == '\n' and len(constituents) > 1:
                self.assertEqual(linkage.constituent_tree(), constituents, "at {}:{}".format(testfile, lineno))
                constituents = None
            else:
                constituents += line[1:]

        # Lines starting with P contain word positions "word(start, end) ... "
        # The first P line contains character positions
        # The second P line contains byte positions
        # It ends with an empty line
        elif line[0] == 'P':
            print("duuuuuuuuuuuuuuuuuuuuude start PPPPPPPP line", flush=True)
            if line[1] == '\n' and len(wordpos) > 1:
                # Spell guesses may vary between spell packages. We assume
                # here that those that we test here always exist (and thus know
                # their relative order) and skip the rest.
                print("duuuuuuuuuuuuuuuuuuuuude PPPPPPPP ok go", flush=True)
                if '~' in wordpos or '&' in wordpos:
                    print("duuuude desired wordpos=" + str(wordpos), flush=True)
                    while getwordpos(linkage) != wordpos:
                        cpo = getwordpos(linkage)
                        print("duuude wanted=" + str(wordpos), flush=True)
                        print("duuude goat=" + str(cpo) + "\n", flush=True)
                        linkage = next(linkages, None)
                        cpo = getwordpos(linkage)
                        if cpo == wordpos:
                            print("duuude yayyyyyaaaayyyaaaayyyyy=" + str(cpo), flush=True)
                    print("duuude done with LOOOOOOOOOOOOOOOOOOOOP", flush=True)

                self.assertEqual(getwordpos(linkage), wordpos, "at {}:{}".format(testfile, lineno))
                print("duuude deone with PPPPPPPPPP go check", flush=True)
                wordpos = None
            else:
                wordpos += line[1:]
            print("duuude deone with PPPPPPPPPP line totally", flush=True)

        # Lines starting with "-" contain a Parse Option
        elif line[0] == '-':
            exec('popt.' + line[1:]) in {}, locals()

        elif line[0] in '%\r\n':
            pass
        else:
            self.fail('\nTest file "{}": Invalid opcode "{}" (ord={})'.format(testfile, line[0], ord(line[0])))

    self.assertIsNotNone(last_opcode, "Missing opcode in " + testfile)
    self.assertIn(last_opcode, 'OCP', "Missing result comparison in " + testfile)

def warning(*msg):
    progname = os.path.basename(sys.argv[0])
    print("{}: Warning:".format(progname), *msg, file=sys.stderr)

import tempfile

class divert_start(object):
    """ Output diversion. """
    def __init__(self, fd):
        """ Divert a file descriptor.
        The created object is used for restoring the original file descriptor.
        """
        self.fd = fd
        self.savedfd = os.dup(fd)
        (newfd, self.filename) = tempfile.mkstemp(text=False)
        os.dup2(newfd, fd)
        os.close(newfd)

    def divert_end(self):
        """ Restore a previous diversion and return its content. """
        if not self.filename:
            return ""
        os.lseek(self.fd, os.SEEK_SET, 0)
        content = os.read(self.fd, 1024) # 1024 is more than needed
        os.dup2(self.savedfd, self.fd)
        os.close(self.savedfd)
        os.unlink(self.filename)
        self.filename = None
        return content

    __del__ = divert_end


# Decorate Sentence.parse with eqcost_sorted_parse.
lg_testutils.add_eqcost_linkage_order(Sentence)

# For testing development branches, it may be sometimes useful to use the
# "test", "debug" and "verbosity" options. The following allows to specify them
# as "tests.py" arguments, interleaved with standard "unittest" arguments.

for i, arg in enumerate(sys.argv):
    debug = sys.argv.pop(i)[7:] if arg.startswith('-debug' + '=') else ''
for i, arg in enumerate(sys.argv):
    test = sys.argv.pop(i)[6:] if arg.startswith('-test' + '=') else ''
for i, arg in enumerate(sys.argv):
    verbosity = int(sys.argv.pop(i)[11:]) if arg.startswith('-verbosity' + '=') else ''
if (test or debug or verbosity):
    ParseOptions = lg_testutils.add_test_option(ParseOptions, test, debug, verbosity)

unittest.main()
