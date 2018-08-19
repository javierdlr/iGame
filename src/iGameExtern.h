#pragma once
#define MUIA_Dtpic_Name 0x80423d72
#define TEMPLATE "SCREENSHOT/K"
#define DEFAULT_GAMELIST_FILE "PROGDIR:gameslist"
#define DEFAULT_REPOS_FILE "PROGDIR:repos.prefs"
#define DEFAULT_GENRES_FILE "PROGDIR:genres"
#define DEFAULT_SCREENSHOT_FILE "PROGDIR:igame.iff"

#define FILENAME_HOTKEY 'f'
#define QUALITY_HOTKEY 'q'
#define QUALITY_DEFAULT MUIV_Guigfx_Quality_Low
#define SCALEUP_HOTKEY 'u'
#define SCALEUP_DEFAULT FALSE
#define SCALEDOWN_HOTKEY 'd'
#define SCALEDOWN_DEFAULT TRUE
#define TRANSMASK_HOTKEY 'm'
#define TRANSMASK_DEFAULT FALSE
#define TRANSCOLOR_HOTKEY 'c'
#define TRANSCOLOR_DEFAULT FALSE
#define TRANSRGB_HOTKEY 'r'
#define TRANSRGB_DEFAULT (0x0)
#define PICASPECT_DEFAULT TRUE
#define PICASPECT_HOTKEY 'a'
#define SCREENASPECT_DEFAULT TRUE
#define SCREENASPECT_HOTKEY 's'
#define SCALEMODEMASK(u,d,p,s) (((u)?GGSMF_SCALEUP:0)|((d)?GGSMF_SCALEDOWN:0)|((p)?GGSMF_KEEPASPECT_PICTURE:0)|((s)?GGSMF_KEEPASPECT_SCREEN:0))

void scan_repositories();
void open_list();
void save_list(int check_exists);
void save_list_as();
void export_list();
void game_duplicate();
void game_delete();
void game_properties_ok();
void filter_change();
void launch_game();
void list_show_hidden();
void app_start();
void game_properties();
void add_non_whdload();
void game_click();
void genres_click();
void non_whdload_ok();
void repo_stop();
void repo_add();
void repo_remove();
void setting_filter_use_enter_changed();
void setting_save_stats_on_exit_changed();
void setting_smart_spaces_changed();
void setting_titles_from_changed();
void setting_show_screenshot_changed();
void setting_use_gui_gfx_changed();
void setting_screenshot_size_changed();
void settings_save();
void setting_hide_side_panel_changed();
void setttings_use();
