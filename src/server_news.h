#ifndef SERVER_NEWS_H
#define SERVER_NEWS_H
#include <jansson.h>
#include "common.h"             // For client_ctx_t
int news_post (const char *body, const char *category, int author_id);
int cmd_news_get_feed (client_ctx_t *ctx, json_t *root);
int cmd_news_mark_feed_read (client_ctx_t *ctx, json_t *root);
int cmd_news_read (client_ctx_t *ctx, json_t *root);
#endif // SERVER_NEWS_H
