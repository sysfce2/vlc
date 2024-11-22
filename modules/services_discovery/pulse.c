/**
 * @file pulse.c
 * @brief List of PulseAudio sources for VLC media player
 */
/*****************************************************************************
 * Copyright © 2011 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <search.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_services_discovery.h>
#include <pulse/pulseaudio.h>
#include "audio_output/vlcpulse.h"

static int Open (vlc_object_t *);
static void Close (vlc_object_t *);

#include <vlc_modules.h>

VLC_SD_PROBE_HELPER("pulse", N_("Audio capture"), SD_CAT_DEVICES);

vlc_module_begin ()
    set_shortname (N_("Audio capture"))
    set_description (N_("Audio capture (PulseAudio)"))
    set_subcategory (SUBCAT_PLAYLIST_SD)
    set_capability ("services_discovery", 0)
    set_callbacks (Open, Close)
    add_shortcut ("pulse", "pa", "pulseaudio", "audio")

    VLC_SD_PROBE_SUBMODULE
vlc_module_end ()

typedef struct
{
    void                 *root;
    void                 *root_card;
    pa_context           *context;
    pa_threaded_mainloop *mainloop;
    bool is_pipewire;
} services_discovery_sys_t;

static void SourceCallback(pa_context *, const pa_source_info *, int, void *);
static void ContextCallback(pa_context *, pa_subscription_event_type_t,
                            uint32_t, void *);

static void server_info_cb(pa_context *ctx, const pa_server_info *info,
                           void *userdata)
{
    services_discovery_t *sd = userdata;

    msg_Dbg(sd, "server %s version %s on %s@%s", info->server_name,
            info->server_version, info->user_name, info->host_name);

    services_discovery_sys_t *sys = sd->p_sys;

    sys->is_pipewire = strcasestr(info->server_name, "pipewire") != NULL;
    pa_threaded_mainloop_signal(sys->mainloop, 0);

    (void) ctx;
}

static int Open (vlc_object_t *obj)
{
    services_discovery_t *sd = (services_discovery_t *)obj;
    pa_operation *op;
    pa_context *ctx;

    services_discovery_sys_t *sys = malloc (sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    ctx = vlc_pa_connect (obj, &sys->mainloop);
    if (ctx == NULL)
    {
        free (sys);
        return VLC_EGENERIC;
    }

    sd->p_sys = sys;
    sd->description = _("Audio capture");
    sys->context = ctx;
    sys->root = NULL;
    sys->root_card = NULL;
    sys->is_pipewire = false;

    pa_threaded_mainloop_lock(sys->mainloop);
    op = pa_context_get_server_info(sys->context, server_info_cb, sd);
    if (likely(op != NULL))
    {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(sys->mainloop);
        if (sys->is_pipewire && module_exists("pipewirelist"))
        {
            msg_Dbg(sd, "refusing to use PipeWire");
            pa_threaded_mainloop_unlock(sys->mainloop);
            vlc_pa_disconnect(obj, sys->context, sys->mainloop);
            free(sys);
            return -ENOTSUP;
        }
        pa_operation_unref(op);
    }

    /* Subscribe for source events */
    const pa_subscription_mask_t mask = PA_SUBSCRIPTION_MASK_SOURCE;
    pa_context_set_subscribe_callback (ctx, ContextCallback, sd);
    op = pa_context_subscribe (ctx, mask, NULL, NULL);
    if (likely(op != NULL))
        pa_operation_unref (op);

    /* Enumerate existing sources */
    op = pa_context_get_source_info_list (ctx, SourceCallback, sd);
    if (likely(op != NULL))
    {
        //while (pa_operation_get_state (op) == PA_OPERATION_RUNNING)
        //    pa_threaded_mainloop_wait (sys->mainloop);
        pa_operation_unref (op);
    }
    pa_threaded_mainloop_unlock (sys->mainloop);
    return VLC_SUCCESS;
/*
error:
    pa_threaded_mainloop_unlock (sys->mainloop);
    vlc_pa_disconnect (obj, ctx, sys->mainloop);
    free (sys);
    return VLC_EGENERIC;*/
}

struct device
{
    uint32_t index;
    input_item_t *item;
    services_discovery_t *sd;
};

struct card
{
    input_item_t *item;
    services_discovery_t *sd;
    char name[];
};

static void DestroySource (void *data)
{
    struct device *d = data;

    services_discovery_RemoveItem (d->sd, d->item);
    input_item_Release (d->item);
    free (d);
}

static void DestroyCard(void *data)
{
    struct card *c = data;

    services_discovery_RemoveItem(c->sd, c->item);
    input_item_Release(c->item);
    free(c);
}

/**
 * Compares two devices (to support binary search).
 */
static int cmpsrc (const void *a, const void *b)
{
    const uint32_t *pa = a, *pb = b;
    uint32_t idxa = *pa, idxb = *pb;

    return (idxa != idxb) ? ((idxa < idxb) ? -1 : +1) : 0;
}

static int cmpcard (const void *a, const void *b)
{
    const struct card *ca = a, *cb = b;
    return strcmp(ca->name, cb->name);
}

static input_item_t *AddCard (services_discovery_t *sd, const pa_source_info *info)
{
    services_discovery_sys_t *sys = sd->p_sys;

    const char *card_name = pa_proplist_gets(info->proplist, "device.product.name");
    if (card_name == NULL)
        card_name = N_("Generic");

    struct card *c = malloc(sizeof(*c) + strlen(card_name) + 1);
    if (unlikely(c == NULL))
        return NULL;
    strcpy(c->name, card_name);

    void **cp = tsearch(c, &sys->root_card, cmpcard);
    if (cp == NULL) /* Out-of-memory */
    {
        free(c);
        return NULL;
    }
    if (*cp != c)
    {
        free(c);
        c = *cp;
        assert(c->item != NULL);
        return c->item;
    }

    c->item = input_item_NewExt("vlc://nop", c->name,
                                INPUT_DURATION_INDEFINITE,
                                ITEM_TYPE_NODE, ITEM_LOCAL);

    if (unlikely(c->item == NULL))
    {
        tdelete(c, &sys->root_card, cmpcard);
        return NULL;
    }
    services_discovery_AddItem(sd, c->item);
    c->sd = sd;

    return c->item;
}

/**
 * Adds a source.
 */
static int AddSource (services_discovery_t *sd, const pa_source_info *info)
{
    services_discovery_sys_t *sys = sd->p_sys;

    msg_Dbg (sd, "adding %s (%s)", info->name, info->description);

    char *mrl;
    if (unlikely(asprintf (&mrl, "pulse://%s", info->name) == -1))
        return -1;

    input_item_t *item = input_item_NewCard (mrl, info->description);
    free (mrl);
    if (unlikely(item == NULL))
        return -1;

    struct device *d = malloc (sizeof (*d));
    if (unlikely(d == NULL))
    {
        input_item_Release (item);
        return -1;
    }
    d->index = info->index;
    d->item = item;

    void **dp = tsearch (d, &sys->root, cmpsrc);
    if (dp == NULL) /* Out-of-memory */
    {
        free (d);
        input_item_Release (item);
        return -1;
    }
    if (*dp != d) /* Update existing source */
    {
        free (d);
        d = *dp;
        input_item_SetURI (d->item, item->psz_uri);
        input_item_SetName (d->item, item->psz_name);
        input_item_Release (item);
        return 0;
    }

    input_item_t *card = AddCard(sd, info);
    services_discovery_AddSubItem(sd, card, item);
    d->sd = sd;
    return 0;
}

static void SourceCallback(pa_context *ctx, const pa_source_info *i, int eol,
                           void *userdata)
{
    services_discovery_t *sd = userdata;

    if (eol)
        return;
    AddSource (sd, i);
    (void) ctx;
}

/**
 * Removes a source (if present) by index.
 */
static void RemoveSource (services_discovery_t *sd, uint32_t idx)
{
    services_discovery_sys_t *sys = sd->p_sys;

    void **dp = tfind (&idx, &sys->root, cmpsrc);
    if (dp == NULL)
        return;

    struct device *d = *dp;
    tdelete (d, &sys->root, cmpsrc);
    DestroySource (d);
}

static void ContextCallback(pa_context *ctx, pa_subscription_event_type_t type,
                            uint32_t idx, void *userdata)
{
    services_discovery_t *sd = userdata;
    pa_operation *op;

    assert ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK)
                                              == PA_SUBSCRIPTION_EVENT_SOURCE);
    switch (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK)
    {
      case PA_SUBSCRIPTION_EVENT_NEW:
      case PA_SUBSCRIPTION_EVENT_CHANGE:
        op = pa_context_get_source_info_by_index(ctx, idx, SourceCallback, sd);
        if (likely(op != NULL))
            pa_operation_unref(op);
        break;

      case PA_SUBSCRIPTION_EVENT_REMOVE:
        RemoveSource (sd, idx);
        break;
    }
}

static void Close (vlc_object_t *obj)
{
    services_discovery_t *sd = (services_discovery_t *)obj;
    services_discovery_sys_t *sys = sd->p_sys;

    vlc_pa_disconnect (obj, sys->context, sys->mainloop);
    tdestroy (sys->root, DestroySource);
    tdestroy (sys->root_card, DestroyCard);
    free (sys);
}
