#include "server_bulk.h"
#include "server_envelope.h"
#include "server_loop.h"        // For server_dispatch_command
#include "errors.h"             // For ERR_BAD_REQUEST, etc.
#include "server_log.h"         // For LOGE
#include "server_config.h"      // For g_capabilities


int
cmd_bulk_execute (client_ctx_t *ctx, json_t *root)
{
  // 1. Check commands array in data
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid data for bulk.execute.");
      return 0;
    }
  json_t *commands_array = json_object_get (data, "commands");


  if (!commands_array || !json_is_array (commands_array))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'commands' array for bulk.execute.");
      return 0;
    }
  size_t num_commands = json_array_size (commands_array);

  // 2. Check limits.max_bulk from g_capabilities
  json_t *limits_obj = json_object_get (g_capabilities, "limits");      // g_capabilities is extern from server_config.c
  json_t *max_bulk_json = NULL;
  int max_bulk = 0;


  if (limits_obj)
    {
      max_bulk_json = json_object_get (limits_obj, "max_bulk");
    }
  if (json_is_integer (max_bulk_json))
    {
      max_bulk = (int) json_integer_value (max_bulk_json);
    }
  else
    {
      // Default to a reasonable limit if not found or invalid
      max_bulk = 100;           // Hardcoded fallback
      LOGW
      (
        "cmd_bulk_execute: limits.max_bulk not found or invalid in capabilities, using default: %d",
        max_bulk);
    }

  if (num_commands > (size_t) max_bulk)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   1305,
                                   "Too many commands in bulk.execute request.",
                                   NULL);                                                                       // 1305: Over-limit refusal
      return 0;
    }

  // 3. Prepare for capturing responses
  ctx->captured_envelopes = json_array ();
  ctx->captured_envelopes_valid = 1;
  if (!ctx->captured_envelopes)
    {
      ctx->captured_envelopes_valid = 0;
      send_response_error (ctx,
                           root,
                           1500,
                           "Memory allocation error for bulk response.");
      return 0;
    }

  // 4. Execute each command
  for (size_t i = 0; i < num_commands; i++)
    {
      json_t *cmd_obj = json_array_get (commands_array, i);     // Borrowed reference
      json_t *cmd_obj_copy = NULL;


      if (!cmd_obj || !json_is_object (cmd_obj))
        {
          json_decref (ctx->captured_envelopes);
          ctx->captured_envelopes = NULL;
          ctx->captured_envelopes_valid = 0;
          send_response_error (ctx,
                               root,
                               ERR_BAD_REQUEST,
                               "Malformed command object in bulk request.");
          return 0;
        }

      // We must pass a fresh root object to server_dispatch_command as it may be decref'd
      cmd_obj_copy = json_deep_copy (cmd_obj);
      if (!cmd_obj_copy)
        {
          json_decref (ctx->captured_envelopes);
          ctx->captured_envelopes = NULL;
          ctx->captured_envelopes_valid = 0;
          send_response_error (ctx,
                               root,
                               1500,
                               "Memory allocation error for deep copy of command.");
          return 0;
        }

      server_dispatch_command (ctx, cmd_obj_copy);      // server_dispatch_command will handle decref of cmd_obj_copy if it passes it along to handlers
      json_decref (cmd_obj_copy);

      // If server_dispatch_command results in an error but does not send response (e.g., cmd not found)
      // then we should make sure something is captured.
      // However, current server_dispatch_command (in server_loop.c) does send an error response if command not found.
    }

  // 5. Send final bulk response
  json_t *response_data = json_object ();


  if (!response_data)
    {
      json_decref (ctx->captured_envelopes);
      ctx->captured_envelopes = NULL;
      ctx->captured_envelopes_valid = 0;
      send_response_error (ctx,
                           root,
                           1500,
                           "Memory allocation error for bulk response.");
      return 0;
    }
  json_object_set_new (response_data, "envelopes", ctx->captured_envelopes);    // steals ref

  // Disable capturing BEFORE sending the final bulk response
  ctx->captured_envelopes = NULL;
  ctx->captured_envelopes_valid = 0;

  send_response_ok_take (ctx, root, "bulk.execute", &response_data);

  return 0;
}

