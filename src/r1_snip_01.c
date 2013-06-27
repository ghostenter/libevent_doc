#define EVENT_LOG_DEBUG     0
#define EVENT_LOG_MSG       1
#define EVENT_LOG_WARN      2
#define EVENT_LOG_ERR       3


/* Deprecated: see note at the end of this section */
#define _EVENT_LOG_DEBUG    EVENT_LOG_DEBUG
#define _EVENT_LOG_MSG      EVENT_LOG_MSG
#define _EVENT_LOG_WARN     EVENT_LOG_WARN
#define _EVENT_LOG_ERR      EVENT_LOG_ERR

typedef void (*event_log_cb)(int severity, const char *msg);

void event_set_log_callback(event_log_cb cb);

