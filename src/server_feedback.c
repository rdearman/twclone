#define _POSIX_C_SOURCE 200809L
#include <jansson.h>
#include <regex.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "server_feedback.h"
#include "server_envelope.h"
#include "server_log.h"
#include "game_db.h"
#include "db/db_api.h"
#include "db/sql_driver.h"

#define FB_TEXT_MAX 12000
#define FB_DIAG_MAX 32768

static const char *const safe_keys[] = {
  "timestamp", "request_id", "correlation_id", "command", "request",
  "response", "response_type", "error_code", "duration_ms", "timeout",
  "context", "client", "client_version", "build_id", "protocol_version",
  "os", "sector_id", "ship_id", "planet_id", "player_id",
  "last_failed_command", "error", "stack_trace", "menu", "workflow",
  "status", "type", "data", "meta", "code", "id",
  "quantity", "amount", "to_sector_id", "from_sector_id", "planet_class",
  "action", "destination", "turns_remaining", "credits", "cargo",
  "shields", "fighters", "hull", NULL
};

static int
key_allowed (const char *key)
{
  char lower[96]; size_t n = strlen (key);
  if (n == 0 || n >= sizeof lower) return 0;
  for (size_t i = 0; i <= n; ++i) lower[i] = (char)tolower ((unsigned char)key[i]);
  static const char *const denied[] = {"pass", "token", "secret", "cookie", "auth", "email", "ip", "path", "private", "chat", "message_body", "github", "database", "connection", NULL};
  for (size_t i=0; denied[i]; ++i) if (strstr(lower, denied[i])) return 0;
  for (size_t i=0; safe_keys[i]; ++i) if (strcmp(lower, safe_keys[i]) == 0) return 1;
  /* Payload field names are retained only when they are protocol-safe scalar
     identifiers. Unknown compound fields are omitted recursively. */
  if (strcmp(lower, "sector") == 0 || strcmp(lower, "ship") == 0 || strcmp(lower, "planet") == 0 || strcmp(lower, "player") == 0 || strcmp(lower, "commodity") == 0 || strcmp(lower, "count") == 0 || strcmp(lower, "level") == 0 || strcmp(lower, "class") == 0 || strcmp(lower, "target") == 0 || strcmp(lower, "port_id") == 0 || strcmp(lower, "ship_id") == 0 || strcmp(lower, "planet_id") == 0 || strcmp(lower, "sector_id") == 0 || strcmp(lower, "player_id") == 0 || strcmp(lower, "port_class") == 0 || strcmp(lower, "quantity") == 0 || strcmp(lower, "turns") == 0) return 1;
  return 0;
}

static char *
sanitize_text (const char *input)
{
  if (!input) return strdup("");
  size_t n = strlen(input); if (n > FB_TEXT_MAX) return NULL;
  regex_t email, ipv4, ipv6, path, bearer, key, jwt, github;
  regcomp(&email, "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}", REG_EXTENDED|REG_ICASE);
  regcomp(&ipv4, "(^|[^0-9])[0-9]{1,3}(\\.[0-9]{1,3}){3}(:[0-9]{1,5})?", REG_EXTENDED);
  regcomp(&ipv6, "(^|[^[:alnum:]])([[:xdigit:]]{0,4}:){2,7}[[:xdigit:]:]{0,4}", REG_EXTENDED|REG_ICASE);
  regcomp(&path, "(/(home|Users|tmp|var|opt|mnt|private|Volumes)/|[A-Za-z]:\\\\)[^[:space:]\"']+", REG_EXTENDED|REG_ICASE);
  regcomp(&bearer, "(bearer[[:space:]]+)[A-Za-z0-9._~+/-]+=*", REG_EXTENDED|REG_ICASE);
  regcomp(&key, "(password|passwd|token|secret|api[_-]?key|authorization)[[:space:]]*[:=][[:space:]]*[^[:space:],;]+", REG_EXTENDED|REG_ICASE);
  regcomp(&jwt, "eyJ[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}", REG_EXTENDED);
  regcomp(&github, "(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})", REG_EXTENDED);
  char *out = calloc(n * 4 + 1, 1); if (!out) goto fail;
  size_t i=0, o=0;
  while (i<n) {
    regmatch_t m; const char *s=input+i; size_t rem=n-i;
    if (regexec(&email,s,1,&m,0)==0 && m.rm_so==0) { memcpy(out+o,"[redacted-email]",16); o+=16; i+=(size_t)m.rm_eo; continue; }
    if (regexec(&ipv4,s,1,&m,0)==0 && m.rm_so==0) { size_t end=(size_t)m.rm_eo; size_t prefix=(n>i && !isdigit((unsigned char)s[0]))?1:0; if(prefix){out[o++]=s[0];} memcpy(out+o,"[redacted-ip]",13); o+=13; i+=end; continue; }
    if (regexec(&ipv6,s,1,&m,0)==0 && m.rm_so==0) { size_t end=(size_t)m.rm_eo; size_t prefix=(n>i && !isxdigit((unsigned char)s[0]) && s[0]!=':')?1:0; if(prefix){out[o++]=s[0];} memcpy(out+o,"[redacted-ip]",13); o+=13; i+=end; continue; }
    if (regexec(&path,s,1,&m,0)==0 && m.rm_so==0) { memcpy(out+o,"[redacted-path]",15); o+=15; i+=(size_t)m.rm_eo; continue; }
    if (regexec(&bearer,s,1,&m,0)==0 && m.rm_so==0) { memcpy(out+o,"[redacted-credential]",21); o+=21; i+=(size_t)m.rm_eo; continue; }
    if (regexec(&key,s,1,&m,0)==0 && m.rm_so==0) { memcpy(out+o,"[redacted-credential]",21); o+=21; i+=(size_t)m.rm_eo; continue; }
    if (regexec(&jwt,s,1,&m,0)==0 && m.rm_so==0) { memcpy(out+o,"[redacted-credential]",21); o+=21; i+=(size_t)m.rm_eo; continue; }
    if (regexec(&github,s,1,&m,0)==0 && m.rm_so==0) { memcpy(out+o,"[redacted-credential]",21); o+=21; i+=(size_t)m.rm_eo; continue; }
    (void)rem; out[o++]=input[i++];
  }
  out[o]=0;
  regfree(&email); regfree(&ipv4); regfree(&ipv6); regfree(&path); regfree(&bearer); regfree(&key); regfree(&jwt); regfree(&github);
  return out;
fail:
  regfree(&email); regfree(&ipv4); regfree(&ipv6); regfree(&path); regfree(&bearer); regfree(&key); regfree(&jwt); regfree(&github);
  return NULL;
}

static int
valid_report_uuid(const char *s)
{
  if(!s || strlen(s)!=36) return 0;
  for(int i=0;i<36;i++) {
    if(i==8||i==13||i==18||i==23) { if(s[i]!='-') return 0; }
    else if(!isxdigit((unsigned char)s[i])) return 0;
  }
  return 1;
}

/* Build the exact content shown by feedback.preview and persisted for the
   asynchronous publisher. Never let the publisher reconstruct public text
   from JSONB, whose key ordering may differ from the preview. */
static int
build_public_issue (const char *kind, const char *subject, const char *desc,
                    const char *expected, const char *actual,
                    const char *steps, const char *diag, const char *client,
                    const char *version, const char *build,
                    const char *protocol, const char *uuid,
                    char **title_out, char **body_out)
{
  const char *client_title = strcmp(client, "python") == 0 ? "Python" : "Godot";
  const char *kind_title = strcmp(kind, "bug") == 0 ? "Bug" : "Suggestion";
  char *title = calloc(256, 1);
  size_t cap = strlen(desc) + strlen(expected) + strlen(actual) + strlen(steps)
    + strlen(diag) + strlen(version) + strlen(build) + strlen(protocol) + 1024;
  char *body = calloc(cap, 1);
  if (!title || !body)
    { free(title); free(body); return -1; }
  /* The protocol caps a subject at 160 bytes, so the complete title fits;
     avoid byte-truncating a UTF-8 code point in the public preview. */
  snprintf(title, 256, "[V3][%s][%s] %s", client_title, kind_title, subject);
  int n = snprintf(body, cap, "## Player description\n%s", desc);
  if (n < 0 || (size_t)n >= cap) goto fail;
  size_t used = (size_t)n;
  if (strcmp(kind, "bug") == 0)
    {
      n = snprintf(body + used, cap - used,
                   "\n\n## Expected behaviour\n%s\n\n## Actual behaviour\n%s",
                   expected, actual);
      if (n < 0 || (size_t)n >= cap - used) goto fail;
      used += (size_t)n;
    }
  if (*steps)
    {
      n = snprintf(body + used, cap - used, "\n\n## Reproduction steps\n%s", steps);
      if (n < 0 || (size_t)n >= cap - used) goto fail;
      used += (size_t)n;
    }
  n = snprintf(body + used, cap - used,
               "\n\n## Versions\n- Client: %s %s\n- Build: %s\n- Protocol: %s\n\n## Sanitised diagnostics\n```json\n%s\n```\n\n<!-- twclone-report-id:%s -->",
               client, version, build, protocol, diag, uuid);
  if (n < 0 || (size_t)n >= cap - used) goto fail;
  *title_out = title; *body_out = body;
  return 0;
fail:
  free(title); free(body); return -1;
}

static json_t *
sanitize_json (const json_t *v, int depth)
{
  if (depth > 8) return NULL;
  if (json_is_object(v)) {
    json_t *o=json_object(); const char *k; json_t *x;
    json_object_foreach((json_t*)v,k,x) {
      if (!key_allowed(k)) continue;
      json_t *clean=sanitize_json(x,depth+1); if (clean) json_object_set_new(o,k,clean);
    }
    return o;
  }
  if (json_is_array(v)) {
    json_t *a=json_array(); size_t i; json_t *x;
    json_array_foreach((json_t*)v,i,x) { if(i>=100) break; json_t *clean=sanitize_json(x,depth+1); if(clean) json_array_append_new(a,clean); }
    return a;
  }
  if (json_is_string(v)) { char *s=sanitize_text(json_string_value(v)); if(!s) return NULL; json_t *r=json_string(s); free(s); return r; }
  if (json_is_integer(v) || json_is_real(v) || json_is_boolean(v) || json_is_null(v)) return json_deep_copy((json_t*)v);
  return NULL;
}

static int
get_string (json_t *data, const char *key, const char **out, size_t max, int required)
{
  json_t *v=json_object_get(data,key);
  if (!v && !required) { *out=""; return 0; }
  if (!json_is_string(v)) return -1;
  *out=json_string_value(v); size_t n=strlen(*out);
  return n<=max && (!required || n>0) ? 0 : -1;
}

static int
query (db_t *db, const char *tmpl, db_bind_t *binds, size_t n, db_res_t **res, db_error_t *err)
{
  char sql[2048];
  if (sql_build(db,tmpl,sql,sizeof sql)!=0) return 0;
  return db_query(db,sql,binds,n,res,err);
}

static int
limit_from_env (const char *name, int fallback)
{
  const char *v=getenv(name); if(!v||!*v) return fallback;
  char *end=NULL; long n=strtol(v,&end,10);
  return end && *end=='\0' && n>0 && n<=INT_MAX ? (int)n : fallback;
}

static int
report_lookup (client_ctx_t *ctx, json_t *root, const char *report_id, int diagnostics)
{
  db_t *db=game_db_get_handle(); if(!db) { send_response_error(ctx,root,ERR_DB,"Database unavailable."); return 0; }
  const char *tmpl=diagnostics
    ? "SELECT diagnostics FROM player_feedback_reports WHERE report_id::text = {1} AND player_id = {2}"
    : "SELECT report_id, publication_state, github_issue_number, github_issue_url, created_at FROM player_feedback_reports WHERE report_id::text = {1} AND player_id = {2}";
  db_res_t *res=NULL; db_error_t err; db_error_clear(&err);
  if(!query(db,tmpl,(db_bind_t[]){db_bind_text(report_id),db_bind_i64(ctx->player_id)},2,&res,&err)) { LOGE("feedback lookup failed: %s",err.message); send_response_error(ctx,root,ERR_DB,"Feedback status unavailable."); return 0; }
  if(!db_res_step(res,&err)) { db_res_finalize(res); send_response_error(ctx,root,ERR_DB_NOT_FOUND,"Feedback report not found."); return 0; }
  json_t *data=json_object();
  if(diagnostics) { const char *raw=db_res_col_text(res,0,&err); json_error_t je; json_t *parsed=raw?json_loads(raw,0,&je):NULL; if(parsed) json_object_set_new(data,"diagnostics",parsed); else json_object_set_new(data,"diagnostics",json_object()); }
  else {
    const char *id=db_res_col_text(res,0,&err), *state=db_res_col_text(res,1,&err), *url=db_res_col_text(res,3,&err), *created=db_res_col_text(res,4,&err);
    char idbuf[64], statebuf[32], urlbuf[512], createdbuf[64];
    snprintf(idbuf,sizeof idbuf,"%s",id?id:""); snprintf(statebuf,sizeof statebuf,"%s",state?state:"pending");
    snprintf(urlbuf,sizeof urlbuf,"%s",url?url:""); snprintf(createdbuf,sizeof createdbuf,"%s",created?created:"");
    json_object_set_new(data,"report_id",json_string(idbuf)); json_object_set_new(data,"state",json_string(statebuf));
    int64_t issue=db_res_col_i64(res,2,&err); if(issue>0) json_object_set_new(data,"github_issue_number",json_integer(issue));
    if(*urlbuf) json_object_set_new(data,"github_issue_url",json_string(urlbuf));
    if(*createdbuf) json_object_set_new(data,"created_at",json_string(createdbuf));
  }
  db_res_finalize(res); SEND_OK_TAKE(ctx,root,diagnostics?"feedback.diagnostics":"feedback.status",&data); return 0;
}

int
cmd_feedback_status(client_ctx_t *ctx, json_t *root)
{
  json_t *d=json_object_get(root,"data"); const char *id=NULL;
  if(!json_is_object(d)||get_string(d,"report_id",&id,64,1)!=0) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"report_id is required."); return 0; }
  return report_lookup(ctx,root,id,0);
}

int
cmd_feedback_diagnostics(client_ctx_t *ctx, json_t *root)
{
  json_t *d=json_object_get(root,"data"); const char *id=NULL;
  if(!json_is_object(d)||get_string(d,"report_id",&id,64,1)!=0) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"report_id is required."); return 0; }
  return report_lookup(ctx,root,id,1);
}

int
cmd_feedback_preview(client_ctx_t *ctx, json_t *root)
{
  (void)ctx;
  json_t *d=json_object_get(root,"data");
  const char *kind=NULL,*subject=NULL,*desc=NULL,*expected=NULL,*actual=NULL,*steps=NULL,*client=NULL,*version=NULL,*build=NULL,*protocol=NULL,*uuid=NULL;
  if(!json_is_object(d)||get_string(d,"report_type",&kind,16,1)||get_string(d,"subject",&subject,160,1)||get_string(d,"description",&desc,FB_TEXT_MAX,1)||get_string(d,"client_name",&client,32,1)||get_string(d,"idempotency_key",&uuid,64,1)||!valid_report_uuid(uuid)) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Feedback preview fields are invalid."); return 0; }
  if(get_string(d,"expected_behavior",&expected,FB_TEXT_MAX,0)||get_string(d,"actual_behavior",&actual,FB_TEXT_MAX,0)||get_string(d,"reproduction_steps",&steps,FB_TEXT_MAX,0)||get_string(d,"client_version",&version,64,0)||get_string(d,"build_id",&build,80,0)||get_string(d,"protocol_version",&protocol,32,0)) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Optional feedback preview fields are invalid."); return 0; }
  if(strcmp(kind,"bug") && strcmp(kind,"suggestion")) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"report_type must be bug or suggestion."); return 0; }
  if(strcmp(client,"python") && strcmp(client,"godot")) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"client_name must be python or godot."); return 0; }
  if(strcmp(kind,"bug")==0 && (!*expected||!*actual)) { send_response_error(ctx,root,ERR_MISSING_FIELD,"Bug preview requires expected and actual behaviour."); return 0; }
  char *ss=sanitize_text(subject),*sd=sanitize_text(desc),*se=sanitize_text(expected),*sa=sanitize_text(actual),*st=sanitize_text(steps),*sv=sanitize_text(version),*sb=sanitize_text(build),*sp=sanitize_text(protocol);
  json_t *clean=sanitize_json(json_object_get(d,"diagnostics"),0); if(!clean)clean=json_object();
  char *diag=json_dumps(clean,JSON_INDENT(2)|JSON_ENSURE_ASCII); json_decref(clean);
  if(!ss||!sd||!se||!sa||!st||!sv||!sb||!sp||!diag||strlen(diag)>FB_DIAG_MAX||strlen(sv)>64||strlen(sb)>80||strlen(sp)>32) { free(ss);free(sd);free(se);free(sa);free(st);free(sv);free(sb);free(sp);free(diag);send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Feedback preview exceeds a size limit.");return 0; }
  char *title=NULL,*body=NULL;
  if(build_public_issue(kind,ss,sd,se,sa,st,diag,client,sv,sb,sp,uuid,&title,&body)!=0) { free(ss);free(sd);free(se);free(sa);free(st);free(sv);free(sb);free(sp);free(diag);send_response_error(ctx,root,ERR_DB,"Preview allocation failed.");return 0; }
  json_t *out=json_object();json_object_set_new(out,"report_id",json_string(uuid));json_object_set_new(out,"title",json_string(title));json_object_set_new(out,"body",json_string(body));
  free(title);free(body);free(ss);free(sd);free(se);free(sa);free(st);free(sv);free(sb);free(sp);free(diag);SEND_OK_TAKE(ctx,root,"feedback.preview",&out);return 0;
}

int
cmd_feedback_submit(client_ctx_t *ctx, json_t *root)
{
  json_t *d=json_object_get(root,"data"); const char *itype=NULL,*subject=NULL,*desc=NULL,*expected=NULL,*actual=NULL,*steps=NULL,*idem=NULL,*client=NULL,*version=NULL,*build=NULL,*protocol=NULL;
  if(!json_is_object(d)||get_string(d,"report_type",&itype,16,1)||get_string(d,"subject",&subject,160,1)||get_string(d,"description",&desc,FB_TEXT_MAX,1)||get_string(d,"idempotency_key",&idem,128,1)||get_string(d,"client_name",&client,32,1)) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Required feedback fields are missing or invalid."); return 0; }
  if(get_string(d,"expected_behavior",&expected,FB_TEXT_MAX,0)||get_string(d,"actual_behavior",&actual,FB_TEXT_MAX,0)||get_string(d,"reproduction_steps",&steps,FB_TEXT_MAX,0)||get_string(d,"client_version",&version,64,0)||get_string(d,"build_id",&build,80,0)||get_string(d,"protocol_version",&protocol,32,0)) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Optional feedback fields are invalid."); return 0; }
  if(strcmp(itype,"bug") && strcmp(itype,"suggestion")) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"report_type must be bug or suggestion."); return 0; }
  if(strcmp(client,"python") && strcmp(client,"godot")) { send_response_error(ctx,root,ERR_INVALID_SCHEMA,"client_name must be python or godot."); return 0; }
  if(strcmp(itype,"bug")==0 && (!*expected || !*actual)) { send_response_error(ctx,root,ERR_MISSING_FIELD,"Bug reports require expected_behavior and actual_behavior."); return 0; }
  json_t *diag=sanitize_json(json_object_get(d,"diagnostics"),0); if(!diag) diag=json_object();
  char *diag_s=json_dumps(diag,JSON_INDENT(2)|JSON_ENSURE_ASCII); json_decref(diag); if(!diag_s||strlen(diag_s)>FB_DIAG_MAX) { free(diag_s); send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Diagnostics exceed the size limit."); return 0; }
  char *safe_subject=sanitize_text(subject), *safe_desc=sanitize_text(desc), *safe_expected=sanitize_text(expected), *safe_actual=sanitize_text(actual), *safe_steps=sanitize_text(steps), *safe_version=sanitize_text(version), *safe_build=sanitize_text(build), *safe_protocol=sanitize_text(protocol);
  if(!safe_subject||!safe_desc||!safe_expected||!safe_actual||!safe_steps||!safe_version||!safe_build||!safe_protocol||strlen(safe_version)>64||strlen(safe_build)>80||strlen(safe_protocol)>32) { free(diag_s); free(safe_subject); free(safe_desc); free(safe_expected); free(safe_actual); free(safe_steps); free(safe_version); free(safe_build); free(safe_protocol); send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Feedback text exceeds the size limit."); return 0; }
  if(strlen(safe_subject)+strlen(safe_desc)+strlen(safe_expected)+strlen(safe_actual)+strlen(safe_steps)>24000) { free(diag_s); free(safe_subject); free(safe_desc); free(safe_expected); free(safe_actual); free(safe_steps); send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Combined feedback text exceeds the size limit."); return 0; }
  char *public_title=NULL,*public_body=NULL;
  if(build_public_issue(itype,safe_subject,safe_desc,safe_expected,safe_actual,safe_steps,diag_s,client,safe_version,safe_build,safe_protocol,idem,&public_title,&public_body)!=0) { free(diag_s);free(safe_subject);free(safe_desc);free(safe_expected);free(safe_actual);free(safe_steps);free(safe_version);free(safe_build);free(safe_protocol);send_response_error(ctx,root,ERR_INVALID_SCHEMA,"Feedback preview could not be prepared.");return 0; }
  db_t *db=game_db_get_handle(); if(!db) { send_response_error(ctx,root,ERR_DB,"Database unavailable."); goto cleanup; }
  db_error_t err; db_error_clear(&err); db_res_t *res=NULL;
  if(!db_tx_begin(db,DB_TX_DEFAULT,&err)) { send_response_error(ctx,root,ERR_DB,"Could not begin feedback transaction."); goto cleanup; }
  const char *global_lock="SELECT pg_advisory_xact_lock(87431,1)";
  if(!query(db,global_lock,NULL,0,&res,&err)||!db_res_step(res,&err)) { if(res)db_res_finalize(res); db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback global rate limit unavailable."); goto cleanup; }
  db_res_finalize(res); res=NULL;
  const char *lock="SELECT player_id FROM players WHERE player_id={1} FOR UPDATE";
  if(!query(db,lock,(db_bind_t[]){db_bind_i64(ctx->player_id)},1,&res,&err)||!db_res_step(res,&err)) { if(res)db_res_finalize(res); db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback owner check failed."); goto cleanup; }
  db_res_finalize(res); res=NULL;
  const char *prior="SELECT report_id::text,publication_state,github_issue_number,github_issue_url FROM player_feedback_reports WHERE player_id={1} AND idempotency_key={2}";
  if(!query(db,prior,(db_bind_t[]){db_bind_i64(ctx->player_id),db_bind_text(idem)},2,&res,&err)) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback retry lookup failed."); goto cleanup; }
  if(db_res_step(res,&err))
    {
      const char *rid=db_res_col_text(res,0,&err), *state=db_res_col_text(res,1,&err), *url=db_res_col_text(res,3,&err);
      char ridbuf[64],statebuf[32],urlbuf[512]; snprintf(ridbuf,sizeof ridbuf,"%s",rid?rid:idem); snprintf(statebuf,sizeof statebuf,"%s",state?state:"pending"); snprintf(urlbuf,sizeof urlbuf,"%s",url?url:""); int64_t issue=db_res_col_i64(res,2,&err);
      db_res_finalize(res); res=NULL;
      if(!db_tx_commit(db,&err)) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback retry confirmation failed."); goto cleanup; }
      json_t *out=json_object(); json_object_set_new(out,"report_id",json_string(ridbuf)); json_object_set_new(out,"state",json_string(statebuf)); json_object_set_new(out,"idempotent",json_true()); if(issue>0)json_object_set_new(out,"github_issue_number",json_integer(issue)); if(*urlbuf)json_object_set_new(out,"github_issue_url",json_string(urlbuf)); SEND_OK_TAKE(ctx,root,"feedback.submit",&out); goto cleanup;
    }
  db_res_finalize(res); res=NULL;
  const char *rate="SELECT COUNT(*) FROM player_feedback_reports WHERE player_id = {1} AND idempotency_key <> {2} AND created_at >= NOW() - INTERVAL '1 hour'";
  if(!query(db,rate,(db_bind_t[]){db_bind_i64(ctx->player_id),db_bind_text(idem)},2,&res,&err)||!db_res_step(res,&err)) { if(res)db_res_finalize(res); db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback limit check failed."); goto cleanup; }
  int64_t count=db_res_col_i64(res,0,&err); db_res_finalize(res); res=NULL; if(count>=limit_from_env("TWCLONE_FEEDBACK_PLAYER_HOURLY",5)) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_RATE_LIMITED,"Feedback limit reached; try again later."); goto cleanup; }
  const char *global_count="SELECT COUNT(*) FROM player_feedback_reports WHERE created_at >= NOW() - INTERVAL '1 hour'";
  if(!query(db,global_count,NULL,0,&res,&err)||!db_res_step(res,&err)) { if(res)db_res_finalize(res); db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback global limit check failed."); goto cleanup; }
  int64_t all_count=db_res_col_i64(res,0,&err); db_res_finalize(res); res=NULL;
  if(all_count>=limit_from_env("TWCLONE_FEEDBACK_GLOBAL_HOURLY",500)) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_RATE_LIMITED,"Feedback service is temporarily busy; try again later."); goto cleanup; }
  if(!valid_report_uuid(idem)) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_INVALID_SCHEMA,"idempotency_key must be a UUID."); goto cleanup; }
  const char *ins="INSERT INTO player_feedback_reports(report_id,player_id,idempotency_key,report_type,subject,description,expected_behavior,actual_behavior,reproduction_steps,diagnostics,client_name,client_version,build_id,protocol_version,public_issue_title,public_issue_body) VALUES({1}::uuid,{2},{3},{4},{5},{6},{7},{8},{9},{10}::jsonb,{11},{12},{13},{14},{15},{16}) ON CONFLICT(player_id,idempotency_key) DO NOTHING RETURNING report_id";
  db_bind_t b[]={db_bind_text(idem),db_bind_i64(ctx->player_id),db_bind_text(idem),db_bind_text(itype),db_bind_text(safe_subject),db_bind_text(safe_desc),db_bind_text(safe_expected),db_bind_text(safe_actual),db_bind_text(safe_steps),db_bind_text(diag_s),db_bind_text(client),db_bind_text(safe_version),db_bind_text(safe_build),db_bind_text(safe_protocol),db_bind_text(public_title),db_bind_text(public_body)};
  int ok=query(db,ins,b,16,&res,&err); int inserted=ok && res && db_res_step(res,&err); if(res)db_res_finalize(res); res=NULL;
  if(!ok) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback could not be saved."); goto cleanup; }
  const char *sel="SELECT report_id::text,publication_state,github_issue_number,github_issue_url FROM player_feedback_reports WHERE player_id={1} AND idempotency_key={2}";
  if(!query(db,sel,(db_bind_t[]){db_bind_i64(ctx->player_id),db_bind_text(idem)},2,&res,&err)||!db_res_step(res,&err)) { if(res)db_res_finalize(res); db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback confirmation failed."); goto cleanup; }
  const char *saved_id=db_res_col_text(res,0,&err), *state=db_res_col_text(res,1,&err), *url=db_res_col_text(res,3,&err); char idbuf[64],statebuf[32],urlbuf[512]; snprintf(idbuf,sizeof idbuf,"%s",saved_id?saved_id:idem); snprintf(statebuf,sizeof statebuf,"%s",state?state:"pending");snprintf(urlbuf,sizeof urlbuf,"%s",url?url:"");int64_t issue=db_res_col_i64(res,2,&err); db_res_finalize(res);
  if(!db_tx_commit(db,&err)) { db_tx_rollback(db,&err); send_response_error(ctx,root,ERR_DB,"Feedback transaction could not be committed."); goto cleanup; }
  json_t *out=json_object(); json_object_set_new(out,"report_id",json_string(idbuf)); json_object_set_new(out,"state",json_string(statebuf)); json_object_set_new(out,"idempotent",json_boolean(!inserted)); if(issue>0)json_object_set_new(out,"github_issue_number",json_integer(issue)); if(*urlbuf)json_object_set_new(out,"github_issue_url",json_string(urlbuf)); SEND_OK_TAKE(ctx,root,"feedback.submit",&out);
cleanup:
  free(diag_s); free(safe_subject); free(safe_desc); free(safe_expected); free(safe_actual); free(safe_steps); free(safe_version); free(safe_build); free(safe_protocol); free(public_title); free(public_body); return 0;
}
