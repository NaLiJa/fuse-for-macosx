/* Shared controller logic. Platform operations live exclusively in match_compat. */
#include "match.h"
#include "parson.h"
#include "match_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct match_context {
    char service[65], name[24], code[5], instance[33], connection[37], error[32];
    match_peer peers[MATCH_MAX_PEERS], selected;
    char initiator[65];
    uint64_t due, advertised;
    uint32_t generation;
    unsigned socket, sequence;
    uint16_t revision;
    int active, pending, udp, stage, count, completion_pending;
} *context;
#define ctx (*context)
static void copy(char *out, size_t n, const char *s) { snprintf(out, n, "%s", s ? s : ""); }
static void trim_name(char *name) {
    size_t start = 0, length = strlen(name);
    while (start < length && name[start] == ' ')
        start++;
    while (length > start && name[length - 1] == ' ')
        length--;
    memmove(name, name + start, length - start);
    name[length - start] = 0;
}
static void effective_name(char name[24]) {
    if (ctx.name[0])
        copy(name, 24, ctx.name);
    else {
        const char *id = match_compat_device();
        snprintf(name, 24, "%.4s-%.4s-%.4s-%.4s", id, id + 4, id + 8, id + 12);
    }
}
int match_resolve(const char *host, uint32_t *ip) {
    const char *suffix = strstr(host, "@match.spx");
    if (!suffix)
        return 0;
    if (suffix[10] || suffix == host || suffix - host > 32)
        return -1;
    for (const char *p = host; p < suffix; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
              *p == '-'))
            return -1;
    match_compat_lock();
    if (!context) context = calloc(1, sizeof(*context));
    if (!context) { match_compat_unlock(); return -1; }
    if (ctx.active && !match_compat_valid(ctx.socket, ctx.generation)) {
        match_compat_release();
        ctx.active = 0;
    }
    if (ctx.active) {
        int same = strlen(ctx.service) == (size_t)(suffix - host) &&
                   !memcmp(ctx.service, host, suffix - host);
        if (same)
            *ip = SPX_MATCH_IP;
        match_compat_unlock();
        return same ? 1 : -1;
    }
    memset(ctx.service, 0, sizeof(ctx.service));
    memcpy(ctx.service, host, (size_t)(suffix - host));
    ctx.pending = 1;
    *ip = SPX_MATCH_IP;
    match_compat_unlock();
    return 1;
}
static int valid_connection(const char *id) {
    if (!id || strlen(id) != 36)
        return 0;
    for (unsigned i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-')
                return 0;
        } else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f') ||
                     (id[i] >= 'A' && id[i] <= 'F')))
            return 0;
    }
    return 1;
}
static int post(const char *action, const char *target, const char *code) {
    JSON_Value *v = json_value_init_object();
    JSON_Object *o = json_value_get_object(v);
    json_object_set_string(o, "service", ctx.service);
    json_object_set_string(o, "device", match_compat_device());
    if (action) {
        if (!strcmp(action, "connect_to")) {
            json_object_set_string(o, action, target);
            if (code && *code)
                json_object_set_string(o, "join_code", code);
        } else if (!strcmp(action, "poll"))
            json_object_set_string(o, "connection", ctx.connection);
        else {
            json_object_set_boolean(o, action, 1);
            if (ctx.connection[0] && strcmp(action, "leave"))
                json_object_set_string(o, "connection", ctx.connection);
        }
    } else {
        if (ctx.name[0])
            json_object_set_string(o, "name", ctx.name);
        if (ctx.code[0])
            json_object_set_string(o, "code", ctx.code);
        if (ctx.udp)
            json_object_set_string(o, "transport", "udp");
    }
    if (!action || (strcmp(action, "leave") && strcmp(action, "cancel"))) {
        uint32_t ip;
        uint16_t port;
        char address[20];
        if (match_compat_stun(&ip, &port)) {
            copy(ctx.error, sizeof(ctx.error), "STUN failed");
            json_value_free(v);
            return -1;
        }
        snprintf(address, sizeof(address), "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255,
                 (ip >> 8) & 255, ip & 255);
        json_object_dotset_string(o, "address.ip", address);
        json_object_dotset_number(o, "address.port", port);
    }
    const size_t response_capacity = 4096;
    char *response = malloc(response_capacity);
    if (!response) { json_value_free(v); return -1; }
    char *body = json_serialize_to_string(v);
    if (!body) { free(response); json_value_free(v); return -1; }
    unsigned sequence = ++ctx.sequence;
    int status = match_compat_post(body, ctx.instance, sequence, response, response_capacity);
    /* Lobby failures belong to the caller; do not retry registration/refresh.
     * Other control requests retain their lost-response retry. Completion is
     * best effort: a usable tunnel must not wait for a retry. */
    if (status < 0 && action && strcmp(action, "connected"))
        status = match_compat_post(body, ctx.instance, sequence, response, response_capacity);
    json_free_serialized_string(body);
    json_value_free(v);
    if (status < 0) {
        free(response);
        copy(ctx.error, sizeof(ctx.error), "API unavailable");
        return -1;
    }
    v = json_parse_string(response);
    free(response);
    o = json_value_get_object(v);
    if (!o) {
        json_value_free(v);
        copy(ctx.error, sizeof(ctx.error), "Invalid API response");
        return -1;
    }
    const char *error = json_object_get_string(o, "error");
    if (status != 200 || error) {
        const char *message = error ? error : "API error";
        if (error && !strcmp(error, "wrong_code"))
            message = "Wrong code";
        else if (error && !strcmp(error, "peer_busy"))
            message = "Peer is busy";
        else if (error && !strcmp(error, "peer_gone"))
            message = "Peer left";
        else if (error && !strcmp(error, "peer_cancelled"))
            message = "Peer cancelled";
        else if (error && !strcmp(error, "address_changed"))
            message = "Network address changed";
        else if (error && !strcmp(error, "connection_expired"))
            message = "Connection expired";
        else if (error && !strcmp(error, "instance_expired"))
            message = "Registration expired";
        copy(ctx.error, sizeof(ctx.error), message);
        json_value_free(v);
        return -1;
    }
    ctx.error[0] = 0;
    const char *connection = json_object_get_string(o, "connection");
    if (!connection && action && (!strcmp(action, "connect_to") || !strcmp(action, "poll"))) {
        copy(ctx.error, sizeof(ctx.error), "Invalid connection response");
        json_value_free(v);
        return -1;
    }
    if (json_object_get_object(o, "destination")) {
        copy(ctx.error, sizeof(ctx.error), "Hosted servers unsupported");
        json_value_free(v);
        return -1;
    }
    if (connection && (!valid_connection(connection) ||
                       (ctx.connection[0] && strcmp(connection, ctx.connection)))) {
        copy(ctx.error, sizeof(ctx.error), "Invalid connection response");
        json_value_free(v);
        return -1;
    }
    if (connection && !ctx.connection[0]) {
        JSON_Object *peer = json_object_get_object(o, "peer");
        const char *id = json_object_get_string(peer, "device"),
                   *ip = json_object_dotget_string(peer, "address.ip");
        unsigned a, b, c, d;
        double port = json_object_dotget_number(peer, "address.port");
        if (!id || !ip || sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 ||
            c > 255 || d > 255 || port < 1 || port > 65535 || strlen(connection) != 36) {
            copy(ctx.error, sizeof(ctx.error), "Invalid peer address");
            json_value_free(v);
            return -1;
        }
        const char *initiator = json_object_get_string(o, "initiator");
        /* Older APIs omit this additive field; do not invent a pairing role. */
        if (initiator && strcmp(initiator, id) && strcmp(initiator, match_compat_device())) {
            copy(ctx.error, sizeof(ctx.error), "Invalid match initiator");
            json_value_free(v);
            return -1;
        }
        copy(ctx.initiator, sizeof(ctx.initiator), initiator);
        copy(ctx.connection, sizeof(ctx.connection), connection);
        copy(ctx.selected.device, sizeof(ctx.selected.device), id);
        copy(ctx.selected.name, sizeof(ctx.selected.name), json_object_get_string(peer, "name"));
        if (match_compat_start(connection, id, (a << 24) | (b << 16) | (c << 8) | d, (uint16_t)port,
                               ctx.udp)) {
            const char *reason = match_compat_error();
            copy(ctx.error, sizeof(ctx.error), reason && reason[0] ? reason : "Transport failed");
            json_value_free(v);
            return -1;
        }
        ctx.stage = MATCH_CONNECTING;
    } else if (!connection && !action) {
        JSON_Array *list = json_object_get_array(o, "peers");
        if (!list) {
            json_value_free(v);
            copy(ctx.error, sizeof(ctx.error), "Invalid peer list");
            return -1;
        }
        match_peer peers[MATCH_MAX_PEERS] = {0};
        int count = 0;
        /* Preserve occupied slots before filling empty slots; identities never change silently. */
        for (size_t i = 0; i < json_array_get_count(list); i++) {
            JSON_Object *p = json_array_get_object(list, i);
            const char *id = json_object_get_string(p, "device");
            if (!id)
                continue;
            int slot = -1;
            for (int j = 0; j < MATCH_MAX_PEERS; j++)
                if (!strcmp(ctx.peers[j].device, id)) {
                    slot = j;
                    break;
                }
            if (slot >= 0) {
                copy(peers[slot].device, 65, id);
                copy(peers[slot].name, 24, json_object_get_string(p, "name"));
                peers[slot].requires_code = json_object_get_boolean(p, "requires_code") == 1;
            }
        }
        for (size_t i = 0; i < json_array_get_count(list); i++) {
            JSON_Object *p = json_array_get_object(list, i);
            const char *id = json_object_get_string(p, "device");
            if (!id)
                continue;
            int found = 0;
            for (int j = 0; j < MATCH_MAX_PEERS; j++)
                if (!strcmp(peers[j].device, id))
                    found = 1;
            if (found)
                continue;
            for (int j = 0; j < MATCH_MAX_PEERS; j++)
                if (!peers[j].device[0]) {
                    copy(peers[j].device, 65, id);
                    copy(peers[j].name, 24, json_object_get_string(p, "name"));
                    peers[j].requires_code = json_object_get_boolean(p, "requires_code") == 1;
                    break;
                }
        }
        for (int j = 0; j < MATCH_MAX_PEERS; j++)
            if (peers[j].device[0])
                count = j + 1;
        if (memcmp(peers, ctx.peers, sizeof(peers))) {
            memcpy(ctx.peers, peers, sizeof(peers));
            ctx.revision++;
        }
        ctx.count = count;
        ctx.stage = MATCH_WAITING;
        ctx.advertised = match_compat_time();
    }
    json_value_free(v);
    return 0;
}
static void release(void) {
    match_compat_release();
    ctx.active = ctx.pending = ctx.completion_pending = 0;
    ctx.stage = MATCH_CANCELLED;
    ctx.initiator[0] = 0;
    memset(&ctx.selected, 0, sizeof(ctx.selected));
}
void match_reset(void) {
    match_compat_lock();
    if (context) {
        release();
        memset(&ctx, 0, sizeof(ctx));
    }
    match_compat_unlock();
}
void match_command(unsigned command, const match_request *r, match_snapshot *s) {
    match_compat_lock();
    if (!context) context = calloc(1, sizeof(*context));
    if (!context) {
        memset(s, 0, sizeof(*s)); s->stage = MATCH_ERROR;
        copy(s->error, sizeof(s->error), "Out of memory");
        match_compat_unlock(); return;
    }
    if (ctx.active && r->socket != ctx.socket) {
        memset(s, 0, sizeof(*s));
        s->stage = MATCH_ERROR;
        copy(s->error, sizeof(s->error), "Match already active");
        match_compat_unlock();
        return;
    }
    if (command == SPECTRANEXT_CMD_MATCHMAKING_MATCH && r->action) {
        if (ctx.active)
            post(r->action == 1 && ctx.connection[0] ? "cancel" : "leave", NULL, NULL);
        release();
        goto snapshot;
    }
    if (!ctx.active) {
        if (command != SPECTRANEXT_CMD_MATCHMAKING_MATCH || !ctx.pending || !ctx.service[0] ||
            match_compat_claim(r->socket, &ctx.generation, &ctx.udp)) {
            ctx.stage = MATCH_ERROR;
            copy(ctx.error, 32, "Socket unavailable");
            goto snapshot;
        }
        ctx.socket = r->socket;
        ctx.active = 1;
        ctx.pending = 0;
        ctx.stage = MATCH_PREPARING;
        ctx.advertised = 0;
        ctx.sequence = 0;
        ctx.connection[0] = ctx.code[0] = ctx.error[0] = 0;
        ctx.count = 0;
        ctx.initiator[0] = 0;
        memset(&ctx.selected, 0, sizeof(ctx.selected));
        memset(ctx.peers, 0, sizeof(ctx.peers));
        ctx.revision++;
        match_compat_load_name(ctx.name);
        trim_name(ctx.name);
        unsigned char random[16];
        match_compat_random(random, sizeof(random));
        for (unsigned i = 0; i < 16; i++)
            snprintf(ctx.instance + 2 * i, 3, "%02x", random[i]);
        ctx.due = 0;
    }
    if (!match_compat_valid(ctx.socket, ctx.generation)) {
        release();
        goto snapshot;
    }
    if (command == SPECTRANEXT_CMD_MATCHMAKING_MATCH && r->socket != ctx.socket) {
        copy(ctx.error, 32, "Match already active");
        goto snapshot;
    }
    if (command == SPECTRANEXT_CMD_MATCHMAKING_SET_NAME &&
        (ctx.stage == MATCH_WAITING || ctx.stage == MATCH_PREPARING)) {
        char name[24];
        memcpy(name, r->text, 24);
        name[23] = 0;
        trim_name(name);
        int valid = 1;
        for (size_t i = 0; name[i]; i++)
            if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126)
                valid = 0;
        if (!valid || (strcmp(name, ctx.name) && match_compat_save_name(name))) {
            copy(ctx.error, 32, "Name save failed");
            goto snapshot;
        } else {
            copy(ctx.name, 24, name);
            ctx.due = 0;
        }
    }
    if (command == SPECTRANEXT_CMD_MATCHMAKING_CODE &&
        (ctx.stage == MATCH_WAITING || ctx.stage == MATCH_PREPARING)) {
        if (r->action && !ctx.code[0]) {
            unsigned short n;
            match_compat_random(&n, sizeof(n));
            snprintf(ctx.code, 5, "%04u", 1000 + n % 9000);
        } else if (!r->action)
            ctx.code[0] = 0;
        ctx.due = 0;
    }
    if (command == SPECTRANEXT_CMD_MATCHMAKING_CONNECT &&
        (ctx.stage == MATCH_WAITING || ctx.stage == MATCH_PREPARING)) {
        if (r->revision != ctx.revision || r->index >= MATCH_MAX_PEERS ||
            !ctx.peers[r->index].device[0])
            copy(ctx.error, 32, "Peer list changed");
        else {
            char code[24];
            memcpy(code, r->text, 24);
            code[23] = 0;
            post("connect_to", ctx.peers[r->index].device, code);
            ctx.due = match_compat_time() + 1000;
        }
    }
    if (command == SPECTRANEXT_CMD_MATCHMAKING_MATCH && ctx.stage == MATCH_CONNECTING) {
        int ready = match_compat_ready();
        if (ready > 0) {
            ctx.stage = MATCH_CONNECTED;
            ctx.completion_pending = 1;
        } else if (ready < 0) {
            char reason[32];
            copy(reason, sizeof(reason), match_compat_error());
            post("cancel", NULL, NULL);
            release();
            ctx.stage = MATCH_ERROR;
            copy(ctx.error, 32, reason);
        }
    }
    /* After pairing, the transport owns readiness. An HTTPS poll here can
     * hide a completed handshake behind a slow control-plane request. */
    if (command != SPECTRANEXT_CMD_MATCHMAKING_GET_PEER && ctx.active &&
        ctx.stage != MATCH_CONNECTED && ctx.stage != MATCH_CONNECTING &&
        match_compat_time() >= ctx.due) {
        if (post(ctx.connection[0] ? "poll" : NULL, NULL, NULL)) {
            release();
            ctx.stage = MATCH_ERROR;
        }
        ctx.due = match_compat_time() + 1000;
    }
snapshot:
    if (ctx.stage == MATCH_WAITING && ctx.advertised &&
        match_compat_time() - ctx.advertised >= 40000)
        ctx.stage = MATCH_PREPARING;
    memset(s, 0, sizeof(*s));
    s->stage = ctx.stage;
    s->count = ctx.count;
    s->revision = ctx.revision;
    effective_name(s->name);
    copy(s->code, 5, ctx.code);
    copy(s->service, 65, ctx.service);
    copy(s->error, 32, ctx.error);
    s->peer = ctx.selected;
    if (command == SPECTRANEXT_CMD_MATCHMAKING_GET_PEER) {
        if (r->revision == ctx.revision && r->index < MATCH_MAX_PEERS)
            s->peer = ctx.peers[r->index];
        else
            copy(s->error, 32, "Peer list changed");
    }
    match_compat_unlock();
}

/* The controller calls this only after publishing the snapshot/status. Keep
 * HTTP housekeeping on its existing worker, outside the application's CONNECT. */
void match_complete(void) {
    match_compat_lock();
    if (context && ctx.completion_pending) {
        ctx.completion_pending = 0;
        if (ctx.active && ctx.stage == MATCH_CONNECTED &&
            match_compat_valid(ctx.socket, ctx.generation))
            post("connected", NULL, NULL);
    }
    match_compat_unlock();
}

int match_get_string(uint8_t string_id, char *out, uint8_t capacity) {
    if (!capacity) return -1;
    out[0] = 0;
    match_compat_lock();
    const char *value = NULL;
    char name[24];
    if (string_id == SPECTRANEXT_STRING_SERIAL)
        value = match_compat_device();
    else if (string_id == SPECTRANEXT_STRING_MATCH_NAME) {
        match_compat_load_name(name);
        name[23] = 0;
        trim_name(name);
        value = name[0] ? name : match_compat_device();
    } else if (context && ctx.active && ctx.stage == MATCH_CONNECTED &&
               match_compat_connected(ctx.socket, ctx.generation, ctx.udp)) {
        switch (string_id) {
        case SPECTRANEXT_STRING_MATCH_PEER_SERIAL: value = ctx.selected.device; break;
        case SPECTRANEXT_STRING_MATCH_PEER_NAME:
            value = ctx.selected.name[0] ? ctx.selected.name : ctx.selected.device; break;
        case SPECTRANEXT_STRING_MATCH_INITIATOR_SERIAL: value = ctx.initiator; break;
        }
    }
    int result = -1;
    if (value && value[0] && strlen(value) < capacity) {
        memcpy(out, value, strlen(value) + 1);
        result = 0;
    }
    match_compat_unlock();
    return result;
}
