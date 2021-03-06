#include <bson.h>
#include "TestSuite.h"
#include "json-test.h"
#include "corpus-test.h"

#ifndef JSON_DIR
#define JSON_DIR "tests/json"
#endif


typedef struct {
   const char *scenario;
   const char *test;
} skipped_corpus_test_t;

skipped_corpus_test_t SKIPPED_CORPUS_TESTS[] = {
   /* CDRIVER-1879, can't make Code with embedded NIL */
   {"Javascript Code", "Embedded nulls"},
   {"Javascript Code with Scope",
    "Unicode and embedded null in code string, empty scope"},
   {0}};


static void
compare_data (const uint8_t *a,
              uint32_t a_len,
              const uint8_t *b,
              uint32_t b_len)
{
   bson_string_t *a_str;
   bson_string_t *b_str;
   uint32_t i;

   if (a_len != b_len || memcmp (a, b, (size_t) a_len)) {
      a_str = bson_string_new (NULL);
      for (i = 0; i < a_len; i++) {
         bson_string_append_printf (a_str, "%02X", (int) a[i]);
      }

      b_str = bson_string_new (NULL);
      for (i = 0; i < b_len; i++) {
         bson_string_append_printf (b_str, "%02X", (int) b[i]);
      }

      fprintf (stderr,
               "unequal data of length %d and %d:\n%s\n%s\n",
               a_len,
               b_len,
               a_str->str,
               b_str->str);

      abort ();
   }
}


/*
See:
github.com/mongodb/specifications/blob/master/source/bson-corpus/bson-corpus.rst
#testing-validity

We don't do the "B->cB" or "cB->cB" steps since we have no concept of decoding
BSON to objects as other drivers like Python do.
 */
static void
test_bson_corpus (test_bson_type_t *test)
{
   skipped_corpus_test_t *skip;
   bson_t B;
   bson_t cB;
   bson_t *decode_E;
   bson_t *decode_cE;
   bson_error_t error;

   BSON_ASSERT (test->B);
   BSON_ASSERT (test->cB);

   for (skip = SKIPPED_CORPUS_TESTS; skip->scenario != NULL; skip++) {
      if (!strcmp (skip->scenario, test->scenario_description) &&
          !strcmp (skip->test, test->test_description)) {
         if (test_suite_debug_output ()) {
            printf ("      SKIP\n");
            fflush (stdout);
         }

         return;
      }
   }

   ASSERT (bson_init_static (&B, test->B, test->B_len));
   ASSERT (bson_init_static (&cB, test->cB, test->cB_len));

   if (test->E) {
      decode_E =
         bson_new_from_json ((const uint8_t *) test->E, test->E_len, &error);

      ASSERT_OR_PRINT (decode_E, error);

      decode_cE =
         bson_new_from_json ((const uint8_t *) test->cE, test->cE_len, &error);

      ASSERT_OR_PRINT (decode_cE, error);

      /* B->cE */
      ASSERT_CMPJSON (bson_as_extended_json (&B, NULL), test->cE);

      /* E->cE */
      ASSERT_CMPJSON (bson_as_extended_json (decode_E, NULL), test->cE);

      if (test->B != test->cB) {
         /* cB->cE */
         ASSERT_CMPJSON (bson_as_extended_json (&cB, NULL), test->cE);
      }

      if (test->E != test->cE) {
         /* cE->cE */
         ASSERT_CMPJSON (bson_as_extended_json (decode_cE, NULL), test->cE);
      }

      if (!test->lossy) {
         /* E->cB */
         compare_data (
            bson_get_data (decode_E), decode_E->len, test->cB, test->cB_len);

         if (test->E != test->cE) {
            /* cE->cB */
            compare_data (bson_get_data (decode_cE),
                          decode_cE->len,
                          test->cB,
                          test->cB_len);
         }
      }

      bson_destroy (decode_E);
      bson_destroy (decode_cE);
   } else {
      /* tests of deprecated types: just make sure we can decode it */
      ASSERT (bson_validate (&B, BSON_VALIDATE_UTF8, 0));
   }

   bson_destroy (&B);
   bson_destroy (&cB);
}


static void
test_bson_corpus_cb (bson_t *scenario)
{
   bson_iter_t iter;
   bson_iter_t inner_iter;
   bson_t invalid_bson;

   /* test valid BSON and Extended JSON */
   corpus_test (scenario, test_bson_corpus);

   /* test invalid BSON */
   if (bson_iter_init_find (&iter, scenario, "decodeErrors")) {
      bson_iter_recurse (&iter, &inner_iter);
      while (bson_iter_next (&inner_iter)) {
         bson_iter_t test;
         const char *description;
         uint8_t *bson_str = NULL;
         uint32_t bson_str_len = 0;

         bson_iter_recurse (&inner_iter, &test);
         while (bson_iter_next (&test)) {
            if (!strcmp (bson_iter_key (&test), "description")) {
               description = bson_iter_utf8 (&test, NULL);
               corpus_test_print_description (description);
            }

            if (!strcmp (bson_iter_key (&test), "bson")) {
               bson_str = corpus_test_unhexlify (&test, &bson_str_len);
            }
         }

         ASSERT (bson_str);
         ASSERT (!bson_init_static (&invalid_bson, bson_str, bson_str_len) ||
                 bson_empty (&invalid_bson) ||
                 !bson_as_extended_json (&invalid_bson, NULL));

         bson_free (bson_str);
      }
   }
}

void
test_bson_corpus_install (TestSuite *suite)
{
   install_json_test_suite (
      suite, JSON_DIR "/bson_corpus", test_bson_corpus_cb);
}
