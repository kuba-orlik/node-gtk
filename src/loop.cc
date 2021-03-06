#include <glib.h>
#include <uv.h>
#include <nan.h>
#include <v8.h>

#include "debug.h"
#include "gi.h"
#include "loop.h"
#include "util.h"

/* Integration for the GLib main loop and uv's main loop */

/* The way that this works is that we take uv's loop and nest it inside GLib's
 * mainloop, since nesting GLib inside uv seems to be fairly impossible until
 * either uv allows external sources to drive prepare/check, or until GLib
 * exposes an epoll fd to wait on... */

using namespace v8;

namespace GNodeJS {

static Nan::Persistent<Array> loopStack(Nan::New<Array> ());

struct uv_loop_source {
    GSource source;
    uv_loop_t *loop;
};

static gboolean uv_loop_source_prepare (GSource *base, int *timeout) {
    struct uv_loop_source *source = (struct uv_loop_source *) base;
    uv_update_time (source->loop);

    bool loop_alive = uv_loop_alive (source->loop);

    /* If the loop is dead, we can simply sleep forever until a GTK+ source
     * (presumably) wakes us back up again. */
    if (!loop_alive)
        return FALSE;

    /* Otherwise, check the timeout. If the timeout is 0, that means we're
     * ready to go. Otherwise, keep sleeping until the timeout happens again. */
    int t = uv_backend_timeout (source->loop);
    *timeout = t;

    if (t == 0)
        return TRUE;
    else
        return FALSE;
}

static gboolean uv_loop_source_dispatch (GSource *base, GSourceFunc callback, gpointer user_data) {
    struct uv_loop_source *source = (struct uv_loop_source *) base;
    uv_run (source->loop, UV_RUN_NOWAIT);
    Util::CallNextTickCallback();
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs uv_loop_source_funcs = {
    uv_loop_source_prepare,
    NULL,
    uv_loop_source_dispatch,
    NULL,

    NULL, NULL,
};

static GSource *uv_loop_source_new (uv_loop_t *loop) {
    struct uv_loop_source *source = (struct uv_loop_source *) g_source_new (&uv_loop_source_funcs, sizeof (*source));
    source->loop = loop;
    g_source_add_unix_fd (&source->source,
                          uv_backend_fd (loop),
                          (GIOCondition) (G_IO_IN | G_IO_OUT | G_IO_ERR));
    return &source->source;
}

void StartLoop() {
    GSource *source = uv_loop_source_new (uv_default_loop ());
    g_source_attach (source, NULL);
}

Local<Array> GetLoopStack() {
    return Nan::New<Array>(loopStack);
}

void QuitLoopStack() {
    Local<Array> stack = GetLoopStack();

    for (uint32_t i = 0; i < stack->Length(); i++) {
        Local<Object> fn = Nan::Get(stack, i).ToLocalChecked()->ToObject();
        Local<Object> self = fn;

        log("calling %s", *Nan::Utf8String(Nan::Get(fn, UTF8("name")).ToLocalChecked()));

        Nan::CallAsFunction(fn, self, 0, nullptr);
    }

    loopStack.Reset(Nan::New<Array>());
}

};
