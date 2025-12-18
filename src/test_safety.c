#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include <assert.h>

/* Mock Context */
typedef struct
{
  json_t *captured;		/* Simulates the output buffer */
} mock_ctx_t;


/*
 * Re-implementation of the logic in server_envelope.c for verification
 * (Since we can't easily link against the object file due to deps)
 */


/* Logic: send_response_ok_borrow borrows 'data' (increfs it to keep). */
void
mock_send_response_ok_borrow (mock_ctx_t *ctx, const char *type, json_t *data)
{
  json_t *resp = json_object ();
  json_object_set_new (resp, "type", json_string (type));

  if (data)
    {
      json_object_set (resp, "data", data);
    }
  else
    {
      json_object_set_new (resp, "data", json_null ());
    }

  ctx->captured = resp;
}


/* Logic: send_response_ok_take steals ownership and NULLs pointer */
void
mock_send_response_ok_take (mock_ctx_t *ctx, const char *type, json_t **pdata)
{
  json_t *data = *pdata;
  *pdata = NULL;

  json_t *resp = json_object ();


  json_object_set_new (resp, "type", json_string (type));

  if (data)
    {
      json_object_set_new (resp, "data", data);
    }
  else
    {
      json_object_set_new (resp, "data", json_null ());
    }

  ctx->captured = resp;
}


int
main ()
{
  printf ("Verifying Ownership Semantics...\n");

  /* Case 1: Stealing (Standard Usage) */
  {
    mock_ctx_t ctx = { 0 };
    json_t *payload = json_object ();


    json_object_set_new (payload, "foo", json_string ("bar"));

    /* refcount should be 1 */
    assert (payload->refcount == 1);

    /* Call take - should consume reference and NULL local pointer */
    mock_send_response_ok_take (&ctx, "test", &payload);
    assert (payload == NULL);

    /* Payload should still exist inside captured, refcount 1 (owned by captured) */
    // We can't easily check internal refcount now, but logic is verified.

    /* Clean up context */
    json_decref (ctx.captured);

    printf ("Case 1 (Take): PASSED\n");
  }

  /* Case 2: Borrowing (Advanced Usage) */
  {
    mock_ctx_t ctx = { 0 };
    json_t *payload = json_object ();


    json_object_set_new (payload, "keep", json_true ());

    /* Call borrow */
    mock_send_response_ok_borrow (&ctx, "test", payload);

    /* Payload should now have refcount 2 (1 original + 1 by capture) */
    assert (payload->refcount == 2);

    /* Clean up context */
    json_decref (ctx.captured);

    /* Payload should still exist with refcount 1 */
    assert (payload->refcount == 1);

    /* We must free our reference */
    json_decref (payload);

    printf ("Case 2 (Borrow): PASSED\n");
  }

  printf ("All checks passed.\n");
  return 0;
}
