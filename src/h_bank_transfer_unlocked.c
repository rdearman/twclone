#include <sqlite3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // For malloc, free
#include <jansson.h> // For JSON manipulation
#include <time.h> // For time()

// Assuming these are defined elsewhere or will be provided by context
#include "database.h"
#include "server_log.h"
#include "errors.h" // For SQLITE_NOTFOUND, SQLITE_CONSTRAINT, etc.
#include "common.h" // For UUID_STR_LEN
#include "server_communication.h" // Assuming this is where comm_publish_system_notice_for_player might be if used

// --- Forward Declarations for assumed/corrected helpers ---
// Corrected signature for h_get_account_alert_threshold_unlocked
// Assuming it returns long long and takes owner_type, not a pointer to threshold.
extern long long h_get_account_alert_threshold_unlocked(sqlite3 *db, int account_id, const char *owner_type);
extern int h_get_account_id_unlocked(sqlite3 *db, const char *owner_type, int owner_id, int *account_id_out);
extern int h_create_bank_account_unlocked(sqlite3 *db, const char *owner_type, int owner_id, long long initial_balance, int *account_id_out);
extern int h_deduct_credits_unlocked(sqlite3 *db, int account_id, long long amount, const char *tx_type, const char *tx_group_id, long long *new_balance_out);
extern int h_add_credits_unlocked(sqlite3 *db, int account_id, long long amount, const char *tx_type, const char *tx_group_id, long long *new_balance_out);
extern int db_notice_create(const char *title, const char *body, const char *severity, time_t expires_at); // Existing system notice creation

// --- NEW HELPER: For creating personalized bank alert notices ---
// This helper function creates a system notice but includes context data
// that can be used by a communication module to target specific players
// or filter for relevant notices.
static int h_create_personal_bank_alert_notice(sqlite3 *db, const char *target_owner_type, int target_owner_id, const char *title, const char *body, json_t *transfer_context) {
    if (!db || !target_owner_type || target_owner_id <= 0 || !title || !body) {
        LOGE("h_create_personal_bank_alert_notice: Invalid arguments.");
        return -1;
    }

    json_t *full_context = json_object();
    if (!full_context) {
        LOGE("h_create_personal_bank_alert_notice: OOM creating context.");
        return -1;
    }

    // Add target owner info to the context
    json_object_set_new(full_context, "target_owner_type", json_string(target_owner_type));
    json_object_set_new(full_context, "target_owner_id", json_integer(target_owner_id));

    // Add transfer-specific context
    if (transfer_context) {
        // Deep copy the transfer_context into full_context to avoid decref issues later
        json_t *transfer_context_copy = json_deep_copy(transfer_context);
        if (transfer_context_copy) {
            json_object_update(full_context, transfer_context_copy);
            json_decref(transfer_context_copy); // The update function increments ref, so we decref original copy
        } else {
            LOGE("h_create_personal_bank_alert_notice: OOM copying transfer_context.");
        }
    }

    // Dump context as body for db_notice_create
    char *context_str = json_dumps(full_context, JSON_COMPACT);
    if (!context_str) {
        LOGE("h_create_personal_bank_alert_notice: OOM dumping context JSON.");
        json_decref(full_context);
        return -1;
    }

    // For now, severity will be "info", expires_at will be current time + 7 days
    // The actual delivery will be handled by the subscription/notification system.
    time_t expires_at_ts = time(NULL) + (7 * 24 * 60 * 60); // 7 days from now
    int rc = db_notice_create(title, context_str, "info", expires_at_ts); // Use the original db_notice_create
    free(context_str);
    json_decref(full_context);
    
    if (rc < 0) {
        LOGE("h_create_personal_bank_alert_notice: Failed to create system notice.");
        return -1;
    }
    return 0;
}


/**
 * @brief Transfers credits between two owners.
 * Updated for Issue 376: Now triggers bank alerts if transfer size exceeds defined thresholds.
 *
 * @param db The SQLite database handle.
 * @param from_owner_type The type of the sender's owner (e.g., "player", "corp").
 * @param from_owner_id The ID of the sender's owner.
 * @param to_owner_type The type of the recipient's owner.
 * @param to_owner_id The ID of the recipient's owner.
 * @param amount The amount to transfer.
 * @param tx_type The transaction type (e.g., "TRANSFER").
 * @param tx_group_id An optional group ID for related transactions.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int h_bank_transfer_unlocked (sqlite3 * db,
                              const char *from_owner_type, int from_owner_id,
                              const char *to_owner_type, int to_owner_id,
                              long long amount,
                              const char *tx_type, const char *tx_group_id)
{
  int from_account_id, to_account_id;
  int rc;

  // Get source account ID
  rc = h_get_account_id_unlocked(db, from_owner_type, from_owner_id, &from_account_id);
  if (rc != SQLITE_OK) {
      // If source account doesn't exist, and it's not a system account, it's an error.
      // System accounts might be implicit.
      if (strcmp(from_owner_type, "system") != 0 && strcmp(from_owner_type, "gov") != 0) {
          LOGW("h_bank_transfer_unlocked: Source account %s:%d not found.", from_owner_type, from_owner_id);
          return SQLITE_NOTFOUND;
      }
      // For system/gov, if not found, it implies no balance to deduct from, so treat as insufficient funds
      LOGW("h_bank_transfer_unlocked: Implicit system/gov source account %s:%d not found or created. Treating as insufficient funds.", from_owner_type, from_owner_id);
      return SQLITE_CONSTRAINT; // Special return for insufficient funds
  }

  // Get or create destination account ID
  rc = h_get_account_id_unlocked(db, to_owner_type, to_owner_id, &to_account_id);
  if (rc == SQLITE_NOTFOUND) {
      rc = h_create_bank_account_unlocked(db, to_owner_type, to_owner_id, 0, &to_account_id);
      if (rc != SQLITE_OK) {
          LOGE("h_bank_transfer_unlocked: Failed to create destination account %s:%d: %s",
               to_owner_type, to_owner_id, sqlite3_errmsg(db));
          return rc;
      }
  } else if (rc != SQLITE_OK) {
      LOGE("h_bank_transfer_unlocked: Failed to get destination account %s:%d: %s",
           to_owner_type, to_owner_id, sqlite3_errmsg(db));
      return rc;
  }

  // Deduct from source
  rc = h_deduct_credits_unlocked(db, from_account_id, amount, tx_type, tx_group_id, NULL);
  if (rc != SQLITE_OK) {
      LOGW("h_bank_transfer_unlocked: Failed to deduct %lld from %s:%d (account %d). Error: %d",
           amount, from_owner_type, from_owner_id, from_account_id, rc);
      return rc; // Insufficient funds or other deduction error
  }

  // Add to destination
  rc = h_add_credits_unlocked(db, to_account_id, amount, tx_type, tx_group_id, NULL);
  if (rc != SQLITE_OK) {
      LOGE("h_bank_transfer_unlocked: Failed to add %lld to %s:%d (account %d). This should not happen after successful deduction. Error: %d",
           amount, to_owner_type, to_owner_id, to_account_id, rc);
      // Attempt to refund the source account (critical error recovery)
      h_add_credits_unlocked(db, from_account_id, amount, "REFUND", "TRANSFER_FAILED", NULL);
      return rc;
  }

  /* --------------------------------------------------------------------------
   * Issue 376: Logic for "bank.alerts"
   * Check thresholds and generate notices for large transfers.
   * -------------------------------------------------------------------------- */
  {
      long long from_threshold = 0;
      long long to_threshold = 0;

      // 1. Check Thresholds
      // We retrieve the alert threshold for both accounts. 
      // h_get_account_alert_threshold_unlocked is assumed to return long long and take owner_type.
      from_threshold = h_get_account_alert_threshold_unlocked(db, from_account_id, from_owner_type);
      to_threshold = h_get_account_alert_threshold_unlocked(db, to_account_id, to_owner_type);

      // 2. Evaluate Alert
      // Trigger if amount meets/exceeds threshold, provided threshold is positive.
      int alert_sender   = (from_threshold > 0 && amount >= from_threshold);
      int alert_receiver = (to_threshold > 0 && amount >= to_threshold);

      if (alert_sender || alert_receiver) {
          // Prepare context data common to both notices
          json_t *transfer_context = json_object();
          if (transfer_context) {
              json_object_set_new(transfer_context, "amount", json_integer(amount));
              json_object_set_new(transfer_context, "from_owner_type", json_string(from_owner_type));
              json_object_set_new(transfer_context, "from_owner_id", json_integer(from_owner_id));
              json_object_set_new(transfer_context, "to_owner_type", json_string(to_owner_type));
              json_object_set_new(transfer_context, "to_owner_id", json_integer(to_owner_id));
              json_object_set_new(transfer_context, "tx_type", json_string(tx_type));
              // tx_group_id is optional, only add if not NULL
              if (tx_group_id) {
                  json_object_set_new(transfer_context, "tx_group_id", json_string(tx_group_id));
              }
          } else {
              LOGE("h_bank_transfer_unlocked: OOM creating transfer_context for alert.");
          }


          // 3. Construct and Create Notices
          char title_buffer[128];
          char body_buffer[512]; // Using snprintf for body might be better if complex, else just use context_str

          if (alert_sender) {
              snprintf(title_buffer, sizeof(title_buffer), "Large Transfer Sent (ID: %d)", from_owner_id);
              snprintf(body_buffer, sizeof(body_buffer), "You sent %lld credits to %s:%d.", amount, to_owner_type, to_owner_id);
              h_create_personal_bank_alert_notice(db, from_owner_type, from_owner_id, title_buffer, body_buffer, transfer_context);
          }
          if (alert_receiver) {
              snprintf(title_buffer, sizeof(title_buffer), "Large Transfer Received (ID: %d)", to_owner_id);
              snprintf(body_buffer, sizeof(body_buffer), "You received %lld credits from %s:%d.", amount, from_owner_type, from_owner_id);
              h_create_personal_bank_alert_notice(db, to_owner_type, to_owner_id, title_buffer, body_buffer, transfer_context);
          }
          
          if (transfer_context) {
              json_decref(transfer_context);
          }
      }
  }
  /* -------------------------------------------------------------------------- */

  return SQLITE_OK;
}
