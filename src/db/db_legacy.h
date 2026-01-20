#ifndef DB_LEGACY_H
#define DB_LEGACY_H

/*
 * *****************************************************************************
 * *****************************************************************************
 *
 *   TEMPORARY COMPATIBILITY HEADER
 *
 *   DO NOT USE IN NEW CODE
 *
 *   TO BE REMOVED AFTER PHASE 3
 *
 * *****************************************************************************
 * *****************************************************************************
 */

/**
 * @file db_legacy.h
 * @brief TEMPORARY COMPATIBILITY HEADER — REMOVE AFTER PHASE 3.
 * 
 * Provides low-level SQL execution primitives to non-DB code during the 
 * transition to a DB-agnostic architecture.
 * 
 * DO NOT ADD NEW CODE TO THIS HEADER.
 * DO NOT INCLUDE THIS HEADER IN NEW MODULES.
 */

#include "db_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Low-level SQL execution primitives (Temporarily public)
// -----------------------------------------------------------------------------

// Definitions moved to db_api.h

#ifdef __cplusplus
}
#endif

#endif // DB_LEGACY_H
