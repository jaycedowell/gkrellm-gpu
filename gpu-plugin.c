/* GKrellM
|  Copyright (C) 2025 Jayce Dowell
|
|  Based on GKrellM codebase by Bill Wilson
|
|  GKrellM GPU plugin - Monitor NVIDIA GPU statistics via NVML
|
|
|  GKrellM is free software: you can redistribute it and/or modify it
|  under the terms of the GNU General Public License as published by
|  the Free Software Foundation, either version 3 of the License, or
|  (at your option) any later version.
|
|  GKrellM is distributed in the hope that it will be useful, but WITHOUT
|  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
|  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
|  License for more details.
|
|  You should have received a copy of the GNU General Public License
|  along with this program. If not, see http://www.gnu.org/licenses/
|
|
|  Additional permission under GNU GPL version 3 section 7
|
|  If you modify this program, or any covered work, by linking or
|  combining it with the OpenSSL project's OpenSSL library (or a
|  modified version of that library), containing parts covered by
|  the terms of the OpenSSL or SSLeay licenses, you are granted
|  additional permission to convey the resulting work.
|  Corresponding Source for a non-source form of such a combination
|  shall include the source code for the parts of OpenSSL used as well
|  as that of the covered work.
*/

#include <gkrellm2/gkrellm.h>

#include <nvml.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define PLUGIN_PLACEMENT  (MON_CPU | MON_INSERT_AFTER)

#define CONFIG_NAME "GPU"
#define STYLE_NAME "gpu"
#define MONITOR_PLUGIN_NAME "gpu"
#define GPU_TICKS_PER_SECOND 100

/* Plugin data structure for each GPU detected */
typedef struct {
    gchar        *name;            /* GPU name like "gpu0", "gpu1" etc. */
    gchar        *label;           /* Display label like "GPU0", "GPU1" */
    gint         instance;         /* GPU device index */
    gboolean     enabled;          /* If monitoring is enabled */
    gboolean     is_composite;     /* If this is the composite GPU (average of all GPUs) */
    
    GtkWidget    *vbox;
    GkrellmPanel *panel;           /* Panel to display in */
    GkrellmChart *chart;           /* Chart for GPU utilization */
    GkrellmChartconfig *cconfig;   /* Chart configuration */
    GkrellmChartdata *util_cd;     /* Chart data for utilization */
    GkrellmChartdata *mem_cd;      /* Chart data for fractional memory usage */
    GkrellmKrell  *krell;          /* Krell for GPU utilization */
    
    gboolean     show_temperature; /* If temperature should be shown */
    gpointer     sensor_temp;      /* Temperature sensor */
    GkrellmDecal *sensor_decal;    /* Temperature decal */
    
    GkrellmAlert *alert;           /* Alert for high utilization */
    
    GkrellmLauncher launch;        /* Launch command */
    
    gulong       utilization;      /* Current GPU utilization */
    gulong       total_memory;     /* Total memory available */
    gulong       used_memory;      /* Currently used memory */
    
    gfloat       temperature;      /* Current temperature */
    
    gboolean     extra_info;       /* Show extra info on chart */
} GpuPlugin;

/* Plugin global variables */
static GList *gpu_list = NULL;          /* List of GpuPlugin instances */
static GpuPlugin *composite_gpu = NULL; /* Composite GPU (average of all) */
static gint n_gpus = 0;                 /* Number of GPUs detected */

static GkrellmMonitor *monitor;         /* Our plugin monitor */
static GkrellmAlert *gpu_alert = NULL;  /* Alert template */

static gint style_id;                   /* Our style ID */
static GtkWidget *gpu_vbox;             /* Box holding the widget */
static GtkWidget *text_format_combo_box;/* Combo box for setting the extra info */
static gboolean show_panel_labels = TRUE;
static gboolean config_tracking = FALSE;

static gchar *text_format;       /* Default text format */
static gchar *text_format_locale;/* Localized text format */

/* Forward declarations */
static void cleanup_plugin(void);
static void draw_sensor_decals(GpuPlugin *gpu);
static void refresh_gpu_chart(GpuPlugin *gpu);
static void format_gpu_data(GpuPlugin *gpu, gchar *src_string, gchar *buf, gint size);
static void cb_command_process(GkrellmAlert *alert, gchar *src, gchar *dst, gint len, GpuPlugin *gpu);
static void cb_alert_trigger(GkrellmAlert *alert, gpointer data);
static void cd_set_alert(GtkWidget *button, gpointer data);
static void create_alert(void);
static gboolean fix_panel(GpuPlugin *gpu);
static void create_gpu_plugin(GtkWidget *vbox, gint first_create);
static void update_gpu_plugin(void);
static void create_gpu_config(GtkWidget *vbox);
static void apply_gpu_config(void);
static void save_gpu_config(FILE *f);
static void load_gpu_config(gchar *arg);

/* Initialize the NVML library and detect GPUs */
static gboolean
setup_gpu_interface(void)
{
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        g_warning("Failed to initialize NVML: %s\n", nvmlErrorString(result));
        return FALSE;
    }
    
    /* Get the device count */
    unsigned int deviceCount = 0;
    result = nvmlDeviceGetCount(&deviceCount);
    if (result != NVML_SUCCESS) {
        g_warning("Failed to get device count: %s\n", nvmlErrorString(result));
        nvmlShutdown();
        return FALSE;
    }
    
    n_gpus = deviceCount;
    
    /* If multiple GPUs, create a composite entry */
    if (n_gpus > 1) {
        composite_gpu = g_new0(GpuPlugin, 1);
        composite_gpu->name = g_strdup("gpu");
        composite_gpu->label = g_strdup("GPU");
        composite_gpu->is_composite = TRUE;
        composite_gpu->instance = -1;
        composite_gpu->enabled = TRUE;
        gpu_list = g_list_append(gpu_list, composite_gpu);
    }
    
    /* Create entries for each GPU */
    for (gint i = 0; i < n_gpus; i++) {
        GpuPlugin *gpu = g_new0(GpuPlugin, 1);
        gpu->instance = i;
        gpu->name = g_strdup_printf("gpu%d", i);
        gpu->label = g_strdup_printf("GPU%d", i);
        gpu->enabled = TRUE;
        gpu_list = g_list_append(gpu_list, gpu);
    }
    
    return TRUE;
}

/* Read data from all GPUs using NVML */
static void
read_gpu_data(void)
{
    GList *list;
    GpuPlugin *gpu;
    nvmlReturn_t result;
    nvmlDevice_t device;
    nvmlUtilization_t utilization;
    
    /* Reset composite GPU stats */
    if (composite_gpu) {
        composite_gpu->utilization = 0;
        composite_gpu->total_memory = 0;
        composite_gpu->used_memory = 0;
        composite_gpu->temperature = 0.0;
    }
    
    /* Loop over all GPUs found */
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *) list->data;
        
        /* Skip composite GPU */
        if (gpu->instance < 0) {
            continue;
        }
            
        /* Get the device handle */
        result = nvmlDeviceGetHandleByIndex(gpu->instance, &device);
        if (result != NVML_SUCCESS) {
            continue;
        }
        
        /* Get utilization rates - this is in percent (0-100) */
        result = nvmlDeviceGetUtilizationRates(device, &utilization);
        if (result == NVML_SUCCESS) {
            gpu->utilization = utilization.gpu;
        }
        
        /* Get memory info */
        nvmlMemory_t memory;
        result = nvmlDeviceGetMemoryInfo(device, &memory);
        if (result == NVML_SUCCESS) {
            gpu->total_memory = memory.total;
            gpu->used_memory = memory.used;
        }
        
        /* Get temperature if needed */
        if (gpu->show_temperature && gpu->sensor_decal) {
            unsigned int temp;
            result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
            if (result == NVML_SUCCESS && gpu->sensor_temp) {
                /* Store temperature for display */
                gpu->temperature = (gfloat)temp;
            }
        }
        
        /* Update composite GPU */
        if (composite_gpu) {
            composite_gpu->utilization += gpu->utilization;
            composite_gpu->total_memory += gpu->total_memory;
            composite_gpu->used_memory += gpu->used_memory;
            if (gpu->temperature > composite_gpu->temperature) {
                composite_gpu->temperature = gpu->temperature;
            }
        }
    }
    
    /* Average the utilization values for composite GPU */
    if (composite_gpu && n_gpus > 1) {
        composite_gpu->utilization /= n_gpus;
    }
}

/* Clean up NVML when plugin is unloaded */
static void
cleanup_plugin(void)
{
    GList *list;
    GpuPlugin *gpu;
    
    /* Free all GPU data structures */
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        
        g_free(gpu->name);
        g_free(gpu->label);
        
        if (gpu->launch.command) {
            g_free(gpu->launch.command);
        }
        if (gpu->launch.tooltip_comment) {
            g_free(gpu->launch.tooltip_comment);
        }
            
        g_free(gpu);
    }
    
    /* Close out the GPU list and composite object */
    g_list_free(gpu_list);
    gpu_list = NULL;
    composite_gpu = NULL;
    
    /* Free text format */
    if (text_format_locale && text_format_locale != text_format)
        g_free(text_format_locale);
    if (text_format)
        g_free(text_format);
        
    /* Shutdown NVML */
    nvmlShutdown();
}

/* Draw sensor (temperature) decals */
static void
draw_sensor_decals(GpuPlugin *gpu)
{
    GkrellmPanel *p = gpu->panel;
    gchar buf[64];
    
    if (gpu->show_temperature && gpu->sensor_decal) {
        /* Format temperature as a string */
        g_snprintf(buf, sizeof(buf), "%.1f C", gpu->temperature);
        
        /* Draw the temperature text on the decal */
        gkrellm_draw_decal_text(p, gpu->sensor_decal, buf, 0);
    }
}

/* Format GPU data for display */
static void
format_gpu_data(GpuPlugin *gpu, gchar *src_string, gchar *buf, gint size)
{
    gchar c, *s;
    gint len, utilization = 0, mem_used = 0, mem_total = 0, mem_used_frac = 0, t;

    if (!buf || size < 1)
        return;
    --size;
    *buf = '\0';
    if (!src_string)
        return;
    
    utilization = gpu->utilization;
    mem_used = gpu->used_memory / 1024;
    mem_total = gpu->total_memory / 1024;
    mem_used_frac = round(100 * (gfloat) mem_used / mem_total);
    
    for (s = src_string; *s != '\0' && size > 0; ++s) {
        len = 1;
        if (*s == '$' && *(s + 1) != '\0') {
            c = *(s + 1);
            t = -1;
            
            if (c == 'u') {
                t = utilization;
                if (t < 0)
                    t = 0;
                if (t > 100)
                    t = 100;
            }
            else if (c == 'm') {
                t = mem_used_frac;
                if (t < 0)
                    t = 0;
                if (t > 100)
                    t = 100;
            }
            else if (c == 'T')
                t = mem_total;
            else if (c == 'U')
                t = mem_used;
            else if (c == 'L')
                len = snprintf(buf, size, "%s", gpu->label);
            else if (c == 'N')
                len = snprintf(buf, size, "%d", gpu->instance);
            else if (c == 'H')
                len = snprintf(buf, size, "%s", gkrellm_get_hostname());
            else {
                *buf = *s;
                if (size > 1) {
                    *(buf + 1) = *(s + 1);
                    ++len;
                }
            }
            
            if (t >= 0) {
                if (c == 'T' || c == 'U') {
                    if (t > 50*1024*1024)
                        len = snprintf(buf, size, "%.0fG", (gfloat) t / (1024*1024));
                    else if (t > 1024*1024)
                        len = snprintf(buf, size, "%.1fG", (gfloat) t / (1024*1024));
                    else if (t > 50*1024)
                        len = snprintf(buf, size, "%.0fM", (gfloat) t / (1024));
                    else
                        len = snprintf(buf, size, "%.1fM", (gfloat) t / (1024));
                }
                else
                    len = snprintf(buf, size, "%d%%", t);
            }
            ++s;
        }
        else {
            *buf = *s;
        }
        
        size -= len;
        buf += len;
    }
    
    *buf = '\0';
}

/* Refresh chart UI */
static void
refresh_gpu_chart(GpuPlugin *gpu)
{
    GkrellmChart *cp = gpu->chart;

    gkrellm_draw_chartdata(cp);
    if (gpu->extra_info) {
        gchar buf[128];
        format_gpu_data(gpu, text_format_locale, buf, sizeof(buf));
        gkrellm_draw_chart_text(cp, style_id, buf);
    }
    gkrellm_draw_chart_to_screen(cp);
}

/* Process alert command variables */
static void
cb_command_process(GkrellmAlert *alert, gchar *src, gchar *dst, gint len, 
                   GpuPlugin *gpu)
{
    format_gpu_data(gpu, src, dst, len);
}

/* Handle alert triggering */
static void
cb_alert_trigger(GkrellmAlert *alert, gpointer data)
{
    GpuPlugin *gpu = (GpuPlugin *)data;
    GkrellmAlertdecal *ad;
    GkrellmDecal *d;

    if (alert && gpu && gpu->panel) {
        ad = &alert->ad;
        d = gpu->sensor_decal;
        if (d) {
            ad->x = d->x - 1;
            ad->y = d->y - 1;
            ad->w = d->w + 2;
            ad->h = d->h + 2;
            gkrellm_render_default_alert_decal(alert);
        }
        alert->panel = gpu->panel;
    }
}

/* Fix panel when sensor display changes */
static gboolean
fix_panel(GpuPlugin *gpu)
{
    GkrellmPanel *p = gpu->panel;
    GkrellmDecal *ds;
    gboolean result = FALSE;
    
    ds = gpu->sensor_decal;
    if (!ds) {
        return FALSE;
    }
    
    gpu->show_temperature = TRUE;
    
    if (!gkrellm_demo_mode()) {
        gkrellm_sensor_alert_connect(gpu->sensor_temp, 
                                   (void (*)(GkrellmAlert *, gpointer))cb_alert_trigger, 
                                   gpu);
    }

    if (gpu->sensor_temp || gkrellm_demo_mode()) {
        gpu->show_temperature = TRUE;
    }
    
    if (gpu->show_temperature) {
        if (!gkrellm_is_decal_visible(ds)) {
            gkrellm_make_decal_visible(p, ds);
        }
        result = TRUE;
    }
    
    gkrellm_draw_panel_label(p);
    draw_sensor_decals(gpu);
    gkrellm_draw_panel_layers(p);
    
    return result;
}

static gint
gpu_chart_expose_event(GtkWidget *widget, GdkEventButton *ev)
{
    GList *list;
    GpuPlugin *gpu;
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        
        if (widget == gpu->chart->drawing_area || widget == gpu->panel->drawing_area) {
            if (ev->type == GDK_BUTTON_PRESS && ev->button == 1) {
                gpu->extra_info = gpu->extra_info == TRUE ? FALSE : TRUE;
                gkrellm_config_modified();
                refresh_gpu_chart(gpu);
                break;
            }
            else if ((ev->button == 1 && ev->type == GDK_2BUTTON_PRESS) \
                     || (ev->button == 3)) {
                gkrellm_chartconfig_window_create(gpu->chart);
                break;
            }
        }
    }
    
    return FALSE;
}

/* Create the plugin UI */
static void
create_gpu_plugin(GtkWidget *vbox, gint first_create)
{
    GList *list;
    GpuPlugin *gpu;
    GkrellmStyle *style;
    GkrellmPanel *p;
    GkrellmChart *cp;
    
    /* Create panel and chart for each GPU */
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        
        /* Skip creating UI for disabled GPUs */
        if (!gpu->enabled) {
            continue;
        }
        
        /* Create chart */
        if( first_create ) {
            gpu->vbox = gtk_vbox_new(FALSE, 0);
            gtk_container_add(GTK_CONTAINER(gpu_vbox), gpu->vbox);
            gtk_widget_show(gpu->vbox); 
            gpu->chart = gkrellm_chart_new0();
            gpu->chart->panel = gkrellm_panel_new0();
            gpu->panel = gpu->chart->panel;
        }
        cp = gpu->chart;
        p = cp->panel;
        
        /* Apply style */
        style = gkrellm_panel_style(style_id);
        gkrellm_create_krell(p, gkrellm_krell_panel_piximage(style_id), style);
        gpu->krell = KRELL(p);
        
        /* Create chart and configure */
        gkrellm_chart_create(vbox, monitor, cp, &gpu->cconfig);
        gkrellm_set_draw_chart_function(cp, refresh_gpu_chart, gpu);
        gpu->util_cd = gkrellm_add_default_chartdata(cp, _("utilization"));
        gpu->mem_cd = gkrellm_add_default_chartdata(cp, _("memory"));
        
        gkrellm_monotonic_chartdata(gpu->util_cd, FALSE);
        gkrellm_monotonic_chartdata(gpu->mem_cd, FALSE);
        gkrellm_set_chartdata_draw_style_default(gpu->util_cd, CHARTDATA_LINE);
        gkrellm_set_chartdata_draw_style_default(gpu->mem_cd, CHARTDATA_LINE);
        gkrellm_set_chartdata_flags(gpu->mem_cd, CHARTDATA_ALLOW_HIDE);
         
        /* Disable auto grid resolution */
        gkrellm_chartconfig_grid_resolution_adjustment(gpu->cconfig,
                                                       TRUE, 0,
                                                       (gfloat) 20, (gfloat) 100,
                                                       0, 0, 0, 70);
         
        /* Create sensor decals if needed */
        gpu->sensor_decal = NULL;
        if (show_panel_labels) {
            /* Create a text decal for temperature display */
            gpu->sensor_decal = gkrellm_create_decal_text(p, "", 
                                     gkrellm_panel_alt_textstyle(style_id),
                                     style, -1, -1, -1);
        }
        
        /* Configure panel with label */
        gkrellm_panel_configure(p, show_panel_labels ? gpu->label : NULL, style);
        
        /* Set label position to center */
        if (p->label) {
            p->label->position = GKRELLM_LABEL_CENTER;
        }
        
        /* Create panel */
        gkrellm_panel_create(vbox, monitor, p);
        
        /* Handle sensors */
        fix_panel(gpu);
        
        /* Setup krell */
        gkrellm_set_krell_full_scale(gpu->krell, 100, 1);
        
        /* Connect signals */
        if (first_create) {
            g_signal_connect(G_OBJECT(cp->drawing_area), "button_press_event",
                             G_CALLBACK(gpu_chart_expose_event), gpu);
            g_signal_connect(G_OBJECT(p->drawing_area), "button_press_event",
                             G_CALLBACK(gpu_chart_expose_event), gpu);
        }
        
        /* Set extra info on */
        gpu->extra_info = TRUE;
        
        /* Setup launcher */
        gkrellm_setup_launcher(p, &gpu->launch, CHART_PANEL_TYPE, 4);
        
        /* Allocate chart data */
        gkrellm_alloc_chartdata(cp);
    }
}

/* Update plugin data and UI */
static void
update_gpu_plugin(void)
{
    GList *list;
    GpuPlugin *gpu;
    GkrellmPanel *p;
    GkrellmChart *cp;
    GkrellmKrell *krell;
    
    /* Read GPU data */
    read_gpu_data();
    
    /* For each GPU, update UI */
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        
        if (!gpu->enabled) {
            continue;
        }
        if (!gpu) {
            continue;
        }
        
        cp = gpu->chart;
        p = cp->panel;
        
        if (GK.second_tick) {
            /* Store chart data */
            if (cp && gpu->util_cd && gpu->mem_cd) {
                gkrellm_store_chartdata(cp, 0, gpu->utilization, (gint) round((gfloat) 100 * gpu->used_memory / gpu->total_memory));
                
                refresh_gpu_chart(gpu);
            }
            
            /* Check alerts */
            if (gpu->alert && !gpu->is_composite) {
                gkrellm_check_alert(gpu->alert, (gfloat)gpu->utilization);
            }
        }
        
        if (GK.two_second_tick && gpu->show_temperature) {
            draw_sensor_decals(gpu);
        }
        
        /* Update krell */
        krell = gpu->krell;
        gkrellm_update_krell(p, krell, gpu->utilization);
        gkrellm_panel_label_on_top_of_decals(p, gkrellm_alert_decal_visible(gpu->alert));
        gkrellm_draw_panel_layers(p);
    }
}

/* Format callback for alert processing */
static void 
cb_alert_config(GkrellmAlert *alert, gpointer data)
{
    GList *list;
    GpuPlugin *gpu;
    
    /* Duplicate the main alert for each GPU */
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        gkrellm_alert_dup(&gpu->alert, gpu_alert);
        gkrellm_alert_trigger_connect(gpu->alert, cb_alert_trigger, gpu);
        gkrellm_alert_command_process_connect(gpu->alert, 
            (void (*)(GkrellmAlert *, gchar *, gchar *, gint, void *))cb_command_process, gpu);
    }
}

static void
cb_set_alert(GtkWidget *button, gpointer data)
{
    if (!gpu_alert) {
        create_alert();
    }
    gkrellm_alert_config_window(&gpu_alert);
}

/* Create an alert for GPU utilization */
static void
create_alert(void)
{
    gpu_alert = gkrellm_alert_create(NULL, _("GPU"),
                                     _("Temperature"),
                                     TRUE, FALSE, TRUE,
                                     100, 10, 1, 10, 0);
    gkrellm_alert_delay_config(gpu_alert, 1, 60 * 60, 2);
    gkrellm_alert_config_connect(gpu_alert, cb_alert_config, NULL);
    /* This alert is a master to be dupped and is itself never checked */
}

/* Info text for config dialog */
static gchar *gpu_info_text[] = {
    N_("<h>Chart Labels\n"),
    N_("Substitution variables for the format string for chart labels:\n"),
    N_("\t$L    the GPU label\n"),
    N_("\t$N    the GPU number\n"),
    N_("\t$u    utilization percent\n"),
    N_("\t$m    memory percent usage\n"),
    N_("\t$U    memory used size\n"),
    N_("\t$T    total memory size\n"),
    "\n",
    N_("Substitution variables may be used in alert commands.\n")
};

static void
cb_text_format(GtkWidget *widget, gpointer data)
{
    GList *list;
    GpuPlugin *gpu;
    gchar *s;
    GtkWidget *entry;
    
    entry = gtk_bin_get_child(GTK_BIN(text_format_combo_box));
    s = gkrellm_gtk_entry_get_text(&entry);
    gkrellm_locale_dup_string(&text_format, s, &text_format_locale);
    
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        refresh_gpu_chart(gpu);
    }
}

/* Create the config UI */
static void
create_gpu_config(GtkWidget *vbox)
{
    GtkWidget *tabs;
    GtkWidget *button;
    GtkWidget *hbox, *cvbox, *vbox1, *vbox2;
    GtkWidget *text;
    GtkWidget *table;
    GList *list;
    GpuPlugin *gpu;
    gchar buf[128];
    gint i;
    
    tabs = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(vbox), tabs, TRUE, TRUE, 0);

    /* Options tab */
    cvbox = gkrellm_gtk_framed_notebook_page(tabs, _("Options"));
    
    /* Show panel labels checkbox */
    gkrellm_gtk_check_button_connected(cvbox, NULL, show_panel_labels,
            FALSE, FALSE, 0, NULL, NULL,
            _("Show labels in panels (no labels reduces vertical space)"));
            
    vbox1 = gkrellm_gtk_category_vbox(cvbox,
                _("GPU Charts Select"),
                4, 0, TRUE);
                
    vbox2 = gkrellm_gtk_scrolled_vbox(vbox1, NULL,
                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
                    
    /* Create checkboxes for each GPU */
    for (i = 0, list = gpu_list; list; list = list->next, ++i) {
        gpu = (GpuPlugin *)list->data;
        
        if (i == 0 && n_gpus > 1) {
            snprintf(buf, sizeof(buf), _("Composite GPU."));
        }
        else {
            snprintf(buf, sizeof(buf), _("%s"), gpu->name);
        }
        
        gkrellm_gtk_check_button_connected(vbox2, NULL, gpu->enabled,
                                           FALSE, FALSE, 0, NULL, NULL, buf);
    }
    
    /* Setup tab */
    cvbox = gkrellm_gtk_framed_notebook_page(tabs, _("Setup"));
    
    vbox1 = gkrellm_gtk_category_vbox(cvbox,
                                      _("Format String for Chart Labels"),
                                      4, 0, TRUE);
                
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox1), hbox, FALSE, FALSE, 0);
    
    /* Text format selector */
    text_format_combo_box = gtk_combo_box_text_new_with_entry();
    gtk_box_pack_start(GTK_BOX(hbox), text_format_combo_box, TRUE, TRUE, 0);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(text_format_combo_box),
                                   text_format);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(text_format_combo_box),
                                   "$u");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(text_format_combo_box),
                                   _("\\f$L\\n$T"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(text_format_combo_box),
                                   _("\\fu \\.$u\\n\\fs \\.$s"));
    
    gtk_combo_box_set_active(GTK_COMBO_BOX(text_format_combo_box), 0);
    g_signal_connect(G_OBJECT(text_format_combo_box), "changed",
                     G_CALLBACK(cb_text_format), NULL);
    
    /* Launch commands */
    vbox1 = gkrellm_gtk_category_vbox(cvbox,
                                      _("Launch Commands"),
                                      4, 0, TRUE);
    vbox1 = gkrellm_gtk_scrolled_vbox(vbox1, NULL,
                                      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    table = gkrellm_gtk_launcher_table_new(vbox1, g_list_length(gpu_list));
    
    /* Setup launchers for each GPU */
    for (i = 0, list = gpu_list; list; list = list->next, ++i) {
        gpu = (GpuPlugin *)list->data;
        snprintf(buf, sizeof(buf), _("%s"), gpu->name);
        
        /* Add entries for launch command and tooltip */
        // This is a simplified version without the signal connections
        gtk_table_attach(GTK_TABLE(table), 
                         gtk_label_new(buf), 0, 1, i, i+1, 
                         GTK_FILL, GTK_FILL, 0, 0);
        
        GtkWidget *entry = gtk_entry_new();
        gtk_table_attach(GTK_TABLE(table), 
                         entry, 1, 2, i, i+1, 
                         GTK_EXPAND|GTK_FILL, GTK_FILL, 0, 0);
            
        if (gpu->launch.command) {
            gtk_entry_set_text(GTK_ENTRY(entry), gpu->launch.command);
        }
    }
    
    /* Alert button */
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gkrellm_gtk_alert_button(hbox, NULL, FALSE, FALSE, 4, TRUE,
                             cb_set_alert, NULL);
    
    /* Info tab */
    cvbox = gkrellm_gtk_framed_notebook_page(tabs, _("Info"));
    text = gkrellm_gtk_scrolled_text_view(cvbox, NULL,
                                          GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    for (i = 0; i < sizeof(gpu_info_text)/sizeof(gchar *); ++i) {
        gkrellm_gtk_text_view_append(text, _(gpu_info_text[i]));
    }
}

/* Apply config changes */
static void
apply_gpu_config(void)
{
    // Currently, there's no dynamic configuration to apply
    // This would normally handle changes from the config UI
    GList *list;
    GpuPlugin *gpu;
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        
        gkrellm_config_modified();
        refresh_gpu_chart(gpu);
    }
}

/* Save plugin config to file */
static void
save_gpu_config(FILE *f)
{
    GList *list;
    GpuPlugin *gpu;
    
    fprintf(f, "%s show_panel_labels %d\n", CONFIG_NAME, show_panel_labels);
    fprintf(f, "%s text_format %s\n", CONFIG_NAME, text_format);
    
    for (list = gpu_list; list; list = list->next) {
        gpu = (GpuPlugin *)list->data;
        fprintf(f, "%s enabled %s %d\n", CONFIG_NAME,
                    gpu->name, gpu->enabled);
                    
        if (gpu->launch.command && *(gpu->launch.command) != '\0') {
            fprintf(f, "%s launch %s %s\n", CONFIG_NAME,
                    gpu->name, gpu->launch.command);
        }
        
        if (gpu->launch.tooltip_comment && *(gpu->launch.tooltip_comment) != '\0') {
            fprintf(f, "%s tooltip_comment %s %s\n", CONFIG_NAME,
                    gpu->name, gpu->launch.tooltip_comment);
        }
        
        fprintf(f, "%s extra_info %s %d\n", CONFIG_NAME,
                gpu->name, gpu->extra_info);
    }
    
    /* Save alert config */
    if (gpu_alert) {
        gkrellm_save_alertconfig(f, gpu_alert, CONFIG_NAME, NULL);
    }
}

/* Load plugin config from file */
static void
load_gpu_config(gchar *arg)
{
    GList *list;
    GpuPlugin *gpu;
    gchar config[32], item[512], gpu_name[32], command[512];
    gint n;
    
    n = sscanf(arg, "%31s %[^\n]", config, item);
    if (n == 2) {
        if (!strcmp(config, "show_panel_labels")) {
            sscanf(item, "%d\n", &show_panel_labels);
        }
        else if (!strcmp(config, "text_format")) {
            gkrellm_locale_dup_string(&text_format, item, &text_format_locale);
        }
        else if (!strcmp(config, "enabled")) {
            sscanf(item, "%31s %[^\n]", gpu_name, command);
            for (list = gpu_list; list; list = list->next) {
                gpu = (GpuPlugin *)list->data;
                if (strcmp(gpu->name, gpu_name) == 0) {
                    sscanf(command, "%d\n", &gpu->enabled);
                }
            }
        }
        else if (!strcmp(config, GKRELLM_ALERTCONFIG_KEYWORD)) {
            if (!gpu_alert) {
                create_alert();
            }
            gkrellm_load_alertconfig(&gpu_alert, item);
            cb_alert_config(gpu_alert, NULL);
        }
        else if (!strcmp(config, "extra_info")) {
            sscanf(item, "%31s %[^\n]", gpu_name, command);
            for (list = gpu_list; list; list = list->next) {
                gpu = (GpuPlugin *)list->data;
                if (strcmp(gpu->name, gpu_name) == 0) {
                    sscanf(command, "%d\n", &gpu->extra_info);
                }
            }
        }
        else if (!strcmp(config, "launch")) {
            sscanf(item, "%31s %[^\n]", gpu_name, command);
            for (list = gpu_list; list; list = list->next) {
                gpu = (GpuPlugin *)list->data;
                if (strcmp(gpu->name, gpu_name) == 0) {
                    gpu->launch.command = g_strdup(command);
                }
            }
        }
        else if (!strcmp(config, "tooltip_comment")) {
            sscanf(item, "%31s %[^\n]", gpu_name, command);
            for (list = gpu_list; list; list = list->next) {
                gpu = (GpuPlugin *)list->data;
                if (strcmp(gpu->name, gpu_name) == 0) {
                    gpu->launch.tooltip_comment = g_strdup(command);
                }
            }
        }
    }
}

/* Register temperature sensor */
gboolean
set_gpu_sensor(gpointer sr, gint type, gint n)
{
    GpuPlugin *gpu;

    if (!show_panel_labels)
        return FALSE;
        
    /* Find the GPU for this sensor index */
    gpu = g_list_nth_data(gpu_list, n);
    if (!gpu || !gpu->enabled) {
        return FALSE;
    }
    
    if (type == SENSOR_TEMPERATURE) {
        gpu->sensor_temp = sr;
    }
    else {
        return FALSE;
    }
    
    return fix_panel(gpu);
}

/* Entry point for the plugin - this function is called by GKrellM */
GkrellmMonitor *
gkrellm_init_plugin(void)
{
    /* Initialize NVML and detect GPUs */
    if (!setup_gpu_interface()) {
        g_warning("GPU plugin: failed to initialize NVML");
        return NULL;
    }
    
    /* Set the default text format */
    gkrellm_locale_dup_string(&text_format, "$u", &text_format_locale);
    
    /* Create alert */
    create_alert();
    
    /* Create our monitor */
    monitor = g_new0(GkrellmMonitor, 1);
    
    monitor->name = MONITOR_PLUGIN_NAME;
    monitor->create_monitor = create_gpu_plugin;
    monitor->update_monitor = update_gpu_plugin;
    monitor->create_config = create_gpu_config;
    monitor->apply_config = apply_gpu_config;
    
    monitor->save_user_config = save_gpu_config;
    monitor->load_user_config = load_gpu_config;
    monitor->config_keyword = CONFIG_NAME;
    
    monitor->undef2 = cleanup_plugin;
    
    monitor->insert_before_id = PLUGIN_PLACEMENT;
    
    /* Register our style */
    style_id = gkrellm_add_chart_style(monitor, STYLE_NAME);
    
    return monitor;
}
