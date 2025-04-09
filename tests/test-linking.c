#include <stdio.h>
#include <dlfcn.h>

/* The expected plugin init function signature */
typedef int (*init_plugin_func)(void);

/* Mock a bunch of GKrellM stuff */
int GK;
int gkrellm_panel_create;
int gkrellm_alert_decal_visible;
int gkrellm_setup_launcher;
int gkrellm_chart_create;
int gkrellm_gtk_launcher_table_new;
int gkrellm_draw_chart_text;
int gkrellm_is_decal_visible;
int gkrellm_gtk_entry_get_text;
int gkrellm_config_modified;
int gkrellm_draw_chart_to_screen;
int gkrellm_gtk_check_button_connected;
int gkrellm_store_chartdata;
int gkrellm_alert_create;
int gkrellm_locale_dup_string;
int gkrellm_alert_trigger_connect;
int gkrellm_set_krell_full_scale;
int gkrellm_krell_panel_piximage;
int gkrellm_draw_decal_text;
int gkrellm_draw_panel_label;
int gkrellm_check_alert;
int gkrellm_draw_panel_layers;
int gkrellm_alert_config_window;
int gkrellm_panel_style;
int gkrellm_load_alertconfig;
int gkrellm_gtk_scrolled_vbox;
int gkrellm_create_krell;
int gkrellm_render_default_alert_decal;
int gkrellm_monotonic_chartdata;
int gkrellm_create_decal_text;
int gkrellm_panel_configure;
int gkrellm_gtk_alert_button;
int gkrellm_gtk_scrolled_text_view;
int gkrellm_alert_dup;
int gkrellm_get_hostname;
int gkrellm_panel_new0;
int gkrellm_chart_new0;
int gkrellm_alert_command_process_connect;
int gkrellm_sensor_alert_connect;
int gkrellm_add_default_chartdata;
int gkrellm_make_decal_visible;
int gkrellm_demo_mode;
int gkrellm_panel_alt_textstyle;
int gkrellm_save_alertconfig;
int gkrellm_gtk_text_view_append;
int gkrellm_add_chart_style;
int gkrellm_gtk_category_vbox;
int gkrellm_chartconfig_grid_resolution_adjustment;
int gkrellm_set_chartdata_flags;
int gkrellm_set_chartdata_draw_style_default;
int gkrellm_set_draw_chart_function;
int gkrellm_alloc_chartdata;
int gkrellm_alert_config_connect;
int gkrellm_chartconfig_window_create;
int gkrellm_alert_delay_config;
int gkrellm_panel_label_on_top_of_decals;
int gkrellm_update_krell;
int gkrellm_draw_chartdata;
int gkrellm_gtk_framed_notebook_page;

int main() {
    void *handle;
    char *error;
    init_plugin_func init_function;
    
    /* Open the plugin shared object */
    handle = dlopen("gpu-plugin.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "Error loading plugin: %s\n", dlerror());
        return 1;
    }
    
    /* Find the init function - replace with your actual init function name */
    init_function = (init_plugin_func) dlsym(handle, "gkrellm_init_plugin");
    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "Error finding init function: %s\n", error);
        dlclose(handle);
        return 1;
    }
    
    printf("Plugin loaded successfully and init function found\n");
    dlclose(handle);
    return 0;
}
